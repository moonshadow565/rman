#include "rcdn.hpp"

#include <curl/curl.h>
#include <zstd.h>

#include <cstdlib>
#include <cstring>
#include <map>

#include "buffer.hpp"
#include "common.hpp"

using namespace rlib;

struct CurlInit {
    CurlInit() noexcept { curl_global_init(CURL_GLOBAL_ALL); }
    ~CurlInit() noexcept { curl_global_cleanup(); }
};

struct RCDN::Worker final {
    Worker(RCDN const* cdn) : cdn_(cdn), handle_(curl_easy_init()) {
        auto& options = cdn_->options_;
        rlib_assert(handle_);
        rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_PRIVATE, (char*)this));
        rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_VERBOSE, (options.verbose ? 1L : 0L)));
        rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_NOPROGRESS, 1L));
        rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, &recv_data));
        rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this));
        if (options.buffer) {
            rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_BUFFERSIZE, options.buffer));
        }
        if (!options.proxy.empty()) {
            rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_PROXY, options.proxy.c_str()));
        }
        if (!options.useragent.empty()) {
            rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_USERAGENT, options.useragent.c_str()));
        }
        if (options.cookiefile != "-") {
            rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_COOKIEFILE, options.cookiefile.c_str()));
        }
        if (!options.cookielist.empty()) {
            rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_COOKIELIST, options.cookielist.c_str()));
        }
    }

    ~Worker() noexcept {
        if (handle_) {
            curl_easy_cleanup(handle_);
        }
    }

    auto start(std::span<RChunk::Dst const>& chunks_queue, RChunk::Dst::data_cb on_data) -> void* {
        auto chunks = find_chunks(chunks_queue);
        auto const& front = chunks.front();
        auto const& back = chunks.back();
        auto range = fmt::format("{}-{}", front.compressed_offset, back.compressed_offset + back.compressed_size - 1);
        auto url = fmt::format("{}/bundles/{}.bundle", cdn_->options_.url, front.bundleId);
        rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_URL, url.c_str()));
        rlib_assert_easy_curl(curl_easy_setopt(handle_, CURLOPT_RANGE, range.c_str()));
        buffer_.clear();
        chunks_ = chunks;
        on_data_ = on_data;
        chunks_queue = chunks_queue.subspan(chunks.size());
        return handle_;
    }

    auto finish(std::vector<RChunk::Dst>& chunks_failed) -> void* {
        chunks_failed.insert(chunks_failed.end(), chunks_.begin(), chunks_.end());
        buffer_.clear();
        chunks_ = {};
        on_data_ = {};
        return handle_;
    }

private:
    RCDN const* cdn_;
    void* handle_;
    Buffer buffer_;
    std::span<RChunk::Dst const> chunks_;
    RChunk::Dst::data_cb on_data_;

    auto recieve(std::span<char const> recv) -> bool {
        while (!recv.empty()) {
            if (chunks_.empty()) {
                return false;
            }
            auto chunk = chunks_.front();
            if (buffer_.empty() && recv.size() >= chunk.compressed_size) {
                if (!decompress(chunk, recv)) {
                    return false;
                }
                recv = recv.subspan(chunk.compressed_size);
            } else if (buffer_.size() + recv.size() >= chunk.compressed_size) {
                auto nsize = chunk.compressed_size - buffer_.size();
                if (!buffer_.append(recv.subspan(0, nsize))) {
                    return false;
                }
                if (!decompress(chunk, buffer_)) {
                    return false;
                }
                buffer_.clear();
                recv = recv.subspan(nsize);
            } else {
                if (!buffer_.append(recv)) {
                    return false;
                }
                recv = recv.subspan(recv.size());
            }
        }
        return true;
    }

    auto decompress(RChunk::Dst const& chunk, std::span<char const> src) -> bool {
        rlib_trace("BundleID: %016llx, ChunkID: %016llx\n", chunk.bundleId, chunk.chunkId);
        src = src.subspan(0, chunk.compressed_size);
        auto dst = zstd_decompress(src, chunk.uncompressed_size);
        if (cdn_->cache_ && cdn_->cache_->can_write()) {
            cdn_->cache_->add(chunk, src);
        }
        while (!chunks_.empty() && chunks_.front().chunkId == chunk.chunkId) {
            on_data_(chunks_.front(), dst);
            chunks_ = chunks_.subspan(1);
        }
        return true;
    }

    static auto find_chunks(std::span<RChunk::Dst const> chunks) noexcept -> std::span<RChunk::Dst const> {
        std::size_t i = 1;
        for (; i != chunks.size(); ++i) {
            // 1. Consecutive chunks must be present in same bundle
            if (chunks[i].bundleId != chunks[0].bundleId) {
                break;
            }
            // 2. Chunks with same id don't follow 3. but are allowed.
            if (chunks[i].chunkId == chunks[i - 1].chunkId) {
                continue;
            }
            // 3. Consecutive chunks should be contigous in memory.
            if (chunks[i].compressed_offset != chunks[i - 1].compressed_offset + chunks[i - 1].compressed_size) {
                break;
            }
        }
        return chunks.subspan(0, i);
    }

    static auto recv_data(char const* data, size_t size, size_t ncount, Worker* self) noexcept -> size_t {
        if (self->recieve({data, size * ncount})) {
            return size * ncount;
        }
        return 0;
    }
};

RCDN::RCDN(Options const& options, RCache* cache) : options_(options), cache_(cache), handle_(nullptr) {
    static auto init = CurlInit{};
}

RCDN::~RCDN() noexcept {
    if (handle_) {
        curl_multi_cleanup(handle_);
    }
}

auto RCDN::get(std::vector<RChunk::Dst> chunks, RChunk::Dst::data_cb on_data) -> std::vector<RChunk::Dst> {
    if (cache_) {
        chunks = cache_->get(std::move(chunks), on_data);
        if (chunks.empty()) {
            return std::move(chunks);
        }
    }

    if (options_.url.empty()) {
        return std::move(chunks);
    }

    if (!handle_) {
        handle_ = curl_multi_init();
        rlib_assert(handle_);
        for (std::uint32_t i = std::clamp(options_.workers, 1u, 64u); i; --i) {
            workers_.push_back(std::make_unique<Worker>(this));
        }
    }

    for (std::uint32_t retry = options_.retry; !chunks.empty() && retry; --retry) {
        sort_by<&RChunk::Dst::bundleId, &RChunk::Dst::compressed_offset, &RChunk::Dst::uncompressed_offset>(
            chunks.begin(),
            chunks.end());

        auto chunks_failed = std::vector<RChunk::Dst>{};
        chunks_failed.reserve(chunks.size());
        auto chunks_queue = std::span<RChunk::Dst const>(chunks);
        auto workers_free = std::vector<Worker*>{};
        for (auto const& worker : workers_) {
            workers_free.push_back(worker.get());
        }

        for (std::size_t workers_running = 0;;) {
            // Start new downloads
            while (!workers_free.empty() && !chunks_queue.empty()) {
                auto worker = workers_free.back();
                auto handle = worker->start(chunks_queue, on_data);
                workers_free.pop_back();
                ++workers_running;
                rlib_assert_multi_curl(curl_multi_add_handle(handle_, handle));
            }

            // Return if we ended.
            if (!workers_running) {
                break;
            }

            // Perform any actual work.
            // NOTE: i do not trust still_running out variable, do our own bookkeeping instead.
            int still_running = 0;
            rlib_assert_multi_curl(curl_multi_perform(handle_, &still_running));

            // Process messages.
            for (int msg_left = 0; auto msg = curl_multi_info_read(handle_, &msg_left);) {
                if (msg->msg != CURLMSG_DONE || msg->easy_handle == nullptr) {
                    continue;
                }
                auto worker = (Worker*)nullptr;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &worker);
                rlib_assert(worker);
                auto handle = worker->finish(chunks_failed);
                workers_free.push_back(worker);
                --workers_running;
                rlib_assert_multi_curl(curl_multi_remove_handle(handle_, handle));
            }

            // Block untill end.
            rlib_assert_multi_curl(curl_multi_wait(handle_, nullptr, 0, options_.interval, nullptr));
        }

        chunks = std::move(chunks_failed);
    }
    return chunks;
}