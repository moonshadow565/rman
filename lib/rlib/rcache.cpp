#include "rcache.hpp"

#include <common/xxhash.h>
#include <zstd.h>

#include <charconv>

#include "buffer.hpp"
#include "common.hpp"

using namespace rlib;

static constexpr auto rcache_file_flags(bool readonly) -> IO::Flags {
    return (readonly ? IO::READ : IO::WRITE) | IO::NO_INTERUPT | IO::NO_OVERGROW;
}

static auto rcache_file_path(fs::path base, std::size_t index) -> fs::path {
    if (!index) return base;
    return std::move(base.replace_extension(fmt::format(".{:05d}.bundle", index)));
}

RCache::RCache(Options const& options) : options_(options) {
    if (!options_.readonly) {
        options_.flush_size = std::max(1 * MiB, options_.flush_size);
        options_.max_size = std::max(options_.flush_size * 2, options_.max_size) - options_.flush_size;
    }
    rlib_assert(fs::exists(options.path));
    if (fs::is_directory(options.path)) {
        this->load_folder_internal();
    } else {
        this->load_file_internal();
    }
}

RCache::~RCache() { this->flush_internal(); }

auto RCache::add(RChunk const& chunk, std::span<char const> data) -> bool {
    if (!can_write()) {
        return false;
    }
    rlib_assert(chunk.compressed_size == data.size());
    if (lookup_.contains(chunk.chunkId)) {
        return false;
    }
    // check if we hit chunk limit
    auto const extra_data = sizeof(RChunk) + data.size();
    this->check_space(extra_data);
    writer_.chunks.push_back(chunk);
    lookup_[chunk.chunkId] = {chunk, BundleID::None, writer_.buffer.size() + writer_.toc_offset};
    rlib_assert(writer_.buffer.append(data));
    if (writer_.buffer.size() > options_.flush_size) {
        this->flush_internal();
    }
    writer_.end_offset += extra_data;
    return true;
}

auto RCache::add_uncompressed(std::span<char const> src, int level, HashType hash_type) -> RChunk::Src {
    auto id = RChunk::hash(src, hash_type);
    if (auto c = this->find_internal(id)) {
        rlib_assert(c->uncompressed_size == src.size());
        return *c;
    }
    thread_local Buffer buffer = {};
    rlib_assert(buffer.resize_destroy(ZSTD_compressBound(src.size())));
    auto size = rlib_assert_zstd(ZSTD_compress(buffer.data(), buffer.size(), src.data(), src.size(), level));
    auto chunk = RChunk::Src{};
    chunk.chunkId = id;
    chunk.uncompressed_size = src.size();
    chunk.compressed_size = (std::uint32_t)size;
    rlib_assert(this->add(chunk, buffer.subspan(0, size)));
    return chunk;
}

auto RCache::contains(ChunkID chunkId) const noexcept -> bool { return lookup_.contains(chunkId); }

auto RCache::get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb on_data) const -> std::vector<RChunk::Dst> {
    auto f = chunks.end();
    auto const e = chunks.end();
    for (auto i = chunks.begin(); i != f;) {
        if (auto c = this->find_internal(i->chunkId); c && c->uncompressed_size == i->uncompressed_size) {
            auto dst = RChunk::Dst{*c, i->hash_type, i->uncompressed_offset};
            *i = *(--f);
            *f = dst;
        } else {
            ++i;
        }
    }
    sort_by<&RChunk::Src::bundleId, &RChunk::Dst::compressed_offset, &RChunk::Dst::uncompressed_offset>(f, e);

    auto last_id = ChunkID::None;
    auto last_data = std::span<char const>{};
    for (auto i = f; i != e; ++i) {
        if (last_id == ChunkID::None || i->chunkId != last_id) {
            last_data = zstd_decompress(this->get_internal(*i), i->uncompressed_size);
            last_id = i->chunkId;
        }
        on_data(*i, last_data);
    }
    chunks.resize(f - chunks.begin());
    return std::move(chunks);
}

auto RCache::get_into(RChunk const& chunk, std::span<char> dst) const noexcept -> bool {
    if (auto c = this->find_internal(chunk.chunkId); c && c->uncompressed_size == chunk.uncompressed_size) {
        auto src = get_internal(*c);
        return ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size()) == chunk.uncompressed_size;
    }
    return false;
}

auto RCache::find_internal(ChunkID chunkId) const noexcept -> RChunk::Src const* {
    if (chunkId == ChunkID::None) {
        return nullptr;
    }
    auto i = lookup_.find(chunkId);
    if (i == lookup_.end()) {
        return nullptr;
    }
    return &i->second;
}

auto RCache::get_internal(RChunk::Src const& chunk) const -> std::span<char const> {
    if (files_.empty()) {
        thread_local struct Lazy {
            BundleID bundleId;
            std::unique_ptr<IO::MMap> io;
        } lazy;
        rlib_assert(lazy.bundleId != BundleID::None);
        if (lazy.bundleId != chunk.bundleId) {
            lazy.bundleId = BundleID::None;
            lazy.io = std::make_unique<IO::MMap>(fmt::format("{}/{}.bundle", options_.path, chunk.bundleId), IO::READ);
            lazy.bundleId = chunk.bundleId;
        }
        if (!in_range(chunk.compressed_offset, chunk.compressed_size, lazy.io->size())) [[unlikely]] {
            return {};
        }
        return lazy.io->copy(chunk.compressed_offset, chunk.compressed_size);
    }
    auto const& file = files_.at((std::size_t)chunk.bundleId);
    if (can_write() && &file == &files_.back() && chunk.compressed_offset >= writer_.toc_offset) {
        return writer_.buffer.subspan(chunk.compressed_offset - writer_.toc_offset, chunk.compressed_size);
    } else {
        return file->copy(chunk.compressed_offset, chunk.compressed_size);
    }
}

auto RCache::check_space(std::size_t extra) -> bool {
    // ensure we can allways at least write one file
    if (writer_.end_offset <= sizeof(RBUN::Footer)) {
        return false;
    }
    // still have space
    if (writer_.end_offset + extra < options_.max_size) {
        return false;
    }
    this->flush_internal();  // flush anything that we have atm
    auto const index = files_.size();
    auto const path = rcache_file_path(options_.path, index);
    auto const flags = rcache_file_flags(false);
    auto file = std::make_unique<IO::File>(path, flags);
    file->resize(0, 0);
    files_.push_back(std::move(file));
    writer_.toc_offset = 0;
    writer_.end_offset = sizeof(RBUN::Footer);
    writer_.chunks.clear();
    writer_.buffer.clear();
    this->flush_internal();
    return true;
}

auto RCache::flush_internal() -> bool {
    // Dont reflush when there is nothing to flush.
    if (!can_write() || (writer_.buffer.empty() && writer_.toc_offset != 0)) {
        return false;
    }
    auto toc_size = sizeof(RChunk) * writer_.chunks.size();
    RBUN::Footer footer = {
        .checksum = std::bit_cast<std::array<char, 8>>(XXH64((char const*)writer_.chunks.data(), toc_size, 0)),
        .entry_count = (std::uint32_t)writer_.chunks.size(),
        .version = RBUN::Footer::VERSION,
        .magic = {'R', 'B', 'U', 'N'},
    };
    auto new_toc_offset = writer_.toc_offset + writer_.buffer.size();
    rlib_assert(writer_.buffer.append({(char const*)writer_.chunks.data(), toc_size}));
    rlib_assert(writer_.buffer.append({(char const*)&footer, sizeof(footer)}));
    rlib_assert(files_.back()->write(writer_.toc_offset, writer_.buffer));
    writer_.buffer.clear();
    writer_.toc_offset = new_toc_offset;
    return true;
}

auto RCache::load_file_internal() -> void {
    for (fs::path path = options_.path;;) {
        auto const index = files_.size();
        auto next_path = rcache_file_path(options_.path, index + 1);
        auto const next_exists = fs::exists(next_path);
        auto const flags = rcache_file_flags(options_.readonly || next_exists);

        auto file = std::make_unique<IO::File>(path, flags);
        auto const is_empty = file->size() == 0;
        auto bundle = !is_empty ? RBUN::read(*file) : RBUN{};
        files_.push_back(std::move(file));
        for (auto& chunk : bundle.lookup) {
            chunk.second.bundleId = (BundleID)index;
        }
        lookup_.merge(std::move(bundle.lookup));

        if (flags & IO::WRITE) {
            writer_ = {
                .toc_offset = bundle.toc_offset,
                .end_offset = bundle.toc_offset + sizeof(RBUN::Footer),
                .chunks = std::move(bundle.chunks),
            };
            writer_.end_offset += sizeof(RChunk) * writer_.chunks.size();
            writer_.buffer.clear();
            can_write_ = true;
            if (is_empty) {
                this->flush_internal();
            } else {
                this->check_space(options_.flush_size);
            }
        }

        if (!next_exists) {
            break;
        }

        path = std::move(next_path);
    }
}

auto RCache::load_folder_internal() -> void {
    options_.readonly = true;
    for (auto const& entry : fs::directory_iterator(options_.path)) {
        auto const& path = entry.path();
        auto filename = path.filename().generic_string();
        if (!filename.ends_with(".bundle") || filename.find(".bundle") != 16) {
            continue;
        }
        auto bundleId = std::uint64_t{};
        auto [ptr, ec] = std::from_chars(filename.data(), filename.data() + 16, bundleId, 16);
        rlib_assert(ptr == filename.data() + 16);
        rlib_assert(ec == std::errc{});
        auto file = IO::File(path, IO::READ);
        auto bundle = RBUN::read(file);
        rlib_assert(bundle.bundleId == (BundleID)bundleId);
        lookup_.merge(std::move(bundle.lookup));
    }
}