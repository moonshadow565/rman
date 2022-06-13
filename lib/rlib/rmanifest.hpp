#pragma once
#include <cinttypes>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <vector>

#include "rbundle.hpp"

namespace rlib {
    namespace fs = std::filesystem;

    enum class FileID : std::uint64_t { None };

    enum class ManifestID : std::uint64_t { None };

    struct RMAN {
        struct Filter {
            std::optional<std::regex> path;
            std::optional<std::regex> langs;
        };

        struct Params {
            std::uint16_t unk0;
            HashType hash_type;
            std::uint8_t unk2;
            std::uint32_t unk3;
            std::uint32_t max_uncompressed;
        };

        struct File {
            FileID fileId;
            Params params;
            std::uint8_t permissions;
            std::uint32_t size;
            std::string path;
            std::string link;
            std::string langs;
            std::vector<RBUN::ChunkDst> chunks;

            auto matches(Filter const& filter) const noexcept -> bool;

            auto verify(fs::path const& path, bool force = false) const -> std::optional<std::vector<RBUN::ChunkDst>>;
        };

        ManifestID manifestId;
        std::vector<File> files;
        std::vector<RBUN> bundles;

        static RMAN read(std::span<char const> data);

    private:
        struct Raw;
    };
}