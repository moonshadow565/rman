#ifndef RMAN_DOWNLOAD_HPP
#define RMAN_DOWNLOAD_HPP
#include "file.hpp"
#include <memory>
#include <filesystem>
#include <map>
#include <fstream>
#include <string_view>

namespace httplib {
    class Client;
}

namespace rman {
    using HttpClient = std::shared_ptr<httplib::Client>;
    extern HttpClient make_httpclient(std::string const& address);


    struct ChunkDownload : FileChunk {
        std::vector<int32_t> offsets = {};
    };

    struct BundleDownload {
        BundleID id = {};
        std::vector<ChunkDownload> chunks = {};
        std::string range = {};
        size_t total_size = {};
        size_t offset_count = {};
        size_t max_uncompressed = {};
        std::vector<char> download(HttpClient& client, std::string const& prefix) const noexcept;
        bool write(std::ofstream& file, std::vector<char> inbuffer) const noexcept;
    };

    struct FileDownload {
        std::unique_ptr<std::ofstream> file = {};
        std::vector<BundleDownload> bundles = {};

        static FileDownload from_file_info(FileInfo const& info, std::string const& output);
        size_t download(HttpClient& client, std::string const& prefix) noexcept;
    private:
    };
}

#endif // RMAN_DOWNLOAD_HPP
