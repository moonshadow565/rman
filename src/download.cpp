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
    return httpclient;
}

std::vector<char> BundleDownload::download(HttpClient &client,
                                           std::string const& prefix) const noexcept {
    std::vector<char> inbuffer = {};
    inbuffer.reserve(total_size + 64 * chunks.size());
    auto path = prefix + "/bundles/" + to_hex(id) + ".bundle";
    auto result = client->Get(path.c_str(), {{ "Range", range }},
                [&inbuffer](char const* data, size_t size) -> bool {
        inbuffer.insert(inbuffer.end(), data, data + size);
        return true;
    });
    inbuffer.push_back('\0');
    return inbuffer;
}

bool BundleDownload::write(std::ofstream &file, std::vector<char> inbuffer) const noexcept {
    if (inbuffer.size() < total_size) {
        return false;
    }
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
    result.file = std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::ate | std::ios::in);
    rman_assert(result.file->good());
    auto chunks = info.chunks;
    std::sort(chunks.begin(), chunks.end(),
              [](FileChunk const& l, FileChunk const& r) {
        using wrap_t = std::tuple<BundleID, int32_t, int32_t>;
        auto left = wrap_t {l.bundle_id, l.uncompressed_offset, l.compressed_offset};
        auto right = wrap_t {r.bundle_id, r.uncompressed_offset, r.compressed_offset};
        return left < right;
    });
    BundleDownload* bundle = {};
    auto bundle_id = BundleID::None;
    ChunkDownload* chunk = {};
    auto chunk_id = ChunkID::None;
    for(auto const& i: chunks) {
        if (i.bundle_id != bundle_id) {
            bundle = &result.bundles.emplace_back();
            bundle->id = i.bundle_id;
            bundle_id = i.bundle_id;
            chunk_id = ChunkID::None;
        }
        if (i.id != chunk_id) {
            if (!bundle->range.empty()) {
                bundle->range += ", ";
            } else {
                bundle->range += "bytes=";
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
    return result;
}

size_t FileDownload::download(HttpClient& client, std::string const& prefix) noexcept {
    size_t finished = 0;
    for(auto const& bundle: bundles) {
        auto inbuffer = bundle.download(client, prefix);
        if (bundle.write(*file, std::move(inbuffer))) {
            finished += bundle.offset_count;
        }
    }
    return finished;
}
