#include "rcache.hpp"

#include <common/xxhash.h>

#include <charconv>

#include "common.hpp"
#include "error.hpp"

using namespace rlib;

RCache::RCache(Options const& options) : file_(options.path, !options.readonly), options_(options) {
    auto file_size = file_.size();
    if (file_size == 0 && !options.readonly) {
        flush();
        return;
    }
    bundle_ = RBUN::read(file_);
}
RCache::~RCache() { this->flush(); }

auto RCache::add(RBUN::Chunk const& chunk, std::span<char const> data) -> bool {
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

auto RCache::run(std::vector<RBUN::ChunkDst> chunks, done_cb done, yield_cb yield) const
    -> std::vector<RBUN::ChunkDst> {
    sort_by<&RBUN::ChunkDst::chunkId, &RBUN::ChunkDst::uncompressed_offset>(chunks.begin(), chunks.end());
    auto lastId = ChunkID::None;
    auto dst = std::span<char const>{};
    remove_if(chunks, [&](RBUN::ChunkDst const& chunk) mutable {
        if (chunk.chunkId == ChunkID::None) {
            return false;
        }
        if (chunk.chunkId == lastId) {
            done(chunk, dst);
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
        done(chunk, dst);
        if (yield) yield();
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
    auto toc_size = sizeof(RBUN::Chunk) * bundle_.chunks.size();
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