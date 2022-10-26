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
#include "rchunk.hpp"

namespace rlib {
    namespace fs = std::filesystem;

    enum class FileID : std::uint64_t { None };

    enum class ManifestID : std::uint64_t { None };

    struct RMAN {
        struct File {
            FileID fileId;
            std::uint8_t permissions;
            std::uint64_t size;
            std::string path;
            std::string link;
            std::string langs;
            std::vector<RChunk::Dst> chunks;

            auto dump() const -> std::string;

            static auto undump(std::string_view data) -> File;

            auto verify(fs::path const& path, RChunk::Dst::data_cb on_good) const -> std::vector<RChunk::Dst>;
        };

        struct Filter {
            std::optional<std::regex> path;
            std::optional<std::regex> langs;

            auto operator()(File const& file) const noexcept -> bool;
        };

        ManifestID manifestId;
        std::vector<File> files;
        std::vector<RBUN> bundles;

        static auto read(std::span<char const> data, Filter const& filter = {}) -> RMAN;
        static auto read_file(fs::path const& path, Filter const& filter = {}) -> RMAN;

        auto lookup() const -> std::unordered_map<std::string, File const*>;

    private:
        static auto read_jrman(std::span<char const> data, Filter const& filter) -> RMAN;
        static auto read_zrman(std::span<char const> data, Filter const& filter) -> RMAN;

        struct Raw;
    };
}

template <>
struct fmt::formatter<rlib::FileID> : formatter<std::string> {
    template <typename FormatContext>
    auto format(rlib::FileID id, FormatContext& ctx) {
        return formatter<std::string>::format(fmt::format("{:016X}", (std::uint64_t)id), ctx);
    }
};
