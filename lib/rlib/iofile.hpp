#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

namespace rlib {
    namespace fs = std::filesystem;

    struct IO {
        struct File;

        enum Flags : unsigned;

        virtual ~IO() noexcept = default;

        virtual auto fd() const noexcept -> std::intptr_t = 0;

        virtual auto flags() const noexcept -> Flags = 0;

        virtual auto size() const noexcept -> std::size_t = 0;

        virtual auto shrink_to_fit() noexcept -> bool = 0;

        virtual auto reserve(std::size_t offset, std::size_t count) noexcept -> bool = 0;

        virtual auto resize(std::size_t offset, std::size_t count) noexcept -> bool = 0;

        virtual auto read(std::size_t offset, std::span<char> dst) const noexcept -> bool = 0;

        virtual auto write(std::size_t offset, std::span<char const> src) noexcept -> bool = 0;

        virtual auto copy(std::size_t offset, std::size_t count) const -> std::span<char const> = 0;

    private:
        constexpr IO() noexcept = default;
        constexpr IO(IO&& other) noexcept = default;
        constexpr IO(IO const& other) noexcept = delete;
        constexpr IO& operator=(IO const& other) noexcept = delete;
        constexpr IO& operator=(IO&& other) noexcept = default;
    };

    enum IO::Flags : unsigned {
        READ = 0,
        WRITE = 1 << 0,
        SEQUENTIAL = 1 << 1,
        RANDOM_ACCESS = 1 << 2,
        NO_INTERUPT = 1 << 3,
        NO_OVERGROW = 1 << 4,
    };

    constexpr auto operator|(IO::Flags lhs, IO::Flags rhs) noexcept -> IO::Flags {
        return (IO::Flags)((unsigned)lhs | (unsigned)rhs);
    }

    constexpr auto operator^(IO::Flags lhs, IO::Flags rhs) noexcept -> IO::Flags {
        return (IO::Flags)((unsigned)lhs ^ (unsigned)rhs);
    }

    constexpr auto operator&(IO::Flags lhs, IO::Flags rhs) noexcept -> IO::Flags {
        return (IO::Flags)((unsigned)lhs & (unsigned)rhs);
    }

    struct IO::File final : IO {
        constexpr File() noexcept = default;

        constexpr File(File&& other) noexcept : impl_(std::exchange(other.impl_, {})) {}

        constexpr File& operator=(File&& other) noexcept {
            impl_ = std::exchange(other.impl_, {});
            return *this;
        }

        File(fs::path const& path, Flags flags);

        ~File() noexcept;

        auto fd() const noexcept -> std::intptr_t override { return impl_.fd; }

        auto flags() const noexcept -> Flags override { return impl_.flags; }

        auto size() const noexcept -> std::size_t override { return impl_.size; }

        auto shrink_to_fit() noexcept -> bool override;

        auto reserve(std::size_t offset, std::size_t count) noexcept -> bool override;

        auto resize(std::size_t offset, std::size_t count) noexcept -> bool override;

        auto read(std::size_t offset, std::span<char> dst) const noexcept -> bool override;

        auto write(std::size_t offset, std::span<char const> src) noexcept -> bool override;

        auto copy(std::size_t offset, std::size_t count) const -> std::span<char const> override;

    private:
        struct Impl {
            std::intptr_t fd = {};
            std::size_t size = {};
            Flags flags = {};
        } impl_ = {};
    };
};
