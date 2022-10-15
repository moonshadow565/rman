#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace rlib {
    namespace fs = std::filesystem;

    struct IO {
        struct File;

        struct MMap;

        struct Reader;

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

    struct IO::MMap final : IO {
        constexpr MMap() noexcept = default;

        inline MMap(MMap&& other) noexcept : impl_(std::exchange(other.impl_, {})) {}

        inline MMap& operator=(MMap&& other) noexcept {
            impl_ = std::exchange(other.impl_, {});
            return *this;
        }

        MMap(fs::path const& path, Flags flags);

        ~MMap() noexcept;

        auto fd() const noexcept -> std::intptr_t override { return impl_.file.fd(); }

        auto flags() const noexcept -> Flags override { return impl_.file.flags(); }

        auto size() const noexcept -> std::size_t override { return impl_.size; }

        auto shrink_to_fit() noexcept -> bool override;

        auto reserve(std::size_t offset, std::size_t count) noexcept -> bool override;

        auto resize(std::size_t offset, std::size_t count) noexcept -> bool override;

        auto read(std::size_t offset, std::span<char> dst) const noexcept -> bool override;

        auto write(std::size_t offset, std::span<char const> src) noexcept -> bool override;

        auto copy(std::size_t offset, std::size_t count) const -> std::span<char const> override;

    private:
        struct Impl {
            void* data = nullptr;
            std::size_t size = {};
            std::size_t capacity = {};
            IO::File file = {};

            auto remap(std::size_t) noexcept -> bool;
        } impl_ = {};
    };

    struct IO::Reader final {
        constexpr Reader() noexcept = default;

        Reader(IO const& io, std::size_t pos = 0, std::size_t size = (std::size_t)-1);

        auto start() const noexcept -> std::size_t { return start_; }

        auto offset() const noexcept -> std::size_t { return pos_ - start_; }

        auto size() const noexcept -> std::size_t { return end_ - start_; }

        auto remains() const noexcept -> std::size_t { return end_ - pos_; }

        auto contains(std::size_t pos, std::size_t count) const noexcept -> bool {
            return pos <= size() && size() - pos >= count;
        }

        auto skip(std::size_t size) noexcept -> bool;

        auto seek(std::size_t pos) noexcept -> bool;

        auto read_within(Reader& reader, std::size_t size) noexcept -> bool;

        auto read_raw(void*, std::size_t size) noexcept -> bool;

        template <typename T>
            requires(std::is_trivially_copyable_v<T>)
        auto read(T& val) noexcept -> bool { return read_raw(&val, sizeof(T)); }

        template <typename T, typename Into>
            requires(std::is_convertible_v<T, Into>&& std::is_trivially_copyable_v<T>)
        auto read(Into& into, T val = {}) noexcept -> bool {
            if (!this->read(val)) return false;
            into = static_cast<Into>(val);
            return true;
        }

        template <typename T>
            requires(std::is_trivially_copyable_v<T>)
        auto read(std::span<T> dst) noexcept -> bool { return read_raw(dst.data(), dst.size_bytes()); }

        template <typename T>
            requires(std::is_trivially_copyable_v<T>)
        auto read_n(std::vector<T>& dst, std::size_t n) noexcept -> bool {
            if (auto size = n * sizeof(T); remains() >= n) {
                try {
                    dst.resize(n);
                } catch (...) {
                    return false;
                }
                return read_raw(dst.data(), size);
            }
            return false;
        }

        constexpr operator bool() const noexcept { return io_ != nullptr; }

    private:
        IO const* io_ = {};
        std::size_t start_ = {};
        std::size_t pos_ = {};
        std::size_t end_ = {};
    };
};
