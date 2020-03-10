#include "download.hpp"
#include "error.hpp"
#include <iterator>
#include <future>
#include <thread>
using namespace rman;
namespace fs = std::filesystem;
#include <zstd.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifdef FCKRMAN_CURL
#include <curl/curl.h>
void HttpClientHandleDeleter::operator()(void *handle) const noexcept {
    curl_easy_cleanup(handle);
}

static size_t write_data(void *ptr, size_t size, size_t nmemb, std::vector<char> *buffer) noexcept {
    buffer->insert(buffer->end(), (char const*)ptr, (char const*)ptr + size * nmemb);
    return size * nmemb;
}

HttpClient::HttpClient(std::string url) {
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
    prefix = url;
    buffer = std::make_unique<std::vector<char>>();
    handle = std::unique_ptr<void, HttpClientHandleDeleter>(curl_easy_init());
    curl_easy_setopt(handle.get(), CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_data);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, buffer.get());
}

bool HttpClient::download(std::string const& path, std::string const& range) noexcept {
    auto target = prefix + path;
    curl_easy_setopt(handle.get(), CURLOPT_URL, target.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_RANGE, range.c_str());
    curl_easy_perform(handle.get());
    return true;
}
#else
#include <httplib.h>
void HttpClientHandleDeleter::operator()(void *handle) const noexcept {
    if (handle) {
        auto client = (httplib::Client*)handle;
        delete client;
    }
}

HttpClient::HttpClient(std::string url) {
    if (auto http = url.find("http://"); http == 0) {
        url = url.substr(7, url.size() - 7);
    } else if (auto https = url.find("https://"); https == 0) {
        url = url.substr(8, url.size() - 8);
    } else {
        throw_error(__func__, "Url must start with either http:// or https://");
    }
    while (url.size() && url.back() == '/') {
        url.pop_back();
    }
    if (auto extra = url.find('/'); extra != std::string::npos) {
        prefix = url.substr(extra, url.size() - extra);
        url = url.substr(0, extra);
    }
    int port = 80;
    if (auto colon = url.find(':'); colon != std::string::npos) {
        port = std::stoi(url.substr(colon + 1, url.size() - colon - 1));
        rman_assert(port > 0 && port < 65536);
        url = url.substr(0, colon);
    }
    rman_assert(url.size());
    buffer = std::make_unique<std::vector<char>>();
    handle = std::unique_ptr<void, HttpClientHandleDeleter>(new httplib::Client(url, port));
}

bool HttpClient::download(std::string const& path, std::string const& range) noexcept {
    auto target = prefix + path;
    auto client = (httplib::Client*)handle.get();
    auto inbuffer = buffer.get();
    auto result = client->Get(target.c_str(), {{"Range", "bytes=" + range}},
                              [inbuffer](char const* data, size_t size) -> bool {
        inbuffer->insert(inbuffer->end(), data, data + size);
        return true;
    });
    return true;
}
#endif

bool BundleDownload::download(HttpClient& client, std::ofstream &file) const noexcept {
    auto& inbuffer = *client.buffer;
    inbuffer.clear();
    inbuffer.reserve(total_size + 128 * chunks.size());
    if (!client.download(path, range)) {
        return false;
    }
    if (inbuffer.size() < total_size) {
        return false;
    }
    inbuffer.push_back('\0');
    auto outbuffer = std::vector<char>();
    outbuffer.reserve(max_uncompressed);
    auto data_cur = inbuffer.data();
    auto data_end = inbuffer.data() + inbuffer.size();
    for (auto const& chunk: chunks) {
        if (chunks.size() > 1) {
            auto c = strstr(data_cur, "\r\n\r\n");
            if (c) {
                data_cur = c + 4;
            }
        }
        if ((data_end - data_cur) < chunk.compressed_size) {
            return false;
        }
        outbuffer.resize((size_t)chunk.uncompressed_size);
        auto result = ZSTD_decompress(outbuffer.data(), outbuffer.size(),
                                      data_cur, (size_t)chunk.compressed_size);
        if (ZSTD_isError(result) || result != outbuffer.size()) {
            return false;
        }
        for (auto offset: chunk.offsets) {
            file.seekp(offset);
            file.write(outbuffer.data(), outbuffer.size());
        }
        data_cur += chunk.compressed_size;
    }
    return true;
}

BundleDownloadList BundleDownloadList::from_file_info(FileInfo const& info) noexcept {
    auto chunks = info.chunks;
    std::sort(chunks.begin(), chunks.end(), [](FileChunk const& l, FileChunk const& r) {
        using wrap_t = std::tuple<BundleID, int32_t, int32_t>;
        auto left = wrap_t{l.bundle_id, l.compressed_offset, l.uncompressed_offset};
        auto right = wrap_t{r.bundle_id, r.compressed_offset, r.uncompressed_offset};
        return left < right;
    });
    auto bundles = std::list<BundleDownload>{};
    BundleDownload* bundle = {};
    auto bundle_id = BundleID::None;
    ChunkDownload* chunk = {};
    auto chunk_id = ChunkID::None;
    for(auto const& i: chunks) {
        if (i.bundle_id != bundle_id) {
            bundle = &bundles.emplace_back();
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
    return {bundles};
}
