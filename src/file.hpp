#ifndef RMAN_FILE_HPP
#define RMAN_FILE_HPP
#include "manifest.hpp"
#include <vector>
#include <list>
#include <string>
#include <unordered_set>
#include <optional>
#include <regex>
#include <fstream>

namespace rman {
    template <typename T>
    inline std::string to_hex (T id, std::size_t s = 16) noexcept {
        static constexpr char table[] = "0123456789ABCDEF";
        char result[] = "0000000000000000";
        auto num = static_cast<uint64_t>(id);
        auto output = result + (s - 1);
        while (num) {
            *(output--) = table[num & 0xF];
            num >>= 4;
        }
        return std::string(result, s);
    };

    struct FileChunk : RMANChunk {
        BundleID bundle_id;
        int32_t compressed_offset;
        int32_t uncompressed_offset;

        bool verify(std::vector<uint8_t> const& buffer, HashType type) const noexcept;
    };

    struct FileInfo {
        FileID id;
        int32_t size;
        std::string path;
        std::string link;
        std::unordered_set<std::string> langs;
        std::vector<FileChunk> chunks;
        RMANParams params;
        uint8_t permissions;
        uint8_t unk5;
        uint8_t unk6;
        uint8_t unk8;
        uint8_t unk10;

        std::string to_csv() const noexcept;
        std::string to_json(int indent) const noexcept;
        std::ofstream create_file(std::string const& folder_name) const;
        bool remove_exist(std::string const& folder_name) noexcept;
        bool remove_verified(std::string const& folder_name) noexcept;
        bool remove_uptodate(FileInfo const& old) noexcept;
    };

    struct FileList {
        std::list<FileInfo> files;

        static FileList from_manifest(RManifest const& manifest);
        static FileList read(char const* data, size_t size);
        inline static FileList read(std::vector<char> const& data) {
            return read(data.data(), data.size());
        }
        void filter_path(std::optional<std::regex> const& pat) noexcept;
        void filter_langs(std::vector<std::string> const& langs) noexcept;
        void remove_uptodate(FileList const& old) noexcept;
        void sanitize() const;
    };
}

#endif // RMAN_FILE_HPP
