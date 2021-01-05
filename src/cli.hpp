#ifndef RMAN_CLI_HPP
#define RMAN_CLI_HPP
#include <string>
#include <vector>
#include <optional>
#include <regex>
#include "download_opts.hpp"

namespace rman {
    enum class Action {
        List,
        ListBundles,
        ListChunks,
        Json,
        Download,
    };

    struct CLI {
        Action action = {};
        std::string manifest = {};
        bool verify = {};
        bool exist = {};
        bool nowrite = {};
        std::optional<std::regex> path = {};
        std::vector<std::string> langs = {};
        std::string upgrade = {};
        std::string output = {};
        DownloadOpts download = {};

        void parse(int argc, char ** argv);
    };
}

#endif // RMAN_CLI_HPP
