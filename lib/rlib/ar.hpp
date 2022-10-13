#pragma once
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>

namespace rlib {
    struct ArSplit {
        struct Entry {
            std::size_t offset;
            std::size_t size;
            bool compressed;
        };
        using offset_cb = function_ref<void(Entry)>;

        std::size_t chunk_size;
        bool no_bnk;
        bool no_wad;
        bool no_wpk;
        bool no_nest;

        auto operator()(IO const& io, offset_cb cb) const -> void;

    private:
        auto process(IO const& io, offset_cb cb, int depth, Entry top_entry) const -> void;

        template <typename T>
        auto process_ar(IO const& io, offset_cb cb, Entry top_entry) const -> void;
    };
}
