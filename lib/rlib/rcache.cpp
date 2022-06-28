#include "rcache.hpp"

#include <common/xxhash.h>

#include <charconv>

#include "common.hpp"

using namespace rlib;

static IOFile::Flags make_flags(bool readonly) {
    auto flags = IOFile::Flags{};
    if (!readonly) {
        flags = (IOFile::Flags)(flags | IOFile::WRITE);
    }
    // flags = (IOFile::Flags)(flags | IOFile::RANDOM_ACCESS);
    return flags;
}

RCache::RCache(Options const& options) : file_(options.path, make_flags(options.readonly)), options_(options) {
    auto file_size = file_.size();
    if (file_size == 0 && !options.readonly) {
        flush();
        return;
    }
    bundle_ = RBUN::read(file_);
}

RCache::~RCache() { this->flush(); }

auto RCache::add(RChunk const& chunk, std::span<char const> data) -> bool {
    if (!can_write() || bundle_.lookup.contains(chunk.chunkId)) {
        return false;
    }
    if (chunk.chunkId == ChunkID::None) {
        return false;
    }
    bundle_.chunks.push_back(chunk);
    bundle_.lookup[chunk.chunkId] = {chunk, BundleID::None, buffer_.size() + bundle_.toc_offset};
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    if (buffer_.size() > options_.flush_size) {
        this->flush();
    }
    return true;
}

auto RCache::contains(ChunkID chunkId) const noexcept -> bool { return bundle_.lookup.contains(chunkId); }

auto RCache::uncache(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb on_data) const -> std::vector<RChunk::Dst> {
    sort_by<&RChunk::Dst::chunkId, &RChunk::Dst::uncompressed_offset>(chunks.begin(), chunks.end());
    auto lastId = ChunkID::None;
    auto dst = std::span<char const>{};
    remove_if(chunks, [&](RChunk::Dst const& chunk) mutable {
        if (chunk.chunkId == ChunkID::None) {
            return false;
        }
        if (chunk.chunkId == lastId) {
            on_data(chunk, dst);
            return true;
        }
        auto i = bundle_.lookup.find(chunk.chunkId);
        if (i == bundle_.lookup.end()) {
            return false;
        }
        auto const& c = i->second;
        rlib_assert(c.uncompressed_size == chunk.uncompressed_size);
        auto src = std::span(buffer_);
        if (c.compressed_offset > bundle_.toc_offset) {
            src = src.subspan(c.compressed_offset - bundle_.toc_offset, c.compressed_size);
        } else {
            src = file_.copy(c.compressed_offset, c.compressed_size);
        }
        dst = zstd_decompress(src, c.uncompressed_size);
        on_data(chunk, dst);
        lastId = chunk.chunkId;
        return true;
    });
    return std::move(chunks);
}

auto RCache::flush() -> bool {
    // Dont reflush when there is nothing to flush.
    if (!can_write() || (buffer_.empty() && bundle_.toc_offset != 0)) {
        return false;
    }
    auto toc_size = sizeof(RChunk) * bundle_.chunks.size();
    RBUN::Footer footer = {
        .checksum = std::bit_cast<std::array<char, 8>>(XXH64((char const*)bundle_.chunks.data(), toc_size, 0)),
        .entry_count = (std::uint32_t)bundle_.chunks.size(),
        .version = RBUN::Footer::VERSION,
        .magic = {'R', 'B', 'U', 'N'},
    };
    auto new_toc_offset = bundle_.toc_offset + buffer_.size();
    buffer_.insert(buffer_.end(), (char const*)bundle_.chunks.data(), (char const*)bundle_.chunks.data() + toc_size);
    buffer_.insert(buffer_.end(), (char const*)&footer, (char const*)&footer + sizeof(footer));
    rlib_assert(file_.write(bundle_.toc_offset, buffer_, true));
    buffer_.clear();
    bundle_.toc_offset = new_toc_offset;
    return true;
}