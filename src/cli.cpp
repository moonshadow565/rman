#include "cli.hpp"
#include "error.hpp"
#include <cinttypes>
#include <argparse.hpp>
using namespace rman;

static inline constexpr auto DEFAULT_URL = "http://lol.secure.dyn.riotcdn.net/channels/public";

void CLI::parse(int argc, char ** argv) {
    argparse::ArgumentParser program("fckrman");
    program.add_argument("action")
            .help("action: list, download, json")
            .required()
            .action([](std::string const& value){
                if (value == "list" || value == "ls") {
                    return Action::List;
                }
                if (value == "json" || value == "js") {
                    return Action::Json;
                }
                if (value == "download" || value == "dl") {
                    return Action::Download;
                }
                throw std::runtime_error("Unknown action!");
            });
    program.add_argument("manifest")
            .required()
            .help(".manifest or .json");
    program.add_argument("--curl-verbose")
            .help("Curl debuging")
            .default_value(false)
            .implicit_value(true);
    program.add_argument("-v", "--verify")
            .help("Skip: verified chunks")
            .default_value(false)
            .implicit_value(true);
    program.add_argument("-e", "--exist")
            .help("Skip: existing files.")
            .default_value(false)
            .implicit_value(true);
    program.add_argument("-l", "--lang")
            .help("Filter: language(none for international files).")
            .default_value(std::vector<std::string>{});
    program.add_argument("-p", "--path")
            .help("Filter: path regex")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const& value) -> std::optional<std::regex> {
                if(value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });
    program.add_argument("-u", "--update")
            .help("Filter: update from old manifest.")
            .default_value(std::string(""));
    program.add_argument("-r", "--retry")
            .help("Number of retrys for failed bundles")
            .default_value(uint32_t{0})
            .action([](std::string const& value) -> uint32_t {
                auto result = std::stoul(value);
                return result;
            });
    program.add_argument("-d", "--download")
            .help("Url: to download from.")
            .default_value(std::string(DEFAULT_URL));
    program.add_argument("-c", "--connections")
            .default_value(uint32_t{32})
            .action([](std::string const& value) -> uint32_t {
                auto result = std::stoul(value);
                if (result < 1) {
                    throw std::runtime_error("Minimum number of connections is 1");
                }
                return result;
            });
    program.add_argument("-o", "--output")
            .help("Directory: output")
            .default_value(std::string("."));
    program.parse_args(argc, argv);
    manifest = program.get<std::string>("manifest");
    action = program.get<Action>("action");
    curl_verbose = program.get<bool>("--curl-verbose");
    verify = program.get<bool>("-v");
    exist = program.get<bool>("-e");
    langs = program.get<std::vector<std::string>>("-l");
    path = program.get<std::optional<std::regex>>("-p");
    upgrade = program.get<std::string>("-u");
    retry = program.get<uint32_t>("-r");
    download = program.get<std::string>("-d");
    connections = program.get<uint32_t>("-c");
    output = program.get<std::string>("-o");
}


