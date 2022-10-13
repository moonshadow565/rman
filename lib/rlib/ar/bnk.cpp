#include "bnk.hpp"

#include <map>

#define ar_assert(...)                                          \
    do {                                                        \
        if (!(__VA_ARGS__)) return " BNK::read: " #__VA_ARGS__; \
    } while (false)

using namespace rlib;
using namespace rlib::ar;

struct BNK::Entry::Raw {
    std::array<char, 4> type;
    std::uint32_t size;
};

struct BNK::Entry::DIDX {
    std::uint32_t id;
    std::uint32_t offset;
    std::uint32_t size;
};

auto BNK::check_magic(std::span<char const> data) noexcept -> bool {
    return data.size() >= 4 && std::memcmp(data.data(), "BKHD", 4) == 0;
}

auto BNK::read(IO const& io, std::size_t offset, std::size_t size) -> char const* {
    using TYPE = std::array<char, 4>;
    static constexpr auto BKHD = TYPE{'B', 'K', 'H', 'D'};
    static constexpr auto DIDX = TYPE{'D', 'I', 'D', 'X'};
    static constexpr auto DATA = TYPE{'D', 'A', 'T', 'A'};

    auto magic = TYPE{};
    ar_assert(size >= 8);
    io.read(offset, magic);
    ar_assert(magic == BKHD);

    auto sections = std::map<TYPE, Entry>{};
    for (std::size_t i = offset; i != offset + size;) {
        Entry::Raw raw = {};
        ar_assert(size >= i);
        ar_assert(size - i >= sizeof(raw));
        io.read(i, {(char*)&raw, sizeof(raw)});

        i += sizeof(Entry::Raw);
        ar_assert(size - i >= raw.size);

        sections[raw.type] = Entry{.offset = i, .size = raw.size};

        i += raw.size;
    }

    entries.clear();
    entries.reserve(sections.size());

    auto i_didx = sections.find(DIDX);
    auto i_data = sections.find(DATA);
    if (i_didx != sections.end() && i_data != sections.end()) {
        auto didx_base = i_didx->second;
        auto data_base = i_data->second;

        ar_assert(didx_base.size % sizeof(Entry::DIDX) == 0);
        auto didx_list = std::vector<Entry::DIDX>(didx_base.size / sizeof(Entry::DIDX));
        io.read(didx_base.offset, {(char*)didx_list.data(), didx_base.size});

        entries.reserve(sections.size() + didx_list.size());
        for (auto const& didx : didx_list) {
            ar_assert(data_base.size >= didx.offset);
            ar_assert(data_base.size - didx.offset >= didx.size);
            entries.push_back(Entry{
                .offset = data_base.offset + didx.offset,
                .size = didx.size,
                .compressed = true,
            });
        }

        i_didx->second.size = 0;
        i_data->second.size = 0;
    }

    for (auto [key, entry] : sections) {
        entry.offset -= sizeof(Entry::Raw);
        entry.size += sizeof(Entry::Raw);
        entries.push_back(entry);
    }

    return nullptr;
}