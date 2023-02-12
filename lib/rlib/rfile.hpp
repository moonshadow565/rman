#pragma once
#include <cinttypes>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <vector>

#include "rchunk.hpp"

namespace rlib {
    namespace fs = std::filesystem;

    enum class FileID : std::uint64_t { None };

    struct RFile {
        FileID fileId;
        std::uint8_t permissions;
        std::uint64_t size;
        std::string path;
        std::string link;
        std::string langs;
        std::uint64_t time;
        std::optional<std::vector<RChunk::Dst>> chunks;

        struct Match {
            std::optional<std::regex> path;
            std::optional<std::regex> langs;

            auto operator()(RFile const& file) const noexcept -> bool;
        };

        using read_cb = function_ref<bool(RFile&)>;

        auto dump() const -> std::string;

        static auto undump(std::string_view data) -> RFile;
        static auto read(std::span<char const> data, read_cb cb) -> void;
        static auto read_file(fs::path const& path, read_cb cb) -> void;

        static auto writer(fs::path const& out, bool append = false) -> std::function<void(RFile&&)>;

        static auto has_known_bundle(fs::path const& path) -> bool;

    private:
        static auto read_jrman(std::span<char const> data, read_cb cb) -> void;
        static auto read_zrman(std::span<char const> data, read_cb cb) -> void;
    };
}

template <>
struct fmt::formatter<rlib::FileID> : formatter<std::string> {
    template <typename FormatContext>
    auto format(rlib::FileID id, FormatContext& ctx) {
        return formatter<std::string>::format(fmt::format("{:016X}", (std::uint64_t)id), ctx);
    }
};
