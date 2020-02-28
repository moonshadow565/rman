#include "manifest.hpp"
#include <zstd.h>
#include <cstdio>

using namespace rman;

namespace {
    struct Reader {
        char const* beg;
        char const* cur;
        char const* end;

        template<typename T>
        inline T read_raw() {
            auto next = cur + sizeof(T);
            if (next > end) {
                throw std::runtime_error("Failed to read memory!");
            }
            T result;
            memcpy(&result, cur, sizeof(T));
            cur = next;
            return result;
        }

        inline ptrdiff_t read_ptr() {
            auto result = tell();
            result += read_raw<int32_t>();
            return result;
        }

        inline ptrdiff_t tell() const noexcept {
            return cur - beg;
        }

        inline ptrdiff_t seek_rel(ptrdiff_t offset) {
            auto next = cur + offset;
            if (next < beg || next > end) {
                throw std::runtime_error("Failed to skip!");
            }
            cur = next;
            return cur - beg;
        }

        inline ptrdiff_t seek_abs(ptrdiff_t offset) {
            auto next = beg + offset;
            if (next < beg || next > end) {
                throw std::runtime_error("Failed to skip!");
            }
            cur = next;
            return cur - beg;
        }
    };

    struct Table {
        Reader reader;
        std::vector<ptrdiff_t> offsets;
        uint16_t vtable_size;
        uint16_t struct_size;

        Table(Reader reader) : reader(reader) {
            Reader copy = reader;
            auto vtable_offset = copy.tell();
            vtable_offset -= copy.read_raw<int32_t>();
            copy.seek_abs(vtable_offset);
            vtable_size = copy.read_raw<uint16_t>();
            struct_size = copy.read_raw<uint16_t>();
            for(auto i = 4; i < vtable_size; i += 2) {
                auto entry_offset = copy.read_raw<uint16_t>();
                if (entry_offset > struct_size) {
                    throw std::runtime_error("Vtable offset outside struct size");
                }
                offsets.push_back(entry_offset);
            }
        }

        struct VOffset {
            Reader reader;
            ptrdiff_t offset;

            template<typename T>
            inline T num() const && {
                static_assert (std::is_arithmetic_v<T> || std::is_enum_v<T>);
                if (!offset) {
                    return T{};
                }
                Reader copy = reader;
                copy.seek_rel(offset);
                return copy.read_raw<T>();
            }

            inline std::string string() const && {
                if (!offset) {
                    return {};
                }
                Reader copy = reader;
                copy.seek_rel(offset);
                auto string_offset = copy.read_ptr();
                copy.seek_abs(string_offset);
                auto size = copy.read_raw<int32_t>();
                auto next = copy.cur + size;
                if (next < copy.beg || next > copy.end) {
                    throw std::runtime_error("Fail to read string");
                }
                auto result = std::string{copy.cur, (size_t)(size)};
                return result;
            }

            template<typename T>
            inline std::vector<T> list() const && {
                static_assert (std::is_arithmetic_v<T> || std::is_enum_v<T>);
                if (!offset) {
                    return {};
                }
                Reader copy = reader;
                copy.seek_rel(offset);
                auto list_offset = copy.read_ptr();
                copy.seek_abs(list_offset);
                auto size = copy.read_raw<int32_t>();
                auto next = copy.cur + size * sizeof(T);
                if (next < copy.beg || next > copy.end) {
                    throw std::runtime_error("Fail to read raw list");
                }
                auto result = std::vector<T>((size_t)(size));
                memcpy(result.data(), copy.cur, result.size() * sizeof(T));
                return result;
            }

            template<typename Func>
            inline auto list(Func parser) const && {
                using func_result_type = decltype(parser(Reader{}));
                auto results = std::vector<func_result_type>();
                if (!offset) {
                    return results;
                }
                Reader copy = reader;
                copy.seek_rel(offset);
                auto list_offset = copy.read_ptr();
                copy.seek_abs(list_offset);
                auto size = copy.read_raw<int32_t>();
                auto start = copy.tell();
                for (auto i = 0; i != size; i++) {
                    auto offset = start + i * 4;
                    copy.seek_abs(offset);
                    offset += copy.read_raw<int32_t>();
                    copy.seek_abs(offset);
                    results.push_back(parser(copy));
                }
                return results;
            }

            explicit inline operator bool() const noexcept {
                return offset != 0;
            }

            inline bool operator!() const noexcept {
                return offset == 0;
            }
        };

        inline VOffset operator[](size_t index) const noexcept {
            if(index >= offsets.size()) {
                return { reader, 0 };
            }
            return { reader, offsets[index] };
        }

        inline size_t size() const noexcept {
            return offsets.size();
        }
    };

    static inline Chunk Chunk_read(Reader reader) {
        auto result = Chunk {};
        auto vtable = Table{reader};
        result.id = vtable[0].num<ChunkID>();
        result.compressed_size = vtable[1].num<uint32_t>();
        result.uncompressed_size = vtable[2].num<uint32_t>();
        return result;
    }

    static inline Bundle Bundle_read(Reader reader) {
        auto result = Bundle {};
        auto vtable = Table{reader};
        result.id = vtable[0].num<BundleID>();
        result.chunks = vtable[1].list(&Chunk_read);
        return result;
    }

    static inline Lang Lang_read(Reader reader) {
        auto result = Lang{};
        auto vtable = Table{reader};
        result.id = vtable[0].num<LangID>();
        result.name = vtable[1].string();
        return result;
    }

    static inline File File_read(Reader reader) {
        auto result = File{};
        auto vtable = Table{reader};
        result.id = vtable[0].num<FileID>();
        result.parent_dir_id = vtable[1].num<DirID>();
        result.size = vtable[2].num<uint32_t>();
        result.name = vtable[3].string();
        result.locale_flags = vtable[4].num<uint64_t>();
        result.unk5 = vtable[5].num<uint8_t>();    // ???, unk size
        result.unk6 = vtable[6].num<uint8_t>();    // ???, unk size
        result.chunk_ids = vtable[7].list<ChunkID>();
        result.unk8 = vtable[8].num<uint8_t>();    // set to 1 when part of .app on MacOS
        result.link = vtable[9].string();
        result.unk10 = vtable[10].num<uint8_t>();  // ???, unk size
        result.params_index = vtable[11].num<uint8_t>();
        result.permissions = vtable[12].num<uint8_t>();
        if (vtable[5] || vtable[6] ||vtable[10]) {
            [](){}();
        }
        return result;
    }

    static inline Dir Dir_read(Reader reader) {
        auto result = Dir {};
        auto vtable = Table{reader};
        result.id = vtable[0].num<DirID>();
        result.parent_dir_id = vtable[1].num<DirID>();
        result.name = vtable[2].string();
        return result;
    }

    static inline EncryptionKey EncryptionKey_read(Reader reader) {
        auto result = EncryptionKey {};
        auto vtable = Table{reader};
        return result;
    }

    static inline ChunkingParameters ChunkingParameters_read(Reader reader) {
        auto result = ChunkingParameters{};
        auto vtable = Table{reader};
        result.unk0 = vtable[0].num<uint16_t>();  // ??? sometimes 1, sometimes not present
        result.unk1 = vtable[1].num<uint8_t>();   // ???, allways 3 ...
        result.unk2 = vtable[2].num<uint8_t>();   // ???
        result.unk3 = vtable[3].num<int32_t>();   // ?? max_uncompressed_size / power of 2
        result.max_uncompressed_size = vtable[4].num<uint32_t>();
        return result;
    }

    static inline Body Body_read(Reader reader) {
        auto result = Body{};
        auto vtable = Table{reader};
        result.bundles = vtable[0].list(&Bundle_read);
        result.langs = vtable[1].list(&Lang_read);
        result.files = vtable[2].list(&File_read);
        result.dirs = vtable[3].list(&Dir_read);
        result.keys = vtable[4].list(&EncryptionKey_read);
        result.params = vtable[5].list(&ChunkingParameters_read);
        return result;
    }
}

Manifest Manifest::read(char const* beg, size_t size) {
    auto result = Manifest {};
    Reader reader = { beg, beg, beg + size };
    auto magic = reader.read_raw<std::array<char, 4>>();
    auto version_major = reader.read_raw<uint8_t>();
    auto version_minor = reader.read_raw<uint8_t>();
    if (magic != std::array{'R', 'M', 'A', 'N'}) {
        throw std::runtime_error("Manifest magic doesn't match!");
    }
    if (version_major != 2) {
        throw std::runtime_error("Manifest major version not supported!");
    }
    (void)version_minor;
    result.flags = reader.read_raw<ManifestFlags>();
    auto offset = reader.read_raw<int32_t>();
    auto length = reader.read_raw<int32_t>();
    result.id = reader.read_raw<ManifestID>();
    auto body_length = reader.read_raw<int32_t>();
    if (reader.beg + offset > reader.end || reader.beg + offset < reader.beg) {
        throw std::runtime_error("Manifest offset outside range!");
    }
    reader.cur = reader.beg + offset;
    if (reader.cur + length > reader.end || reader.cur + length < reader.beg) {
        throw std::runtime_error("Manifest length outside range!");
    }
    auto data = std::vector<char>((size_t)(body_length));
    auto zstd_result = ZSTD_decompress(data.data(), data.size(), reader.cur, (size_t)length);
    if (ZSTD_isError(zstd_result)) {
        throw std::runtime_error("Manifest Body not decompressed");
    }
    auto body_reader = Reader{data.data(), data.data(), data.data() + data.size()};
    auto body_offset = body_reader.read_ptr();
    body_reader.seek_abs(body_offset);
    result.body = Body_read(body_reader);
    return result;
}

Manifest Manifest::read(const char *filename) {
    auto file = fopen(filename, "rb");
    if (!file) {
        throw std::runtime_error("Failed to open file!");
    }
    fseek(file, 0, SEEK_END);
    auto end = ftell(file);
    fseek(file, 0, SEEK_SET);
    auto data = std::vector<char>((size_t)end);
    fread(data.data(), 1, data.size(), file);
    fclose(file);
    return Manifest::read(data.data(), data.size());
}
