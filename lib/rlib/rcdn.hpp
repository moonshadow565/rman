#pragma once
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "common.hpp"
#include "rbundle.hpp"
#include "rcache.hpp"

namespace rlib {
    struct RCDN {
        struct Options {
            std::string url = {};
            bool verbose = {};
            long buffer = {};
            std::uint32_t workers = {};
            std::string proxy = {};
            std::string useragent = {};
            std::string cookiefile = {};
            std::string cookielist = {};
        };

        RCDN(Options const& options, RCache* cache_out);
        RCDN(RCDN const&) = delete;
        ~RCDN() noexcept;

        using done_cb = function_ref<void(RBUN::ChunkDst const& chunk, std::span<char const> data)>;
        using yield_cb = function_ref<void()>;

        auto run(std::vector<RBUN::ChunkDst> chunks, done_cb done, yield_cb yield, int delay = 100)
            -> std::vector<RBUN::ChunkDst>;

    private:
        struct Worker;
        void* handle_;
        Options options_;
        std::vector<std::unique_ptr<Worker>> workers_;
    };
}