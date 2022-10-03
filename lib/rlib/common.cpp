#include "common.hpp"

#include <zstd.h>

#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <iomanip>
#include <iostream>

using namespace rlib;

void rlib::throw_error(char const* from, char const* msg) {
    // break point goes here
    throw std::runtime_error(std::string(from) + msg);
}

error_stack_t& rlib::error_stack() noexcept {
    thread_local error_stack_t instance = {};
    return instance;
}

void rlib::push_error_msg(char const* fmt, ...) noexcept {
    va_list args;
    char buffer[4096];
    int result;
    va_start(args, fmt);
    result = vsnprintf(buffer, 4096, fmt, args);
    va_end(args);
    if (result >= 0) {
        error_stack().push_back({buffer, buffer + result});
    }
}

rlib::progress_bar::progress_bar(char const* banner,
                                 bool disabled,
                                 std::uint32_t index,
                                 std::uint64_t done,
                                 std::uint64_t total) noexcept
    : banner_(banner),
      disabled_(disabled), 
      index_(index), 
      done_(done), 
      total_(std::max(total, std::uint64_t{1})), 
      percent_(done_ * 100 / total_) 
{
    this->render();
}

rlib::progress_bar::~progress_bar() noexcept {
    this->render();
    std::cerr << std::endl;
}

auto rlib::progress_bar::render() const noexcept -> void {
    char buffer[128];
    sprintf(buffer, "\r%s #%u: %.02fMB %u%%", banner_, index_, total_ / MB, (std::uint32_t)percent_);
    std::cerr << buffer;
}

auto rlib::progress_bar::update(std::uint64_t done) noexcept -> void {
    done_ = done;
    auto percent = std::exchange(percent_, done_ * 100 / total_);
    if (!disabled_ && percent != percent_) {
        this->render();
    }
}

auto rlib::from_hex(std::string name) noexcept -> std::optional<std::uint64_t> {
    auto result = std::uint64_t{};
    auto [p, ec] = std::from_chars(name.data(), name.data() + name.size(), result, 16);
    if (ec != std::errc{}) {
        return std::nullopt;
    }
    if (p != name.data() + name.size()) {
        return std::nullopt;
    }
    return result;
}

auto rlib::clean_path(std::string path) noexcept -> std::string {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.ends_with('/')) {
        path.pop_back();
    }
    return path;
}

auto rlib::zstd_decompress(std::span<char const> src, std::size_t count) -> std::span<char const> {
    thread_local static std::vector<char> buffer = {};
    std::size_t size_decompressed = rlib_assert_zstd(ZSTD_findDecompressedSize(src.data(), src.size()));
    rlib_assert(size_decompressed == count);
    if (buffer.size() < count) {
        buffer.clear();
        buffer.resize(count);
    }
    std::size_t result = rlib_assert_zstd(ZSTD_decompress(buffer.data(), count, src.data(), src.size()));
    rlib_assert(result == size_decompressed);
    return {buffer.data(), count};
}

auto rlib::zstd_frame_decompress_size(std::span<char const> src) -> std::size_t {
    ZSTD_frameHeader header = {};
    rlib_assert_zstd(ZSTD_getFrameHeader(&header, src.data(), src.size()));
    return header.frameContentSize;
}

auto rlib::collect_files(std::vector<std::string> const& inputs,
                         function_ref<bool(fs::path const& path)> filter,
                         bool recursive) -> std::vector<fs::path> {
    auto paths = std::vector<fs::path>{};
    if (inputs.size() == 1 && inputs.back() == "-") {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) {
                continue;
            }
            rlib_trace("input = %s", line.c_str());
            rlib_assert(fs::exists(line));
            paths.push_back(line);
        }
    } else {
        for (auto const& input : inputs) {
            rlib_trace("input = %s", input.c_str());
            rlib_assert(fs::exists(input));
            if (fs::is_regular_file(input)) {
                paths.push_back(input);
            } else if (recursive) {
                for (auto const& entry : fs::recursive_directory_iterator(input)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    if (filter && !filter(entry.path())) {
                        continue;
                    }
                    paths.push_back(entry.path());
                }
            } else {
                for (auto const& entry : fs::directory_iterator(input)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    if (filter && !filter(entry.path())) {
                        continue;
                    }
                    paths.push_back(entry.path());
                }
            }
        }
    }
    return paths;
}
