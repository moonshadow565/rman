#ifndef MANIFEST_HPP
#define MANIFEST_HPP
#include <cinttypes>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include <array>
#include <optional>

namespace rman {
    struct Chunk;
    enum class ChunkID : uint64_t {
        None
    };
    struct Bundle;
    enum class BundleID : uint64_t {
        None
    };
    struct Lang;
    enum class LangID : uint8_t {};
    struct File;
    enum class FileID : uint64_t {
        None
    };
    struct Dir;
    enum class DirID : uint64_t {
        None
    };
    struct EncryptionKey;
    struct ChunkingParameters;
    struct Body;
    struct Manifest;
    enum class ManifestFlags : uint16_t {};
    enum class ManifestID : uint64_t {};


    struct Chunk {
        ChunkID id;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
    };

    struct Bundle {
        BundleID id;
        std::vector<Chunk> chunks;
    };

    struct Lang {
        LangID id;
        std::string name;
    };

    struct File {
        FileID id;
        DirID parent_dir_id;
        uint32_t size;
        std::string name;
        uint64_t locale_flags;
        uint8_t unk5;
        uint8_t unk6;
        std::string link;
        uint8_t unk8;
        std::vector<ChunkID> chunk_ids;
        uint8_t unk10;
        uint8_t params_index;
        uint8_t permissions;
    };

    struct Dir {
        DirID id;
        DirID parent_dir_id;
        std::string name;
    };

    struct EncryptionKey {};

    struct ChunkingParameters {
        uint16_t unk0;
        uint8_t unk1;
        uint8_t unk2;
        uint32_t unk3;
        uint32_t max_uncompressed_size;
    };

    struct Body {
        std::vector<Bundle> bundles;
        std::vector<Lang> langs;
        std::vector<File> files;
        std::vector<Dir> dirs;
        std::vector<EncryptionKey> keys;
        std::vector<ChunkingParameters> params;
    };

    struct Manifest {
        ManifestFlags flags;
        ManifestID id;
        Body body;

        static Manifest read(char const* beg, size_t size);
        static Manifest read(char const* filename);
    };
}

#endif // MANIFEST_HPP
