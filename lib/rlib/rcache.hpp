#pragma once
#include <cstddef>
#include <span>
#include <vector>

#include "common.hpp"
#include "iofile.hpp"
#include "rbundle.hpp"

namespace rlib {
    struct RCache {
        struct Options {
            std::string path;
            bool readonly;
            std::size_t flush_size;
            std::size_t max_size;
        };

        RCache(Options const& options);
        ~RCache();

        auto add(RChunk const& chunk, std::span<char const> data) -> bool;

        auto add_uncompressed(std::span<char const> data, int level) -> RChunk::Src;

        auto contains(ChunkID chunkId) const noexcept -> bool;

        auto find(ChunkID chunkId) const noexcept -> RChunk::Src;

        auto get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb read) const -> std::vector<RChunk::Dst>;

        auto flush() -> bool;

        auto can_write() const noexcept -> bool { return can_write_; }

    private:
        struct Writer {
            std::size_t toc_offset;
            std::size_t end_offset;
            std::vector<RChunk> chunks;
            std::vector<char> buffer;
        };
        bool can_write_ = {};
        Options options_ = {};
        Writer writer_ = {};
        std::vector<std::unique_ptr<IO::File>> files_;
        std::unordered_map<ChunkID, RChunk::Src> lookup_ = {};

        auto check_space(std::size_t extra) -> bool;
    };
}