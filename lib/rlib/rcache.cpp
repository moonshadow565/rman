#include "rcache.hpp"

#include <common/xxhash.h>
#include <zstd.h>

#include <charconv>

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
        options_.flush_size = std::max(32 * MiB, options_.flush_size);
        options_.max_size = std::max(options_.flush_size * 2, options_.max_size) - options_.flush_size;
    }
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
            writer_.buffer.reserve(options_.flush_size * 2);
            can_write_ = true;
            if (is_empty) {
                this->flush();
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

RCache::~RCache() { this->flush(); }

auto RCache::add(RChunk const& chunk, std::span<char const> data) -> bool {
    rlib_assert(chunk.compressed_size == data.size());
    if (!can_write() || lookup_.contains(chunk.chunkId)) {
        return false;
    }
    if (chunk.chunkId == ChunkID::None) {
        return false;
    }

    // check if we hit chunk limit
    auto const extra_data = sizeof(RChunk) + data.size();
    this->check_space(extra_data);

    writer_.chunks.push_back(chunk);
    lookup_[chunk.chunkId] = {chunk, BundleID::None, writer_.buffer.size() + writer_.toc_offset};
    writer_.buffer.insert(writer_.buffer.end(), data.begin(), data.end());
    if (writer_.buffer.size() > options_.flush_size) {
        this->flush();
    }
    writer_.end_offset += extra_data;

    return true;
}

auto RCache::add_uncompressed(std::span<char const> src, int level) -> RChunk::Src {
    auto id = RChunk::hash(src, HashType::RITO_HKDF);
    auto chunk = this->find(id);
    if (chunk.chunkId != ChunkID::None) {
        rlib_assert(chunk.uncompressed_size == src.size());
        return chunk;
    }
    auto dst = std::vector<char>(ZSTD_compressBound(src.size()));
    auto res = rlib_assert_zstd(ZSTD_compress(dst.data(), dst.size(), src.data(), src.size(), level));
    dst.resize(res);
    chunk.chunkId = id;
    chunk.uncompressed_size = src.size();
    chunk.compressed_size = dst.size();
    rlib_assert(this->add(chunk, dst));
    return chunk;
}

auto RCache::contains(ChunkID chunkId) const noexcept -> bool { return lookup_.contains(chunkId); }

auto RCache::find(ChunkID chunkId) const noexcept -> RChunk::Src {
    auto i = lookup_.find(chunkId);
    if (i == lookup_.end()) {
        return {};
    }
    return i->second;
}

auto RCache::uncache(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb on_data) const -> std::vector<RChunk::Dst> {
    auto found = std::vector<RChunk::Dst>{};
    found.reserve(chunks.size());
    remove_if(chunks, [&](RChunk::Dst& chunk) mutable {
        if (chunk.chunkId == ChunkID::None) {
            return false;
        }
        auto c = this->find(chunk.chunkId);
        if (c.chunkId == ChunkID::None) {
            return false;
        }
        rlib_assert(c.uncompressed_size == chunk.uncompressed_size);
        chunk.compressed_offset = c.compressed_offset;
        chunk.compressed_size = c.compressed_size;
        chunk.bundleId = c.bundleId;
        found.push_back(chunk);
        return true;
    });

    sort_by<&RChunk::Dst::compressed_offset, &RChunk::Dst::uncompressed_offset>(found.begin(), found.end());

    auto lastId = ChunkID::None;
    auto dst = std::span<char const>{};
    for (RChunk::Dst const& chunk : found) {
        if (chunk.chunkId == lastId) {
            on_data(chunk, dst);
            continue;
        }
        auto src = std::span<char const>{};
        auto const& file = files_.at((std::size_t)chunk.bundleId);
        if (can_write() && &file == &files_.back() && chunk.compressed_offset > writer_.toc_offset) {
            src = src.subspan(chunk.compressed_offset - writer_.toc_offset, chunk.compressed_size);
        } else {
            src = file->copy(chunk.compressed_offset, chunk.compressed_size);
        }
        dst = zstd_decompress(src, chunk.uncompressed_size);
        on_data(chunk, dst);
        lastId = chunk.chunkId;
    }
    return std::move(chunks);
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
    this->flush();  // flush anything that we have atm
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
    this->flush();
    return true;
}

auto RCache::flush() -> bool {
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
    writer_.buffer.insert(writer_.buffer.end(),
                          (char const*)writer_.chunks.data(),
                          (char const*)writer_.chunks.data() + toc_size);
    writer_.buffer.insert(writer_.buffer.end(), (char const*)&footer, (char const*)&footer + sizeof(footer));
    rlib_assert(files_.back()->write(writer_.toc_offset, writer_.buffer));
    writer_.buffer.clear();
    writer_.toc_offset = new_toc_offset;
    return true;
}