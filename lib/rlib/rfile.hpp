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
        std::vector<RChunk::Dst> chunks;

        enum BundleStatus : bool {
            UNKNOWN_BUNDLE,
            KNOWN_BUNDLE,
        };

        struct Match {
            std::optional<std::regex> path;
            std::optional<std::regex> langs;

            auto operator()(RFile const& file) const noexcept -> bool;
        };

        using read_cb = function_ref<bool(RFile&)>;

        auto dump() const -> std::string;

        static auto undump(std::string_view data) -> RFile;

        auto verify(fs::path const& path, RChunk::Dst::data_cb on_good) const -> std::vector<RChunk::Dst>;

        static auto read(std::span<char const> data, read_cb cb) -> BundleStatus;
        static auto read_file(fs::path const& path, read_cb cb) -> BundleStatus;

        static auto writer(fs::path const& out, bool append = false) -> std::function<void(RFile&&)>;

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
