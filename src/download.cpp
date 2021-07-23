#include "download.hpp"
#include "error.hpp"
#include <array>
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

/// Bundle download

bool BundleDownload::can_simplify() const noexcept {
    if (range_mode != RangeMode::Multi) {
        return false;
    }
    auto next = chunks.front().compressed_offset;
    for (auto const& chunk: chunks) {
        if (chunk.compressed_offset != next) {
            return false;
        }
        next = chunk.compressed_offset + chunk.compressed_size;
    }
    return true;
}

bool BundleDownload::max_range() const noexcept {
    return range_mode == RangeMode::Multi && range_multi.size() > 4000;
}

/// Bundle List

BundleDownloadList BundleDownloadList::from_file_info(FileInfo const& info, DownloadOpts const& opts) {
    auto chunks = info.chunks;
    std::sort(chunks.begin(), chunks.end(), [](FileChunk const& l, FileChunk const& r) {
        using wrap_t = std::tuple<BundleID, uint32_t, uint32_t>;
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
        rman_assert(i.id != ChunkID::None);
        rman_assert(i.bundle_id != BundleID::None);
        if (i.bundle_id != bundle_id || (bundle && bundle->max_range())) {
            bundle = bundles.emplace_back(std::make_unique<BundleDownload>()).get();
            bundle->range_mode = opts.range_mode;
            bundle->id = i.bundle_id;
            bundle->path = "/bundles/" + to_hex(i.bundle_id) + ".bundle";
            bundle_id = i.bundle_id;
            chunk_id = ChunkID::None;
        }
        if (i.id != chunk_id) {
            if (opts.range_mode == RangeMode::Multi) {
                if (!bundle->range_multi.empty()) {
                    bundle->range_multi += ", ";
                }
                bundle->range_multi += std::to_string(i.compressed_offset);
                bundle->range_multi += "-";
                bundle->range_multi += std::to_string(i.compressed_offset + i.compressed_size - 1);
            }
            bundle->total_size += (size_t)i.compressed_size;
            bundle->max_uncompressed = std::max(bundle->max_uncompressed,
                                                (size_t)i.uncompressed_size);
            chunk = &bundle->chunks.emplace_back(ChunkDownload{i, {}});
            chunk_id = i.id;
        }
        chunk->offsets.push_back(i.uncompressed_offset);
        bundle->offset_count++;
    }
    for (auto& bundle: bundles) {
        if (bundle->can_simplify()) { // simplify to one range
            bundle->range_mode = RangeMode::One;
        }
        auto const start = bundle->chunks.front().compressed_offset;
        auto const end = bundle->chunks.back().compressed_offset + bundle->chunks.back().compressed_size - 1;
        bundle->range_one = std::to_string(start) + "-" + std::to_string(end);
    }
    return BundleDownloadList { std::move(bundles), {}, {} };
}

/// Connection

HttpConnection::HttpConnection(DownloadOpts const& opts)
    : prefix_(opts.prefix), archive_(opts.archive) {
    handle_ = curl_easy_init();
    rman_assert(curl_easy_setopt(handle_, CURLOPT_VERBOSE, (opts.curl_verbose ? 1L : 0L)) == CURLE_OK);
    rman_assert(curl_easy_setopt(handle_, CURLOPT_NOPROGRESS, 1L) == CURLE_OK);
    rman_assert(curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, &write_data) == CURLE_OK);
    rman_assert(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this) == CURLE_OK);
    // curl_easy_setopt(handle_, CURLOPT_PRIVATE, (char*)this);
    if (opts.curl_buffer) {
        rman_assert(curl_easy_setopt(handle_, CURLOPT_BUFFERSIZE, opts.curl_buffer) == CURLE_OK);
    }
    if (!opts.curl_proxy.empty()) {
        rman_assert(curl_easy_setopt(handle_, CURLOPT_PROXY, opts.curl_proxy.c_str()) == CURLE_OK);
    }
    if (!opts.curl_useragent.empty()) {
        rman_assert(curl_easy_setopt(handle_, CURLOPT_USERAGENT, opts.curl_useragent.c_str()) == CURLE_OK);
    }
    if (opts.curl_cookiefile != "-") {
        rman_assert(curl_easy_setopt(handle_, CURLOPT_COOKIEFILE, opts.curl_cookiefile.c_str()) == CURLE_OK);
    }
    if (!opts.curl_cookielist.empty()) {
        rman_assert(curl_easy_setopt(handle_, CURLOPT_COOKIELIST, opts.curl_cookielist.c_str()) == CURLE_OK);
    }
    inbuffer_.reserve(1 * 1024 * 1024);
    outbuffer_.reserve(1 * 1024 * 1024);
}

HttpConnection::~HttpConnection() noexcept {
    if (handle_) {
        curl_easy_cleanup(handle_);
    }
}

static bool is_valid_bundle(std::string const& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    std::array<char, 4> magic = {};
    file.seekg(-4, std::ios::end);
    file.read(magic.data(), 4);
    return magic == std::array { 'R', 'B', 'U', 'N' };
}

void HttpConnection::give_bundle(std::unique_ptr<BundleDownload> bundle) {
    bundle_ = std::move(bundle);
    chunk_ = 0;
    inbuffer_.clear();
    outbuffer_.clear();
    inbuffer_.reserve(ZSTD_COMPRESSBOUND((size_t)(bundle_->max_uncompressed)));
    outbuffer_.reserve(bundle_->max_uncompressed);
    if (bundle_->chunks.size() > 1) {
        state_ = HttpState::RecvR0;
    } else {
        state_ = HttpState::RecvData;
    }
    range_pos_ = 0;
    range_mode_ = bundle_->range_mode;
    std::string target = prefix_ + bundle_->path;
    archivefile_ = nullptr;
    if (!archive_.empty()) {
        std::string backup = archive_ + bundle_->path;
        if (is_valid_bundle(backup)) {
            target = "file://" + backup;
            if (range_mode_ == RangeMode::Multi) {
                range_mode_ = RangeMode::One;
            }
        } else {
            archivefile_ = std::make_unique<std::ofstream>(backup, std::ios::binary);
            range_mode_ = RangeMode::Full;
        }
    }
    rman_assert(curl_easy_setopt(handle_, CURLOPT_URL, target.c_str()) == CURLE_OK);
    switch (range_mode_) {
    case RangeMode::Multi:
        rman_assert(curl_easy_setopt(handle_, CURLOPT_RANGE, bundle_->range_multi.c_str()) == CURLE_OK);
        break;
    case RangeMode::One:
        range_pos_ = bundle_->chunks.front().compressed_offset;
        rman_assert(curl_easy_setopt(handle_, CURLOPT_RANGE, bundle_->range_one.c_str()) == CURLE_OK);
        break;
    case RangeMode::Full:
        rman_assert(curl_easy_setopt(handle_, CURLOPT_RANGE, nullptr) == CURLE_OK);
        break;
    }
}

std::unique_ptr<BundleDownload> HttpConnection::take_bundle() {
    archivefile_ = nullptr;
    return std::move(bundle_);
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
    switch (range_mode_) {
    case RangeMode::Full:
        if (archivefile_) {
            archivefile_->write(data, end - data);
        }
    case RangeMode::One:
        return write_raw(data, end);
    case RangeMode::Multi:
        return write_http(data, end);
    }
    return false;
}

bool HttpConnection::write_raw(char const* data, char const* end) noexcept {
    while (data != end) {
        auto old_data = data;
        if (state_ == HttpState::Done) {
            data = end;
        } else {
            auto const& chunk = bundle_->chunks[chunk_];
            if (chunk.compressed_offset > range_pos_) {
                data += std::min(end - data, (ptrdiff_t)(chunk.compressed_offset - range_pos_));
            } else {
                data = receive(data, end - data);
                if (!data) {
                    state_ = HttpState::Done;
                    return false;
                }
            }
        }
        range_pos_ += (data - old_data);
    }
    return true;
}

bool HttpConnection::write_http(char const* data, char const* end) noexcept {
    while (data != end) {
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
    auto needed = total - (uint32_t)inbuffer_.size();
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
    if (outfile_) {
        for (auto offset: chunk.offsets) {
            outfile_->seekp(offset);
            outfile_->write(outbuffer_.data(), (std::streamsize)outbuffer_.size());
        }
    }
    return true;
}

/// Client

HttpClient::HttpClient(DownloadOpts const& opts) {
    struct CurlInit {
        CurlInit() {
            curl_global_init(CURL_GLOBAL_ALL);
        }
        ~CurlInit() {
            curl_global_cleanup();
        }
    };
    static auto init = CurlInit{};
    handle_ = curl_multi_init();
    connections_.resize(opts.connections);
    for (auto& connection: connections_) {
        connection = std::make_unique<HttpConnection>(opts);
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
            list.good.push_back(connection->take_bundle());
        } else {
            list.unfinished.push_back(connection->take_bundle());
        }
        free_.push_back(connection);
        rman_assert(curl_multi_remove_handle(handle_, handle) == CURLM_OK);
    }
}

void HttpClient::push(BundleDownloadList &list) {
    while (!free_.empty() && !list.queued.empty()) {
        auto connection = free_.back();
        free_.pop_back();
        connection->give_bundle(std::move(list.queued.back()));
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

