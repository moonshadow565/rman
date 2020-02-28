#include <cstdio>
#include <manifest.hpp>
#include <manifest_info.hpp>
#include <zstd.h>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace rman;
namespace fs = std::filesystem;

struct IncompleteFile {
    std::ofstream outfile = {};
    fs::path out = {};
    bool finished = false;
    IncompleteFile(fs::path const& out) : out(out)  {
        outfile.open(out.generic_string().c_str(), std::ios::binary);
    }

    void write(size_t offset, char const* data, size_t compressed_size, size_t uncompressed_size) {
        auto buffer = std::vector<char>(uncompressed_size);
        auto zstd_result = ZSTD_decompress(buffer.data(), uncompressed_size, data, compressed_size);
        if (ZSTD_isError(zstd_result)) {
            throw std::runtime_error("Failed to decompress file!");
        }
        outfile.seekp((std::streamoff)offset);
        outfile.write(buffer.data(), uncompressed_size);
    }

    ~IncompleteFile() {
        outfile.close();
        if (!finished) {
            fs::remove(out);
        }
    }
};

struct CurlInit {
    CurlInit() {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    ~CurlInit() {
        curl_global_cleanup();
    }
};

struct CurlDeleter {
    void operator()(CURL* handle) const noexcept {
        curl_easy_cleanup(handle);
    }
};

struct Downloader {
    std::unique_ptr<CURL, CurlDeleter> client;
    std::string url;
    fs::path folder;
    std::vector<char> buffer = {};

    Downloader(std::string cdn, fs::path folder) : client(curl_easy_init()), url(cdn), folder(folder) {
        static CurlInit curlinit = {};
        if (url.size() == 0 || url.back() != '/') {
            url.push_back('/');
        }
        curl_easy_setopt(client.get(), CURLOPT_VERBOSE, 0L);
        curl_easy_setopt(client.get(), CURLOPT_NOPROGRESS, 1L);
        curl_easy_setopt(client.get(), CURLOPT_WRITEFUNCTION, write_buffer);
        curl_easy_setopt(client.get(), CURLOPT_WRITEDATA, (void*)&buffer);
    }

    static size_t write_buffer(void *ptr, size_t size, size_t nmemb, void *stream) {
        auto buffer = (std::vector<char>*)stream;
        buffer->insert(buffer->end(), (char const*)ptr, (char const*)ptr + size * nmemb);
        return size * nmemb;
    }

    void download_file_impl(FileInfo const& info, fs::path const& dest) {
        fs::create_directories(dest.parent_path());
        auto file = IncompleteFile{dest};
        for (auto const& bundle: info.bundles) {
            std::string target = url + "bundles/" + to_hex(bundle.id) + ".bundle";
            uint32_t start = 0xFFFFFFFF;
            uint32_t end = 0;
            for (auto const& chunk: bundle.chunks) {
                start = std::min(start, chunk.compressed_offset);
                end = std::max(end, chunk.compressed_offset + chunk.compressed_size);
            }
            auto range = std::to_string(start) + "-" + std::to_string(end - 1);
            uint32_t size = end - start;
            buffer.resize(0);
            buffer.reserve(size);
            curl_easy_setopt(client.get(), CURLOPT_URL, target.c_str());
            curl_easy_setopt(client.get(), CURLOPT_RANGE, range.c_str());
            if (curl_easy_perform(client.get()) != CURLE_OK) {
                throw std::runtime_error("Not OK: " + target + " @ " + range);
            }
            if (buffer.size() < size) {
                throw std::runtime_error("Not downloaded completly: " + target + " @ " + range);
            }
            for (auto const& chunk: bundle.chunks) {
                file.write(chunk.uncompressed_offset,
                           buffer.data() + (chunk.compressed_offset - start),
                           chunk.compressed_size,
                           chunk.uncompressed_size);
            }
        }
        file.finished = true;
    }

    void download_file(FileInfo const& info) {
        auto dest = folder / fs::path(info.path, fs::path::generic_format);
        if (!fs::exists(dest)) {
            download_file_impl(info, dest);
        }
    }
};

int main(int argc, char** argv) {
#ifdef NDEBUG
    std::string input = argc > 1 ? argv[1] : "";
    std::string output = argc > 2 ? argv[2] : "";
#else
    std::string input = argc > 1 ? argv[1] : "manifest2.manifest";
    std::string output = argc > 2 ? argv[2] : "./output";
#endif
    std::string filter = argc > 3 ? argv[3] : "";
    std::string lang = argc > 4 ? argv[4] : "en_US";
    std::string cdn = argc > 5 ? argv[5] : "https://lol.secure.dyn.riotcdn.net/channels/public/";
    try {
        if (input.empty() || output.empty() || cdn.empty()) {
            throw std::runtime_error("./rman2json <manifest file> "
                                     "<output dir> "
                                     "<optional: regex filter, default empty> "
                                     "<optional lang filter, default: en_US> "
                                     "<optional cdn, default: http://lol.secure.dyn.riotcdn.net/channels/public/>");
        }
        auto manifest = Manifest::read(input.c_str());
        auto info = ManifestInfo::from_manifest(manifest);
        if (!filter.empty()) {
            info = info.filter_path(filter);
        }
        if (!lang.empty()) {
            info = info.filter_lang(lang);
        }
        auto downloader = Downloader{cdn, output};
        for (auto const& file: info.files) {
            printf("Downloading: %s\n", file.path.c_str());
            try {
                downloader.download_file(file);
                printf("Done!\n");
            } catch(std::exception const& error) {
                printf("Failed: %s!\n", error.what());
            }
        }
    } catch (std::exception const& error) {
        fputs(error.what(), stderr);
        return 1;
    }
    return 0;
}
