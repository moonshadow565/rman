#include "ar.hpp"
using namespace rlib;

struct Ar::PE {
    struct DOS;
    struct NT;
    struct Section;

    static inline auto constexpr MAGIC_DOS = 0x5A4Du;
    static inline auto constexpr MAGIC_NT = 0x4550u;
};

struct Ar::PE::DOS {
    std::uint16_t magic;
    std::uint16_t pad[28];
    std::uint32_t nt;
};

struct Ar::PE::NT {
    std::uint32_t magic;
    std::uint16_t machine;
    std::uint16_t nsects;
    std::uint32_t timestamp;
    std::uint32_t symtab;
    std::uint32_t symcount;
    std::uint16_t optsize;
    std::uint16_t characteristcs;
};

struct Ar::PE::Section {
    char name[8];
    std::uint32_t vmsize;
    std::uint32_t vmaddr;
    std::uint32_t filesize;
    std::uint32_t fileoff;
    std::uint32_t relocsoff;
    std::uint32_t linesoff;
    std::uint16_t nrelocs;
    std::uint16_t nlines;
    std::uint32_t characteristcs;
};

auto Ar::process_try_pe(IO const &io, offset_cb cb, Entry const &top_entry) const -> bool {
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    auto dos = PE::DOS{};
    if (!reader.read(dos) || dos.magic != PE::MAGIC_DOS) {
        return false;
    }

    auto nt = PE::NT{};
    if (!reader.seek(dos.nt) || !reader.read(nt) || nt.magic != PE::MAGIC_NT) {
        return false;
    }
    rlib_assert(reader.skip(nt.optsize));

    auto sections = std::vector<PE::Section>();
    rlib_assert(reader.read_n(sections, nt.nsects));

    auto entries = std::vector<Entry>();
    entries.reserve(nt.nsects);

    for (auto const &section : sections) {
        if (section.filesize == 0) {
            continue;
        }
        rlib_ar_assert(reader.contains(section.fileoff, section.filesize));
        entries.push_back(Entry{
            .offset = top_entry.offset + section.fileoff,
            .size = section.filesize,
        });
    }

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}