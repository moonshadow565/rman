#ifndef RMAN_DOWNLOAD_OPTS_HPP
#define RMAN_DOWNLOAD_OPTS_HPP
#include <string>

namespace rman {
    enum class RangeMode {
        Full,
        One,
        Multi,
    };

    struct DownloadOpts {
        std::string prefix = {};
        std::string archive = {};
        RangeMode range_mode = {};
        uint32_t retry = {};
        uint32_t connections = {};
        bool curl_verbose = {};
        long curl_buffer = {};
        std::string curl_proxy = {};
        std::string curl_useragent = {};
        std::string curl_cookiefile = {};
        std::string curl_cookielist = {};
    };
}

#endif // DOWNLOAD_OPTS_HPP
