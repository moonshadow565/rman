#ifndef RMAN_DOWNLOAD_HPP
#define RMAN_DOWNLOAD_HPP
#include "file.hpp"
#include <memory>
#include <filesystem>
#include <map>
#include <fstream>

namespace httplib {
    class Client;
}

namespace rman {
    using HttpClient = std::shared_ptr<httplib::Client>;
    extern HttpClient make_httpclient(std::string const& address);

    struct FileDownload {
        std::unique_ptr<std::ofstream> file;
        std::vector<FileChunk> chunks;
        RMANParams params;

        static FileDownload from_file_info(FileInfo const& info, std::string const& output);
        size_t download(HttpClient& client, std::string const& prefix) noexcept;
    private:
        bool write(size_t start_chunk, size_t end_chunk,
                   std::string const& data, bool multi) noexcept;
    };
}

#endif // RMAN_DOWNLOAD_HPP
