#pragma once
#include <cstddef>
#include <span>
#include <utility>

#include "iofile.hpp"

namespace rlib {
    using Buffer = IO::Buffer;
    struct IO::Buffer final : IO {
        Buffer() = default;
        Buffer(Buffer const&) = delete;
        Buffer(Buffer&& other) noexcept : impl_(std::exchange(other.impl_, {})) {}
        Buffer& operator=(Buffer other) noexcept {
            impl_ = std::exchange(other.impl_, {});
            return *this;
        }
        ~Buffer() noexcept;

        operator std::span<char>() noexcept { return {impl_.data, impl_.size}; }
        operator std::span<char const>() const noexcept { return {impl_.data, impl_.size}; }

        auto data() noexcept -> char* { return impl_.data; }
        auto data() const noexcept -> char const* { return impl_.data; }
        auto size() const noexcept -> std::size_t override { return impl_.size; }
        auto capacity() const noexcept -> std::size_t { return impl_.capacity; }
        auto empty() const noexcept -> bool { return impl_.size == 0; }
        auto clear() noexcept -> void { impl_.size = 0; }

        auto subspan(std::size_t off = 0, std::size_t size = std::dynamic_extent) noexcept -> auto{
            return std::span<char>(impl_.data, impl_.size).subspan(off, size);
        }
        auto subspan(std::size_t off = 0, std::size_t size = std::dynamic_extent) const noexcept -> auto{
            return std::span<char const>(impl_.data, impl_.size).subspan(off, size);
        }

        [[nodiscard]] auto append(std::span<char const> src) noexcept -> bool;
        template <typename T>
            requires(std::is_trivially_copyable_v<T>)
        auto append_s(std::span<T const> src) noexcept -> bool {
            return this->append({(char const*)src.data(), src.size() * sizeof(T)});
        }

        [[nodiscard]] auto reserve_keep(std::size_t size) noexcept -> bool;
        [[nodiscard]] auto resize_keep(std::size_t size) noexcept -> bool;
        [[nodiscard]] auto reserve_destroy(std::size_t size) noexcept -> bool;
        [[nodiscard]] auto resize_destroy(std::size_t size) noexcept -> bool;

        auto fd() const noexcept -> std::intptr_t override { return 0; }
        auto flags() const noexcept -> Flags override { return WRITE; }
        auto shrink_to_fit() noexcept -> bool override;
        auto reserve(std::size_t offset, std::size_t count) noexcept -> bool override;
        auto resize(std::size_t offset, std::size_t count) noexcept -> bool override;
        auto read(std::size_t offset, std::span<char> dst) const noexcept -> bool override;
        auto write(std::size_t offset, std::span<char const> src) noexcept -> bool override;
        auto copy(std::size_t offset, std::size_t count) const -> std::span<char const> override;

    private:
        struct Impl {
            char* data = {};
            std::size_t size = {};
            std::size_t capacity = {};
        } impl_ = {};
    };
}