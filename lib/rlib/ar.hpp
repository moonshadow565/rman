#pragma once
#include <bitset>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>

#define rlib_ar_assert(...)                                                 \
    do {                                                                    \
        if (!(__VA_ARGS__)) [[unlikely]] {                                  \
            this->push_error(top_entry, __PRETTY_FUNCTION__, #__VA_ARGS__); \
            return false;                                                   \
        }                                                                   \
    } while (false)

namespace rlib {
    struct ArDyn;

    struct ArDynDeleter {
        auto operator()(ArDyn* ptr) noexcept -> void;
    };

    extern auto make_ardyn(fs::path const& init) -> std::unique_ptr<ArDyn, ArDynDeleter>;

    struct Ar {
        struct Entry {
            std::size_t offset;
            std::size_t size;
            bool high_entropy;
            bool nest;
        };
        using offset_cb = function_ref<void(Entry)>;

        struct Processor {
            std::string_view name;
            bool (Ar::*method)(IO const& io, offset_cb cb, Entry const& top_entry) const;
        };

        std::size_t chunk_min;
        std::size_t chunk_max;
        std::bitset<32> disabled;
        std::uint32_t cdc;
        bool strict;
        std::unique_ptr<ArDyn, ArDynDeleter> ardyn;
        mutable std::vector<std::string> errors;

        static auto PROCESSORS(bool cdc = false) noexcept -> std::span<Processor const>;

        static auto PROCESSORS_LIST(bool cdc = false) noexcept -> std::string;

        static auto PROCESSOR_PARSE(std::string const& str, bool cdc = false) -> std::uint32_t;

        auto operator()(IO const& io, offset_cb cb) const -> void;

    private:
        struct FSB;
        struct FSB5;
        struct MAC;
        struct Load;
        struct PE;
        struct WAD;
        struct WPK;
        struct ZIP;

        auto process(IO const& io, offset_cb cb, Entry const& top_entry) const -> void;

        auto process_try_dyn(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_fsb(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_fsb5(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_load(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_mac_exe(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_mac_fat(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_pe(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_wad(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_wpk(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_zip(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_iter(IO const& io, offset_cb cb, Entry const& top_entry, std::vector<Entry> entries) const -> bool;

        auto push_error(Entry const& top_entry, char const* func, char const* expr) const -> void;

        auto process_cdc_fixed(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_cdc_bup(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;
    };
}
