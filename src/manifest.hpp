#ifndef RMAN_MANIFEST_HPP
#define RMAN_MANIFEST_HPP
#include <string>
#include <vector>

namespace rman {
    enum class ChunkID : uint64_t {
        None
    };

    enum class BundleID : uint64_t {
        None
    };

    enum class LangID : uint8_t {
        None
    };

    enum class FileID : uint64_t {
        None
    };

    enum class DirID : uint64_t {
        None
    };

    enum class ManifestID : uint64_t {
        None
    };

    enum class HashType : uint8_t {
        None,
        SHA512,
        SHA256,
        RITO_HKDF,
    };

    struct RMANChunk {
        ChunkID id;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
    };

    struct RMANBundle {
        BundleID id;
        std::vector<RMANChunk> chunks;
    };

    struct RMANLang {
        LangID id;
        std::string name;
    };

    struct RMANFile {
        FileID id;
        DirID parent_dir_id;
        uint32_t size;
        std::string name;
        uint64_t locale_flags;
        uint8_t unk5;
        uint8_t unk6;
        std::string link;
        uint8_t unk8;
        std::vector<ChunkID> chunk_ids = {};
        uint8_t unk10;
        uint8_t params_index;
        uint8_t permissions;
    };

    struct RMANDir {
        DirID id;
        DirID parent_dir_id;
        std::string name;
    };

    struct RMANKey {};

    struct RMANParams {
        uint16_t unk0;
        HashType hash_type;
        uint8_t unk2;
        uint32_t unk3;
        uint32_t max_uncompressed;
    };

    struct RManifest {
        std::vector<RMANBundle> bundles;
        std::vector<RMANLang> langs;
        std::vector<RMANFile> files;
        std::vector<RMANDir> dirs;
        std::vector<RMANKey> keys;
        std::vector<RMANParams> params;
        static RManifest read(char const* data, size_t size);
        inline static RManifest read(std::vector<char> const& data) {
            return read(data.data(), data.size());
        }
    };
}

#endif // RMAN_MANIFEST_HPP
