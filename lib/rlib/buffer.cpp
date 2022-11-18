#include "buffer.hpp"

#include <cstdlib>

#include "common.hpp"

using namespace rlib;

Buffer::~Buffer() noexcept {
    if (impl_.data) {
        free(impl_.data);
    }
}

auto Buffer::resize_keep(std::size_t size) noexcept -> bool {
    if (size > impl_.capacity) {
        auto capacity = std::max(size, std::bit_ceil(size));
        auto data = (char*)realloc(impl_.data, capacity);
        if (data == nullptr) [[unlikely]] {
            return false;
        }
        impl_.data = data;
        impl_.capacity = capacity;
    }
    impl_.size = size;
    return true;
}

auto Buffer::resize_destroy(std::size_t size) noexcept -> bool {
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