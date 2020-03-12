#ifndef RMAN_DOWNLOAD_HPP
#define RMAN_DOWNLOAD_HPP
#include "file.hpp"
#include <memory>
#include <filesystem>
#include <map>
#include <fstream>
#include <list>
#include <string_view>
#include <functional>

namespace rman {
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
    };

    struct BundleDownloadList {
        std::vector<std::unique_ptr<BundleDownload>> unfinished = {};
        std::vector<std::unique_ptr<BundleDownload>> good = {};
        std::vector<std::unique_ptr<BundleDownload>> queued = {};

        static BundleDownloadList from_file_info(FileInfo const& info) noexcept;
    };

    enum class HttpState {
        Done = 0,
        RecvData,
        RecvR0,
        RecvN0,
        RecvR1,
        RecvN1,
    };

    struct HttpConnection {
        HttpConnection(std::string url, bool verbose);
        HttpConnection(HttpConnection const&) = delete;
        HttpConnection(HttpConnection&& other) = delete;
        HttpConnection& operator=(HttpConnection const&) = delete;
        HttpConnection& operator=(HttpConnection&& other) = delete;
        ~HttpConnection() noexcept;

        inline void set_file(std::ofstream* file) noexcept {
            outfile_ = file;
        }

        void set_bundle(std::unique_ptr<BundleDownload> bundle);

        inline void* get_handle() const noexcept {
            return handle_;
        }

        inline std::unique_ptr<BundleDownload> get_bundle() noexcept {
            return std::move(bundle_);
        }

        inline bool is_done() const noexcept {
            return state_ == HttpState::Done && bundle_ && bundle_->chunks.size() == chunk_;
        }
    private:
        void* handle_ = {};
        std::string prefix_ = {};
        std::vector<char> inbuffer_ = {};
        mutable std::vector<char> outbuffer_ = {};
        HttpState state_ = {};
        std::unique_ptr<BundleDownload> bundle_ = {};
        size_t chunk_ = {};
        std::ofstream* outfile_ = {};

        static size_t write_data(char const* p, size_t s, size_t n, HttpConnection *c) noexcept;
        bool write(char const* data, char const* end) noexcept;
        char const* receive(char const* data, ptrdiff_t size) noexcept;
        bool decompress(char const* data) const noexcept;
    };

    struct HttpClient {
        HttpClient(std::string url, bool verbose, size_t connections);
        HttpClient(HttpClient const&) = delete;
        HttpClient(HttpClient&& other) = delete;
        HttpClient& operator=(HttpClient const&) = delete;
        HttpClient& operator=(HttpClient&& other) = delete;
        ~HttpClient() noexcept;

        inline bool finished() const noexcept {
            return inprogress_.size() == 0;
        }

        void set_outfile(std::ofstream* file) noexcept;
        void pop(BundleDownloadList& list);
        void push(BundleDownloadList& list);
        void perform();
        void poll(int timeout);
    private:
        void* handle_ = {};
        std::vector<std::unique_ptr<HttpConnection>> connections_ = {};
        std::map<void*, HttpConnection*> inprogress_ = {};
        std::vector<HttpConnection*> free_ = {};
    };
}

#endif // RMAN_DOWNLOAD_HPP
