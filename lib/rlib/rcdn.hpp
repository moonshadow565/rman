#pragma once
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "common.hpp"
#include "rcache.hpp"
#include "rchunk.hpp"

namespace rlib {
    struct RCDN {
        struct Options {
            std::string url = {};
            bool verbose = {};
            long buffer = {};
            int interval = {};
            std::uint32_t retry = {};
            std::uint32_t workers = {};
            std::string proxy = {};
            std::string useragent = {};
            std::string cookiefile = {};
            std::string cookielist = {};
            std::size_t low_speed_limit = 64 * KiB;
            std::size_t low_speed_time = 0;
        };

        RCDN(Options const& options, RCache* cache_out);
        RCDN(RCDN const&) = delete;
        ~RCDN() noexcept;

        auto get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb on_good) -> std::vector<RChunk::Dst>;

        auto get_into(RChunk::Src const& src, std::span<char> dst) -> bool;

    private:
        struct Worker;
        Options options_;
        RCache* cache_;
        mutable void* handle_;
        mutable std::vector<std::unique_ptr<Worker>> workers_;
    };
}