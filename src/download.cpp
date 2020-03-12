#include "download.hpp"
#include "error.hpp"
#include <iterator>
#include <iostream>
#include <zstd.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <curl/curl.h>
using namespace rman;
namespace fs = std::filesystem;

/// Bundle List

BundleDownloadList BundleDownloadList::from_file_info(FileInfo const& info) noexcept {
    auto chunks = info.chunks;
    std::sort(chunks.begin(), chunks.end(), [](FileChunk const& l, FileChunk const& r) {
        using wrap_t = std::tuple<BundleID, int32_t, int32_t>;
        auto left = wrap_t{l.bundle_id, l.compressed_offset, l.uncompressed_offset};
        auto right = wrap_t{r.bundle_id, r.compressed_offset, r.uncompressed_offset};
        return left < right;
    });
    auto bundles = std::vector<std::unique_ptr<BundleDownload>>{};
    BundleDownload* bundle = {};
    auto bundle_id = BundleID::None;
    ChunkDownload* chunk = {};
    auto chunk_id = ChunkID::None;
    for(auto const& i: chunks) {
        if (i.bundle_id != bundle_id) {
            bundle = bundles.emplace_back(std::make_unique<BundleDownload>()).get();
            bundle->id = i.bundle_id;
            bundle->path = "/bundles/" + to_hex(i.bundle_id) + ".bundle";
            bundle_id = i.bundle_id;
            chunk_id = ChunkID::None;
        }
        if (i.id != chunk_id) {
            if (!bundle->range.empty()) {
                bundle->range += ", ";
            }
            bundle->range += std::to_string(i.compressed_offset);
            bundle->range += "-";
            bundle->range += std::to_string(i.compressed_offset + i.compressed_size - 1);
            bundle->total_size += (size_t)i.compressed_size;
            bundle->max_uncompressed = std::max(bundle->max_uncompressed,
                                                (size_t)i.uncompressed_size);
            chunk = &bundle->chunks.emplace_back(ChunkDownload{i, {}});
            chunk_id = i.id;
        }
        chunk->offsets.push_back(i.uncompressed_offset);
        bundle->offset_count++;
    }
    return BundleDownloadList { std::move(bundles), {}, {} };
}

/// Connection

HttpConnection::HttpConnection(std::string url, bool verbose) : prefix_(std::move(url)) {
    handle_ = curl_easy_init();
    rman_assert(curl_easy_setopt(handle_, CURLOPT_VERBOSE, (verbose ? 1L : 0L)) == CURLE_OK);
    rman_assert(curl_easy_setopt(handle_, CURLOPT_NOPROGRESS, 1L) == CURLE_OK);
    rman_assert(curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, &write_data) == CURLE_OK);
    rman_assert(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this) == CURLE_OK);
    // curl_easy_setopt(handle_, CURLOPT_PRIVATE, (char*)this);
}

HttpConnection::~HttpConnection() noexcept {
    if (handle_) {
        curl_easy_cleanup(handle_);
    }
}

void HttpConnection::set_bundle(std::unique_ptr<BundleDownload> bundle) {
    bundle_ = std::move(bundle);
    chunk_ = 0;
    inbuffer_.clear();
    if (bundle_->chunks.size() > 1) {
        state_ = HttpState::RecvR0;
    } else {
        state_ = HttpState::RecvData;
    }
    std::string target = prefix_ + bundle_->path;
    rman_assert(curl_easy_setopt(handle_, CURLOPT_URL, target.c_str()) == CURLE_OK);
    rman_assert(curl_easy_setopt(handle_, CURLOPT_RANGE, bundle_->range.c_str()) == CURLE_OK);
}

size_t HttpConnection::write_data(const char *p, size_t s, size_t n, HttpConnection *c) noexcept {
    size_t size = s * n;
    if (c->write(p, p + size)) {
        return size;
    } else {
        return 0;
    }
}

bool HttpConnection::write(char const* data, char const* end) noexcept {
    while(data != end) {
        switch(state_) {
        case HttpState::Done:
            return false;
        case HttpState::RecvR0:
            if (*data == '\r') {
                state_ = HttpState::RecvN0;
            }
            data++;
            continue;
        case HttpState::RecvN0:
            if (*data == '\n') {
                state_ = HttpState::RecvR1;
            } else {
                state_ = HttpState::RecvR0;
            }
            data++;
            continue;
        case HttpState::RecvR1:
            if (*data == '\r') {
                state_ = HttpState::RecvN1;
            } else {
                state_ = HttpState::RecvR0;
            }
            data++;
            continue;
        case HttpState::RecvN1:
            if (*data == '\n') {
                state_ = HttpState::RecvData;
            } else {
                state_ = HttpState::RecvR0;
            }
            data++;
            continue;
        case HttpState::RecvData:
            data = receive(data, end - data);
            if (!data) {
                state_ = HttpState::Done;
                return false;
            }
            continue;
        }
    }
    return true;
}

char const* HttpConnection::receive(const char *data, ptrdiff_t size) noexcept {
    auto total = bundle_->chunks[chunk_].compressed_size;
    auto needed = total - (int32_t)inbuffer_.size();
    if (needed == total && size >= total) {
        // Nothing in buffer and received data is enough to decompress
        if (!decompress(data)) {
            return nullptr;
        }
        chunk_++;
        state_ = HttpState::RecvR0;
        data += total;
    } else if (size >= needed) {
        // Enough data to fill the buffer
        inbuffer_.insert(inbuffer_.end(), data, data + needed);
        if (!decompress(inbuffer_.data())) {
            return nullptr;
        }
        inbuffer_.clear();
        chunk_++;
        state_ = HttpState::RecvR0;
        data += needed;
    } else {
        // Copy data into buffer for latter
        inbuffer_.insert(inbuffer_.end(), data, data + size);
        data += size;
    }
    if (chunk_ == bundle_->chunks.size()) {
        state_ = HttpState::Done;
    }
    return data;
}

bool HttpConnection::decompress(const char *data) const noexcept {
    auto const& chunk = bundle_->chunks[chunk_];
    outbuffer_.clear();
    outbuffer_.resize((size_t)chunk.uncompressed_size);
    auto result = ZSTD_decompress(outbuffer_.data(), outbuffer_.size(),
                                  data, (size_t)chunk.compressed_size);
    if (ZSTD_isError(result) || result != outbuffer_.size()) {
        return false;
    }
    for (auto offset: chunk.offsets) {
        outfile_->seekp(offset);
        outfile_->write(outbuffer_.data(), (std::streamsize)outbuffer_.size());
    }
    return true;
}

/// Client

HttpClient::HttpClient(std::string url, bool verbose, size_t connections) {
    struct CurlInit {
        CurlInit() {
            curl_global_init(CURL_GLOBAL_ALL);
        }
        ~CurlInit() {
            curl_global_cleanup();
        }
    };
    static auto init = CurlInit{};
    while (url.size() && url.back() == '/') {
        url.pop_back();
    }
    handle_ = curl_multi_init();
    connections_.resize(connections);
    for (auto& connection: connections_) {
        connection = std::unique_ptr<HttpConnection>(new HttpConnection(url, verbose));
        free_.push_back(connection.get());
    }
}

HttpClient::~HttpClient() noexcept {
    if (handle_) {
        for(auto const& kvp: inprogress_) {
            curl_multi_remove_handle(handle_, kvp.first);
        }
        curl_multi_cleanup(handle_);
    }
}

void HttpClient::set_outfile(std::ofstream *file) noexcept {
    for(auto& con: connections_) {
        con->set_file(file);
    }
}

void HttpClient::pop(BundleDownloadList &list) {
    CURLMsg* msg = nullptr;
    int msg_left = 0;
    while ((msg = curl_multi_info_read(handle_, &msg_left))) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }
        auto handle = msg->easy_handle;
        auto connection = inprogress_[handle];
        inprogress_.erase(handle);
        rman_assert(connection != nullptr);
        if (connection->is_done()) {
            list.good.push_back(connection->get_bundle());
        } else {
            list.unfinished.push_back(connection->get_bundle());
        }
        free_.push_back(connection);
        rman_assert(curl_multi_remove_handle(handle_, handle) == CURLM_OK);
    }
}

void HttpClient::push(BundleDownloadList &list) {
    while (!free_.empty() && !list.queued.empty()) {
        auto connection = free_.back();
        free_.pop_back();
        connection->set_bundle(std::move(list.queued.back()));
        list.queued.pop_back();
        auto handle = connection->get_handle();
        inprogress_[handle] = connection;
        rman_assert(curl_multi_add_handle(handle_, handle) == CURLM_OK);
    }
}

void HttpClient::perform() {
    int still_running = 0;
    rman_assert(curl_multi_perform(handle_, &still_running) == CURLM_OK);
}

void HttpClient::poll(int timeout) {
    int numfds = 0;
    rman_assert(curl_multi_wait(handle_, nullptr, 0, timeout, &numfds) == CURLM_OK);
}
