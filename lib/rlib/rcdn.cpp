#include "rcdn.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <cstring>
#include <map>

#include "error.hpp"

using namespace rlib;

struct CurlInit {
    CurlInit() noexcept { curl_global_init(CURL_GLOBAL_ALL); }
    ~CurlInit() noexcept { curl_global_cleanup(); }
};

struct RCDN::Worker {
    Worker(Options const& options, RCache* cache_out) {
        handle_ = curl_easy_init();
        url_ = options.url;
        cache_out_ = cache_out;
        rlib_assert(handle_);
        rlib_assert(curl_easy_setopt(handle_, CURLOPT_PRIVATE, (char*)this) == CURLE_OK);
        rlib_assert(curl_easy_setopt(handle_, CURLOPT_VERBOSE, (options.verbose ? 1L : 0L)) == CURLE_OK);
        rlib_assert(curl_easy_setopt(handle_, CURLOPT_NOPROGRESS, 1L) == CURLE_OK);
        rlib_assert(curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, &recv_data) == CURLE_OK);
        rlib_assert(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this) == CURLE_OK);
        if (options.buffer) {
            rlib_assert(curl_easy_setopt(handle_, CURLOPT_BUFFERSIZE, options.buffer) == CURLE_OK);
        }
        if (!options.proxy.empty()) {
            rlib_assert(curl_easy_setopt(handle_, CURLOPT_PROXY, options.proxy.c_str()) == CURLE_OK);
        }
        if (!options.useragent.empty()) {
            rlib_assert(curl_easy_setopt(handle_, CURLOPT_USERAGENT, options.useragent.c_str()) == CURLE_OK);
        }
        if (options.cookiefile != "-") {
            rlib_assert(curl_easy_setopt(handle_, CURLOPT_COOKIEFILE, options.cookiefile.c_str()) == CURLE_OK);
        }
        if (!options.cookielist.empty()) {
            rlib_assert(curl_easy_setopt(handle_, CURLOPT_COOKIELIST, options.cookielist.c_str()) == CURLE_OK);
        }
    }

    ~Worker() noexcept {
        if (handle_) {
            curl_easy_cleanup(handle_);
        }
    }

    auto start(std::span<RBUN::ChunkDst const>& chunks_queue, done_cb done) -> void* {
        auto chunks = find_chunks(chunks_queue);
        auto start = std::to_string(chunks.front().compressed_offset);
        auto end = std::to_string(chunks.back().compressed_offset + chunks.back().compressed_size - 1);
        auto range = start + "-" + end;
        auto url = url_ + "/bundles/" + to_hex(chunks.front().bundleId) + ".bundle";
        rlib_assert(curl_easy_setopt(handle_, CURLOPT_URL, url.c_str()) == CURLE_OK);
        rlib_assert(curl_easy_setopt(handle_, CURLOPT_RANGE, range.c_str()) == CURLE_OK);
        buffer_.clear();
        chunks_ = chunks;
        done_ = done;
        chunks_queue = chunks_queue.subspan(chunks.size());
        return handle_;
    }

    auto finish(std::vector<RBUN::ChunkDst>& chunks_failed) -> void* {
        chunks_failed.insert(chunks_failed.end(), chunks_.begin(), chunks_.end());
        buffer_.clear();
        chunks_ = {};
        done_ = {};
        return handle_;
    }

private:
    void* handle_;
    std::string url_;
    std::vector<char> buffer_;
    std::span<RBUN::ChunkDst const> chunks_;
    done_cb done_;
    RCache* cache_out_;

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
                buffer_.insert(buffer_.end(), recv.data(), recv.data() + nsize);
                if (!decompress(chunk, buffer_)) {
                    return false;
                }
                buffer_.clear();
                recv = recv.subspan(nsize);
            } else {
                buffer_.insert(buffer_.end(), recv.data(), recv.data() + recv.size());
                recv = recv.subspan(recv.size());
            }
        }
        return true;
    }

    auto decompress(RBUN::ChunkDst const& chunk, std::span<char const> src) -> bool {
        src = src.subspan(0, chunk.compressed_size);
        auto dst = try_zstd_decompress(src, chunk.uncompressed_size);
        if (dst.size() != chunk.uncompressed_size) {
            return false;
        }
        if (chunk.hash(dst, chunk.hash_type) != chunk.chunkId) {
            return false;
        }
        if (cache_out_) {
            cache_out_->add(chunk, src);
        }
        while (!chunks_.empty() && chunks_.front().chunkId == chunk.chunkId) {
            done_(chunks_.front(), dst);
            chunks_ = chunks_.subspan(1);
        }
        return true;
    }

    static auto recv_data(char const* data, size_t size, size_t ncount, Worker* self) noexcept -> size_t {
        if (self->recieve({data, size * ncount})) {
            return size * ncount;
        }
        return 0;
    }

    static auto find_chunks(std::span<RBUN::ChunkDst const> chunks) noexcept -> std::span<RBUN::ChunkDst const> {
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
};

RCDN::RCDN(Options const& options, RCache* cache_out) {
    if (cache_out && !cache_out->can_write()) {
        cache_out = nullptr;
    }
    static auto init = CurlInit{};
    handle_ = curl_multi_init();
    rlib_assert(handle_);
    for (std::uint32_t i = std::clamp(options.workers, 1u, 64u); i; --i) {
        workers_.push_back(std::make_unique<Worker>(options, cache_out));
    }
}

RCDN::~RCDN() noexcept {
    if (handle_) {
        curl_multi_cleanup(handle_);
    }
}

auto RCDN::run(std::vector<RBUN::ChunkDst> chunks, done_cb callback, yield_cb yield, int delay)
    -> std::vector<RBUN::ChunkDst> {
    sort_by<&RBUN::ChunkDst::bundleId, &RBUN::ChunkDst::compressed_offset, &RBUN::ChunkDst::uncompressed_offset>(
        chunks.begin(),
        chunks.end());

    auto chunks_failed = std::vector<RBUN::ChunkDst>{};
    auto chunks_queue = std::span<RBUN::ChunkDst const>(chunks);
    auto workers_free = std::vector<Worker*>{};
    for (auto const& worker : workers_) {
        workers_free.push_back(worker.get());
    }

    for (std::size_t workers_running = 0;;) {
        if (yield) yield();

        // Start new downloads
        while (!workers_free.empty() && !chunks_queue.empty()) {
            auto worker = workers_free.back();
            auto handle = worker->start(chunks_queue, callback);
            workers_free.pop_back();
            ++workers_running;
            rlib_assert(curl_multi_add_handle(handle_, handle) == CURLM_OK);
        }

        // Return if we ended.
        if (!workers_running) {
            break;
        }

        // Perform any actual work.
        // NOTE: i do not trust still_running out variable, do our own bookkeeping instead.
        int still_running = 0;
        rlib_assert(curl_multi_perform(handle_, &still_running) == CURLM_OK);

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
            rlib_assert(curl_multi_remove_handle(handle_, handle) == CURLM_OK);
        }

        // Block untill end.
        rlib_assert(curl_multi_wait(handle_, nullptr, 0, delay, nullptr) == CURLM_OK);
    }

    return chunks_failed;
}
