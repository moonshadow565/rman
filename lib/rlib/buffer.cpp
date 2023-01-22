#include "buffer.hpp"

#include <cstdlib>

#include "common.hpp"

using namespace rlib;

Buffer::~Buffer() noexcept {
    if (impl_.data) {
        free(impl_.data);
    }
}

auto Buffer::reserve_keep(std::size_t size) noexcept -> bool {
    if (size > impl_.capacity) {
        auto capacity = std::max(size, std::bit_ceil(size));
        auto data = (char*)realloc(impl_.data, capacity);
        if (data == nullptr) [[unlikely]] {
            return false;
        }
        impl_.data = data;
        impl_.capacity = capacity;
    }
    return true;
}

auto Buffer::resize_keep(std::size_t size) noexcept -> bool {
    if (!reserve_keep(size)) [[unlikely]] {
        return false;
    }
    impl_.size = size;
    return true;
}

auto Buffer::reserve_destroy(std::size_t size) noexcept -> bool {
    if (size > impl_.capacity) {
        auto capacity = std::max(size, std::bit_ceil(size));
        auto data = (char*)malloc(capacity);
        if (data == nullptr) [[unlikely]] {
            return false;
        }
        free(impl_.data);
        impl_.data = data;
        impl_.capacity = capacity;
    }
    return true;
}

auto Buffer::resize_destroy(std::size_t size) noexcept -> bool {
    if (!reserve_destroy(size)) [[unlikely]] {
        return false;
    }
    impl_.size = size;
    return true;
}

auto Buffer::append(std::span<char const> src) noexcept -> bool {
    auto const offset = this->size();
    if (!this->resize_keep(offset + src.size())) {
        return false;
    }
    std::memcpy(impl_.data + offset, src.data(), src.size());
    return true;
}

auto Buffer::shrink_to_fit() noexcept -> bool { return resize_keep(impl_.size); }

auto IO::Buffer::reserve(std::size_t offset, std::size_t count) noexcept -> bool {
    std::size_t total = offset + count;
    if (total < offset || total < count) [[unlikely]] {
        return false;
    }
    return reserve_keep(total);
}

auto IO::Buffer::resize(std::size_t offset, std::size_t count) noexcept -> bool {
    std::size_t total = offset + count;
    if (total < offset || total < count) [[unlikely]] {
        return false;
    }
    return resize_keep(total);
}

auto IO::Buffer::read(std::size_t offset, std::span<char> dst) const noexcept -> bool {
    if (!in_range(offset, dst.size(), size())) {
        return false;
    }
    std::memcpy(dst.data(), impl_.data + offset, dst.size());
    return true;
}

auto IO::Buffer::write(std::size_t offset, std::span<char const> src) noexcept -> bool {
    auto const total = offset + src.size();
    if (total < offset || total < src.size()) [[unlikely]] {
        return false;
    }
    if (!this->reserve_keep(total)) [[unlikely]] {
        return false;
    }
    std::memcpy(impl_.data + offset, src.data(), src.size());
    return true;
}

auto IO::Buffer::copy(std::size_t offset, std::size_t count) const -> std::span<char const> {
    rlib_assert(in_range(offset, count, impl_.size));
    return {impl_.data + offset, count};
}