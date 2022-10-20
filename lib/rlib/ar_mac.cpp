#include "ar.hpp"

using namespace rlib;

struct Ar::MAC {
    struct FATHeader;
    struct Arch;
    struct Arch64;
    struct EXEHeader;
    struct Command;
    struct Section;
    struct Section64;
    struct Segment;
    struct Segment64;
    static constexpr auto FAT_MAGIC = 0xcafebabeu;
    static constexpr auto FAT_MAGIC_64 = 0xcafebabfu;
    static constexpr auto EXE_MAGIC = 0xfeedfaceu;
    static constexpr auto EXE_MAGIC_64 = 0xfeedfacfu;
    static constexpr auto LC_SEGMENT = 0x1u;
    static constexpr auto LC_SEGMENT_64 = 0x19u;
};

struct Ar::MAC::FATHeader {
    std::uint32_t magic;
    std::uint32_t narchs;
};

struct Ar::MAC::Arch {
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint64_t offset;
    std::uint64_t size;
    std::uint32_t align;
    std::uint32_t reserved;
};

struct Ar::MAC::Arch64 {
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t align;
};

struct Ar::MAC::EXEHeader {
    std::uint32_t magic;
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
};

struct Ar::MAC::Command {
    std::uint32_t cmd;
    std::uint32_t size;
};

struct Ar::MAC::Segment {
    char segname[16];
    std::uint32_t vmaddr;
    std::uint32_t vmsize;
    std::uint32_t fileoff;
    std::uint32_t filesize;
    std::uint32_t maxprot;
    std::uint32_t initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

struct Ar::MAC::Section {
    char sectname[16];
    char segname[16];
    std::uint32_t addr;
    std::uint32_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
};

struct Ar::MAC::Segment64 {
    char segname[16];
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    std::uint32_t maxprot;
    std::uint32_t initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

struct Ar::MAC::Section64 {
    char sectname[16];
    char segname[16];
    std::uint64_t addr;
    std::uint64_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
    std::uint32_t reserved3;
};

auto Ar::process_try_mac_fat(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);
    auto header = MAC::FATHeader{};
    if (!reader.read(header) || header.narchs >= 43) {
        return false;
    }
    if (header.magic == MAC::FAT_MAGIC) {
        auto archs = std::vector<MAC::Arch>();
        rlib_assert(reader.read_n(archs, header.narchs));
        auto entries = std::vector<Entry>(archs.size());
        for (std::size_t i = 0; i != archs.size(); ++i) {
            auto const& arch = archs[i];
            rlib_assert(reader.contains(arch.offset, arch.size));
            entries.push_back(Entry{
                .offset = top_entry.offset + arch.offset,
                .size = arch.size,
                .nest = true,
            });
        }
        rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));
        return true;
    }
    if (header.magic == MAC::FAT_MAGIC_64) {
        auto archs = std::vector<MAC::Arch>();
        rlib_assert(reader.read_n(archs, header.narchs));
        auto entries = std::vector<Entry>(archs.size());
        for (std::size_t i = 0; i != archs.size(); ++i) {
            auto const& arch = archs[i];
            rlib_assert(reader.contains(arch.offset, arch.size));
            entries.push_back(Entry{
                .offset = top_entry.offset + arch.offset,
                .size = arch.size,
                .nest = true,
            });
        }
        rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));
        return true;
    }
    return false;
}

auto Ar::process_try_mac_exe(IO const& io, offset_cb cb, Entry const& top_entry) const -> bool {
    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    auto header = MAC::EXEHeader{};
    if (!reader.read(header) || (header.magic != MAC::EXE_MAGIC && header.magic != MAC::EXE_MAGIC_64)) {
        return false;
    }
    if (header.magic == MAC::EXE_MAGIC_64) {
        rlib_ar_assert(reader.skip(4));
    }

    auto reader_cmds = IO::Reader();
    rlib_ar_assert(reader.read_within(reader_cmds, header.sizeofcmds));
    rlib_ar_assert(reader.remains() >= sizeof(MAC::Command) * header.ncmds);

    auto entries = std::vector<Entry>{};
    for (std::size_t i = 0; i != header.ncmds; ++i) {
        auto cmd = MAC::Command{};
        rlib_ar_assert(reader_cmds.read(cmd));

        auto reader_cmd = IO::Reader();
        rlib_ar_assert(reader_cmds.read_within(reader_cmd, cmd.size - sizeof(MAC::Command)));

        if (cmd.cmd == MAC::LC_SEGMENT) {
            auto segment = MAC::Segment{};
            rlib_ar_assert(reader_cmd.read(segment));
            auto sections = std::vector<MAC::Section>();
            rlib_ar_assert(reader_cmd.read_n(sections, segment.nsects));
            if (segment.filesize == 0) {
                continue;
            }
            rlib_ar_assert(reader.contains(segment.fileoff, segment.filesize));
            if (segment.filesize <= chunk_min || sections.size() == 0) {
                entries.push_back(Entry{
                    .offset = top_entry.offset + segment.fileoff,
                    .size = segment.filesize,
                });
                continue;
            }
            for (auto const& section : sections) {
                if (section.offset == 0) {
                    continue;
                }
                rlib_ar_assert(section.offset >= segment.fileoff);
                rlib_ar_assert(section.offset - segment.fileoff <= segment.filesize);
                rlib_ar_assert(reader.contains(section.offset, section.size));
                entries.push_back(Entry{
                    .offset = top_entry.offset + section.offset,
                    .size = section.size,
                });
            }
            continue;
        }

        if (cmd.cmd == MAC::LC_SEGMENT_64) {
            auto segment = MAC::Segment64{};
            rlib_ar_assert(reader_cmd.read(segment));
            auto sections = std::vector<MAC::Section64>();
            rlib_ar_assert(reader_cmd.read_n(sections, segment.nsects));
            if (segment.filesize == 0) {
                continue;
            }
            rlib_ar_assert(reader.contains(segment.fileoff, segment.filesize));
            if (segment.filesize <= chunk_min || sections.size() == 0) {
                entries.push_back(Entry{
                    .offset = top_entry.offset + segment.fileoff,
                    .size = segment.filesize,
                });
                continue;
            }
            for (auto const& section : sections) {
                if (section.offset == 0) {
                    continue;
                }
                rlib_ar_assert(section.offset >= segment.fileoff);
                rlib_ar_assert(section.offset - segment.fileoff <= segment.filesize);
                rlib_ar_assert(reader.contains(section.offset, section.size));
                entries.push_back(Entry{
                    .offset = top_entry.offset + section.offset,
                    .size = section.size,
                });
            }
            continue;
        }
    }

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}
