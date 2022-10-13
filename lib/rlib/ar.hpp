#pragma once
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>

namespace rlib {
    struct Ar {
        struct Entry {
            std::size_t offset;
            std::size_t size;
            bool compressed;
            bool nest;
        };
        using offset_cb = function_ref<void(Entry)>;

        std::size_t chunk_size;
        bool no_wad;
        bool no_wpk;
        bool no_zip;

        auto operator()(IO const& io, offset_cb cb) const -> void;

    private:
        struct WAD;
        struct WPK;
        struct ZIP;

        auto process(IO const& io, offset_cb cb, Entry const& top_entry) const -> void;

        auto process_try_wad(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_wpk(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_try_zip(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool;

        auto process_iter_next(IO const& io,
                               offset_cb cb,
                               Entry const& top_entry,
                               std::size_t& cur,
                               Entry const& entry) const -> void;

        auto process_iter_end(IO const& io, offset_cb cb, Entry const& top_entry, std::size_t cur) const -> void;
    };
}
