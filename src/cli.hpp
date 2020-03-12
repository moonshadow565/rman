#ifndef RMAN_CLI_HPP
#define RMAN_CLI_HPP
#include <string>
#include <vector>
#include <optional>
#include <regex>

namespace rman {
    enum class Action {
        List,
        Json,
        Download,
    };

    struct CLI {
        Action action = {};
        std::string manifest = {};
        bool curl_verbose = {};
        bool verify = {};
        bool exist = {};
        std::optional<std::regex> path = {};
        std::vector<std::string> langs = {};
        std::string upgrade = {};
        uint32_t retry = {};
        uint32_t connections = {};
        std::string download = {};
        std::string output = {};

        void parse(int argc, char ** argv);
    };
}

#endif // RMAN_CLI_HPP
