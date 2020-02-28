#ifndef MANIFEST_INFO_HPP
#define MANIFEST_INFO_HPP
#include "manifest.hpp"
#include <charconv>
#include <algorithm>
#include <cstring>

namespace rman {
    struct ChunkInfo;
    struct BundleInfo;
    struct ParamsInfo;
    struct FileInfo;
    struct ManifestInfo;

    struct ChunkInfo : Chunk {
        uint32_t compressed_offset;
        uint32_t uncompressed_offset;
    };

    struct BundleInfo {
        BundleID id;
        std::vector<ChunkInfo> chunks;
    };

    struct ParamsInfo : ChunkingParameters {};

    struct FileInfo {
        FileID id;
        uint32_t size;
        std::string path;
        std::string link;
        std::vector<std::string> langs;
        std::vector<BundleInfo> bundles;
        ParamsInfo params;
        uint8_t permissions;
        uint8_t unk5;
        uint8_t unk6;
        uint8_t unk8;
        uint8_t unk10;
    };


    struct ManifestInfo {
        ManifestID id;
        std::vector<FileInfo> files;

        ManifestInfo filter_path(std::string pattern) const;
        ManifestInfo filter_lang(std::string lang) const;
        static ManifestInfo from_manifest(Manifest const& manifest);
        std::string to_json() const;
        std::string to_csv() const;
    };

    inline constexpr auto to_hex = [](auto id) {
        uint64_t num = static_cast<uint64_t>(id);
        char buffer[32] = { "0000000000000000" };
        auto conv = std::to_chars(buffer + 16, buffer + 32, num, 16);
        std::transform(buffer + 16, conv.ptr, buffer + 16, ::toupper);
        return std::string { conv.ptr - 16, (size_t)16 };
    };
}

#endif // MANIFEST_INFO
