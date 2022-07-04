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
            std::uint32_t flush_size;
        };

        RCache(Options const& options);
        ~RCache();

        auto add(RChunk const& chunk, std::span<char const> data) -> bool;

        auto add_uncompressed(std::span<char const> data, int level) -> RChunk::Src;

        auto contains(ChunkID chunkId) const noexcept -> bool;

        auto find(ChunkID chunkId) const noexcept -> RChunk::Src;

        auto uncache(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb read) const -> std::vector<RChunk::Dst>;

        auto flush() -> bool;

        auto can_write() const noexcept -> bool { return file_.fd() && !options_.readonly; }
    private:
        IO::File file_;
        Options options_;
        std::vector<char> buffer_;
        RBUN bundle_;
    };
}