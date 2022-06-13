#pragma once
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace rlib {
    extern auto to_hex(std::uint64_t id, std::size_t s = 16) noexcept -> std::string;

    template <typename T>
        requires(std::is_enum_v<T>)
    inline auto to_hex(T id, std::size_t s = 16) noexcept -> std::string { return to_hex((std::uint64_t)id, s); }

    extern auto progress(char const* banner, std::uint32_t index, std::uint64_t done, std::uint64_t total) noexcept
        -> std::string;

    extern auto clean_path(std::string path) noexcept -> std::string;

    extern auto zstd_decompress(std::span<char const> src, std::size_t count) -> std::span<char const>;

    extern auto try_zstd_decompress(std::span<char const> src, std::size_t count) -> std::span<char const>;

    template <auto... M>
    inline auto sort_by(auto beg, auto end) noexcept -> void {
        std::sort(beg, end, [](auto const& l, auto const& r) { return std::tie((l.*M)...) < std::tie((r.*M)...); });
    }

    template <typename F>
    inline auto remove_if(auto& container, F&& func) noexcept -> bool {
        container.erase(std::remove_if(container.begin(), container.end(), std::forward<F>(func)), container.end());
        return container.empty();
    }

    inline auto in_range(std::size_t offset, std::size_t size, std::size_t target) noexcept -> bool {
        return target >= offset && target - offset >= size;
    }

    inline auto in_range(char const* ptr, std::size_t size, std::span<char const> target) noexcept -> bool {
        return ptr >= target.data() && (target.data() + target.size() - ptr) >= size;
    }

    template <typename Signature>
    struct function_ref;

    template <typename Ret, typename... Args>
    struct function_ref<Ret(Args...)> {
        constexpr function_ref() noexcept = default;

        template <typename Func>
            requires(std::is_invocable_r_v<Ret, Func, Args...>)
        function_ref(Func* func)
        noexcept
            : ref_((void*)func),
              invoke_(+[](void* ref, Args... args) -> Ret { return std::invoke(*(Func*)ref, args...); }) {}

        template <typename Func>
            requires(std::is_invocable_r_v<Ret, Func, Args...>)
        function_ref(Func&& func)
        noexcept : function_ref(&func) {}

        explicit constexpr operator bool() const noexcept { return ref_; }

        constexpr bool operator!() const noexcept { return !ref_; }

        auto operator()(Args... args) -> Ret { return invoke_(ref_, args...); }

    private:
        Ret (*invoke_)(void* ref, Args...) = nullptr;
        void* ref_ = nullptr;
    };
}