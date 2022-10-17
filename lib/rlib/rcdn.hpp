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
        };

        RCDN(Options const& options, RCache* cache_out);
        RCDN(RCDN const&) = delete;
        ~RCDN() noexcept;

        auto get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb on_good) -> std::vector<RChunk::Dst>;

    private:
        struct Worker;
        void* handle_;
        Options options_;
        RCache* cache_out_;
        std::vector<std::unique_ptr<Worker>> workers_;
    };
}