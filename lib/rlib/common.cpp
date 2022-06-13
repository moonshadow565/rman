#include "common.hpp"

#include <zstd.h>

#include "error.hpp"

using namespace rlib;

auto rlib::progress(char const* banner, std::uint32_t index, std::uint64_t done, std::uint64_t total) noexcept
    -> std::string {
    constexpr auto MB = 1024.0 * 1024.0;
    char buffer[128];
    sprintf(buffer, "%s #%u: %4.3f / %4.3f", banner, index, done / MB, total / MB);
    return buffer;
}

auto rlib::to_hex(std::uint64_t id, std::size_t s) noexcept -> std::string {
    static constexpr char table[] = "0123456789ABCDEF";
    char result[] = "0000000000000000";
    auto num = static_cast<std::uint64_t>(id);
    auto output = result + (s - 1);
    while (num) {
        *(output--) = table[num & 0xF];
        num >>= 4;
    }
    return std::string(result, s);
};

auto rlib::clean_path(std::string path) noexcept -> std::string {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.ends_with('/')) {
        path.pop_back();
    }
    return path;
}

auto rlib::zstd_decompress(std::span<char const> src, std::size_t count) -> std::span<char const> {
    thread_local static std::vector<char> buffer = {};
    if (buffer.size() < count) {
        buffer.resize(count);
    }
    auto result = ZSTD_decompress(buffer.data(), count, src.data(), src.size());
    if (ZSTD_isError(result)) {
        rlib_error(ZSTD_getErrorName(result));
    }
    rlib_assert(result == count);
    return {buffer.data(), count};
}

auto rlib::try_zstd_decompress(std::span<char const> src, std::size_t count) -> std::span<char const> {
    thread_local static std::vector<char> buffer = {};
    if (buffer.size() < count) {
        buffer.resize(count);
    }
    auto result = ZSTD_decompress(buffer.data(), count, src.data(), src.size());
    if (ZSTD_isError(result)) {
        return {};
    }
    return {buffer.data(), result};
}
