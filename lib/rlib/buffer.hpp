#pragma once
#include <cstddef>
#include <span>
#include <utility>

namespace rlib {
    struct Buffer {
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
        auto size() const noexcept -> std::size_t { return impl_.size; }
        auto empty() const noexcept -> bool { return impl_.size == 0; }
        auto clear() noexcept -> void { impl_.size = 0; }

        auto subspan(std::size_t off = 0, std::size_t size = std::dynamic_extent) noexcept -> auto{
            return std::span<char>(impl_.data, impl_.size).subspan(off, size);
        }
        auto subspan(std::size_t off = 0, std::size_t size = std::dynamic_extent) const noexcept -> auto{
            return std::span<char const>(impl_.data, impl_.size).subspan(off, size);
        }

        [[nodiscard]] auto append(std::span<char const> src) noexcept -> bool;
        [[nodiscard]] auto resize_keep(std::size_t size) noexcept -> bool;
        [[nodiscard]] auto resize_destroy(std::size_t size) noexcept -> bool;

    private:
        struct Impl {
            char* data = {};
            std::size_t size = {};
            std::size_t capacity = {};
        } impl_ = {};
    };
}