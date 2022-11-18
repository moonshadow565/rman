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
    if (fs::exists(options.path) && fs::is_directory(options.path)) {
        this->load_folder_internal();
    } else if (options_.readonly) {
        this->load_file_internal_read_only();
    } else {
        this->load_file_internal_read_write();
    }
}

RCache::~RCache() { this->flush_internal(); }

auto RCache::add(RChunk const& chunk, std::span<char const> data) -> bool {
    if (!can_write()) {
        return false;
    }
    rlib_assert(chunk.compressed_size == data.size());
    std::lock_guard lock(this->mutex_);
    if (lookup_.contains(chunk.chunkId)) {
        return false;
    }
    this->add_internal(chunk, data);
    return true;
}

auto RCache::add_uncompressed(std::span<char const> src, int level, HashType hash_type) -> RChunk::Src {
    rlib_assert(can_write());
    auto id = RChunk::hash(src, hash_type);
    std::lock_guard lock(this->mutex_);
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
    this->add_internal(chunk, buffer.subspan(0, size));
    return chunk;
}

auto RCache::contains(ChunkID chunkId) const noexcept -> bool {
    std::shared_lock lock(this->mutex_);
    return lookup_.contains(chunkId);
}

auto RCache::get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb on_data) const -> std::vector<RChunk::Dst> {
    std::shared_lock lock(this->mutex_);
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

auto RCache::get_into(RChunk const& chunk, std::span<char> dst) const -> bool {
    std::shared_lock lock(this->mutex_);
    if (auto c = this->find_internal(chunk.chunkId); c) {
        rlib_assert(c->uncompressed_size == chunk.uncompressed_size);
        auto src = get_internal(*c);
        auto result = rlib_assert_zstd(ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size()));
        rlib_assert(result == chunk.uncompressed_size);
        return true;
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
        fflush(stdout);
        thread_local struct Lazy {
            BundleID bundleId = {};
            std::unique_ptr<IO::MMap> io = {};
        } lazy = {};
        rlib_assert(chunk.bundleId != BundleID::None);
        if (lazy.bundleId != chunk.bundleId) {
            auto path = fmt::format("{}/{}.bundle", options_.path, chunk.bundleId);
            lazy.bundleId = BundleID::None;
            lazy.io = std::make_unique<IO::MMap>(path, IO::READ);
            lazy.bundleId = chunk.bundleId;
        }
        return lazy.io->copy(chunk.compressed_offset, chunk.compressed_size);
    }
    auto const index = (std::size_t)chunk.bundleId;
    rlib_assert(index < files_.size());
    auto const& file = files_.at(index);
    if (can_write() && &file == &files_.back() && chunk.compressed_offset >= writer_.toc_offset) {
        return writer_.buffer.subspan(chunk.compressed_offset - writer_.toc_offset, chunk.compressed_size);
    } else {
        return file->copy(chunk.compressed_offset, chunk.compressed_size);
    }
}

auto RCache::add_internal(RChunk const& chunk, std::span<char const> data) -> void {
    // Space we will be adding this write
    auto const extra_data = sizeof(RChunk) + data.size();
    // only move to next bundle when we wrote at least one chunk and we run out of space
    if (writer_.chunks.size() && writer_.end_offset + extra_data > options_.max_size) {
        this->flush_internal();  // flush anything that we have atm
        auto const index = files_.size();
        auto const path = rcache_file_path(options_.path, index);
        auto file = std::make_unique<IO::File>(path, rcache_file_flags(false));
        file->resize(0, 0);
        files_.push_back(std::move(file));
        writer_.toc_offset = 0;
        writer_.end_offset = sizeof(RBUN::Footer);
        writer_.chunks.clear();
        writer_.buffer.clear();
        this->flush_internal();
    }
    writer_.chunks.push_back(chunk);
    rlib_assert(writer_.buffer.append(data));
    lookup_[chunk.chunkId] = {chunk, (BundleID)(files_.size() - 1), writer_.buffer.size() + writer_.toc_offset};
    auto const buffer_size = writer_.buffer.size();
    auto const current_toc_size = files_.back()->size() - writer_.toc_offset;
    if (buffer_size > current_toc_size && buffer_size - current_toc_size > options_.flush_size) {
        this->flush_internal();
    }
    writer_.end_offset += extra_data;
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

auto RCache::load_file_internal_read_only() -> void {
    fs::path path = options_.path;
    do {
        auto const index = files_.size();
        auto file = std::make_unique<IO::MMap>(path, rcache_file_flags(true));
        auto bundle = RBUN::read(*file);
        files_.push_back(std::move(file));
        for (auto& chunk : bundle.lookup) {
            chunk.second.bundleId = (BundleID)index;
        }
        lookup_.merge(std::move(bundle.lookup));
        path = rcache_file_path(options_.path, index + 1);
    } while (fs::exists(path));
}

auto RCache::load_file_internal_read_write() -> void {
    fs::path path = options_.path;
    do {
        auto const index = files_.size();
        auto next_path = rcache_file_path(options_.path, index + 1);
        if (!fs::exists(path) || (fs::file_size(path) < options_.max_size && !fs::exists(next_path))) {
            auto file = std::make_unique<IO::File>(path, rcache_file_flags(false));
            auto size = file->size();
            auto bundle = size ? RBUN::read(*file) : RBUN{};
            files_.push_back(std::move(file));
            for (auto& chunk : bundle.lookup) {
                chunk.second.bundleId = (BundleID)index;
            }
            lookup_.merge(std::move(bundle.lookup));
            writer_.toc_offset = bundle.toc_offset;
            writer_.end_offset = size;
            writer_.chunks = std::move(bundle.chunks);
            writer_.buffer.clear();
            can_write_ = true;
            this->flush_internal();
            break;
        }
        auto file = std::make_unique<IO::MMap>(path, rcache_file_flags(true));
        auto bundle = RBUN::read(*file);
        files_.push_back(std::move(file));
        for (auto& chunk : bundle.lookup) {
            chunk.second.bundleId = (BundleID)index;
        }
        lookup_.merge(std::move(bundle.lookup));
        path = std::move(next_path);
    } while (true);
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
        for (auto& [key, value] : bundle.lookup) {
            value.bundleId = bundle.bundleId;
        }
        lookup_.merge(std::move(bundle.lookup));
    }
}