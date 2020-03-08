#include "download.hpp"
#include "error.hpp"
#include <iterator>
using namespace rman;
namespace fs = std::filesystem;
/// Curl include
#define NOMINMAX
#include <zstd.h>
#include <httplib.h>
/// End curl include

HttpClient rman::make_httpclient(std::string const& address) {
    auto httpclient = std::make_shared<httplib::Client>(address);
    rman_assert(httpclient != nullptr);
    httpclient->set_keep_alive_max_count(0);
    return httpclient;
}

FileDownload FileDownload::from_file_info(FileInfo const& info, std::string const& output) {
    auto path = fs::path(output) / fs::path(info.path, fs::path::generic_format);
    auto folder = path.parent_path();
    if (!fs::exists(folder)) {
        rman_rethrow(fs::create_directories(folder));
    }
    if (fs::exists(path)) {
        auto size = rman_rethrow(fs::file_size(path));
        if (size > INT32_MAX || (int32_t)size != info.size) {
            rman_rethrow(fs::resize_file(path, (uint32_t)info.size));
        }
    }
    auto result = FileDownload{};
    result.file = std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::ate);
    rman_assert(result.file->good());
    result.params = info.params;
    result.chunks = info.chunks;
    std::sort(result.chunks.begin(), result.chunks.end(),
              [](FileChunk const& l, FileChunk const& r) {
        using wrap_t = std::tuple<BundleID, int32_t, int32_t>;
        auto left = wrap_t {l.bundle_id, l.uncompressed_offset, l.compressed_offset};
        auto right = wrap_t {r.bundle_id, r.uncompressed_offset, r.compressed_offset};
        return left < right;
    });
    return result;
}

bool FileDownload::write(size_t start_chunk, size_t end_chunk,
                         std::string_view data, bool multi) noexcept {
    auto chunk_id = ChunkID::None;
    auto buffer = std::vector<char>();
    buffer.reserve((size_t)params.max_uncompressed);
    auto start_data = data.data();
    auto end_data = start_data + data.size();
    for (auto i = start_chunk; i != end_chunk; i++) {
        if (chunks[i].id != chunk_id) {
            if (multi) {
                auto c = strstr(start_data, "\r\n\r\n");
                if (c) {
                    start_data = c + 4;
                }
            }
            if ((end_data - start_data) < chunks[i].compressed_size) {
                return false;
            }
            buffer.resize((size_t)chunks[i].uncompressed_size);
            auto result = ZSTD_decompress(buffer.data(), buffer.size(),
                                          start_data, (size_t)chunks[i].compressed_size);
            if (ZSTD_isError(result) || result != buffer.size()) {
                return false;
            }
            start_data += chunks[i].compressed_size;
            chunk_id = chunks[i].id;
        }
        file->seekp(chunks[i].uncompressed_offset);
        file->write(buffer.data(), buffer.size());
    }
    return true;
}

size_t FileDownload::download(HttpClient& client, std::string const& prefix) noexcept {
    std::string range = "";
    auto start_bundle = size_t{0};
    auto last_chunk_id = ChunkID::None;
    int32_t total_size = 0;
    bool multi = false;
    std::vector<char> inbuffer = {};
    size_t finished = 0;
    for (auto i = start_bundle; i != chunks.size();) {
        if (chunks[i].id != last_chunk_id) {
            if (!range.empty()) {
                range += ", ";
                multi = true;
            } else {
                range += "bytes=";
            }
            range += std::to_string(chunks[i].compressed_offset);
            range += "-";
            range += std::to_string(chunks[i].compressed_offset + chunks[i].compressed_size - 1);
            last_chunk_id = chunks[i].id;
            total_size += chunks[i].compressed_size;
        }
        ++i;
        if (i == chunks.size() || chunks[i].bundle_id != chunks[start_bundle].bundle_id) {
            auto bundle_id = chunks[start_bundle].bundle_id;
            auto path = prefix + "/bundles/" + to_hex(bundle_id) + ".bundle";
            inbuffer.clear();
            inbuffer.reserve((size_t)total_size);
            client->Get(path.c_str(), {{ "Range", range }},
                        [&inbuffer](char const* data, size_t size) -> bool {
                inbuffer.insert(inbuffer.end(), data, data + size);
                return true;
            });
            inbuffer.push_back('\0');
            if (inbuffer.size() >= (size_t)total_size) {
                if(write(start_bundle, i, {inbuffer.data(), inbuffer.size()}, multi)) {
                    finished += i - start_bundle;
                }
            }
            start_bundle = i;
            range.clear();
            last_chunk_id = ChunkID::None;
            total_size = 0;
            multi = false;
        }
    }
    return finished;
}
