#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

namespace rlib {
    namespace fs = std::filesystem;

    struct IOFile final {
        constexpr IOFile() noexcept = default;

        constexpr IOFile(IOFile const& other) noexcept = delete;

        constexpr IOFile(IOFile&& other) noexcept : impl_(std::exchange(other.impl_, {})) {}

        constexpr IOFile& operator=(IOFile&& other) noexcept {
            impl_ = std::exchange(other.impl_, {});
            return *this;
        }

        constexpr IOFile& operator=(IOFile const& other) noexcept = delete;

        constexpr explicit operator bool() const noexcept { return impl_.fd; }

        constexpr bool operator!() const noexcept { return !impl_.fd; }

        IOFile(fs::path const& path, bool write);

        ~IOFile() noexcept;

        auto size() const noexcept -> std::size_t { return impl_.size; }

        auto resize(std::size_t offset, std::size_t count) noexcept -> bool;

        auto read(std::size_t offset, std::span<char> dst) const noexcept -> bool;

        auto write(std::size_t offset, std::span<char const> src, bool no_interupt = false) noexcept -> bool;

        auto copy(std::size_t offset, std::size_t count) const -> std::span<char const>;

    private:
        struct Impl {
            std::intptr_t fd = {};
            std::size_t size = {};
        } impl_ = {};
    };
};
