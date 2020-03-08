#ifndef RMAN_CLI_HPP
#define RMAN_CLI_HPP
#include <string>
#include <vector>
#include <optional>
#include <regex>

namespace rman {
    struct Url {
        std::string address;
        std::string prefix;
    };

    struct CLI {
        std::string manifest = {};
        bool verify = {};
        bool exist = {};
        std::optional<std::regex> path = {};
        std::vector<std::string> langs = {};
        std::optional<int> json = {};
        std::string upgrade = {};
        std::optional<Url> download = {};
        std::string output = {};

        void parse(int argc, char ** argv);
    };
}

#endif // RMAN_CLI_HPP
