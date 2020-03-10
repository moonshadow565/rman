#ifndef RMAN_DOWNLOAD_HPP
#define RMAN_DOWNLOAD_HPP
#include "file.hpp"
#include <memory>
#include <filesystem>
#include <map>
#include <fstream>
#include <list>
#include <string_view>

namespace rman {
    struct HttpClientHandleDeleter {
        void operator()(void* handle) const noexcept;
    };

    struct HttpClient {
        std::string prefix;
        std::unique_ptr<std::vector<char>> buffer;
        std::unique_ptr<void, HttpClientHandleDeleter> handle;
        bool download(std::string const& path, std::string const& range) noexcept;

        inline HttpClient() noexcept = default;
        HttpClient(std::string url, bool verbose);
    };

    struct ChunkDownload : FileChunk {
        std::vector<int32_t> offsets = {};
    };

    struct BundleDownload {
        BundleID id = {};
        std::vector<ChunkDownload> chunks = {};
        std::string range = {};
        std::string path = {};
        size_t total_size = {};
        size_t offset_count = {};
        size_t max_uncompressed = {};
        bool download(HttpClient& client, std::ofstream &file) const noexcept;
    };

    struct BundleDownloadList {
        std::list<BundleDownload> bundles = {};

        static BundleDownloadList from_file_info(FileInfo const& info) noexcept;
        size_t download(HttpClient& client, std::string const& prefix) noexcept;
    };
}

#endif // RMAN_DOWNLOAD_HPP
