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

        auto add(RBUN::Chunk const& chunk, std::span<char const> data) -> bool;

        auto contains(ChunkID chunkId) const noexcept -> bool;

        using done_cb = function_ref<void(RBUN::ChunkDst const& chunk, std::span<char const> data)>;
        using yield_cb = function_ref<void()>;
        auto run(std::vector<RBUN::ChunkDst> chunks, done_cb done, yield_cb yield) const -> std::vector<RBUN::ChunkDst>;

        auto flush() -> bool;

        auto can_write() const noexcept -> bool { return file_ && !options_.readonly; }

    private:
        IOFile file_;
        Options options_;
        std::vector<char> buffer_;
        RBUN bundle_;
    };
}