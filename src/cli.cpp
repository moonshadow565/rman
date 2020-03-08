#include "cli.hpp"
#include "error.hpp"
#include <cinttypes>
#include <argparse.hpp>
using namespace rman;

static inline constexpr auto DEFAULT_URL = "lol.secure.dyn.riotcdn.net";
static inline constexpr auto DEFAULT_PREFIX = "/channels/public";

void CLI::parse(int argc, char ** argv) {
    argparse::ArgumentParser program("fckrman");
    program.add_argument("manifest")
            .required()
            .help(".manifest or .json");
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
    program.add_argument("-j", "--json")
            .help("Json: print json instead of csv")
            .default_value(std::optional<int>{})
            .implicit_value(std::optional<int>{-1});
    program.add_argument("-d", "--download")
            .help("Url: to download from.")
            .default_value(std::optional<Url>{})
            .implicit_value(std::optional<Url> { Url { DEFAULT_URL, DEFAULT_PREFIX } })
            .action([](std::string value) -> std::optional<Url> {
                if (auto http = value.find("http://"); http == 0) {
                    value = value.substr(6, value.size() - 6);
                } else if (auto https = value.find("http://"); https == 0) {
                    value = value.substr(7, value.size() - 6);
                }
                while(value.size() && value.back() == '/') {
                    value.pop_back();
                }
                if (!value.size()) {
                    throw std::runtime_error("Url can't be empty");
                }
                auto result = Url {};
                if (auto prefix = value.find('/'); prefix != std::string::npos) {
                    result.address = value.substr(0, prefix);
                    result.prefix = value.substr(prefix, value.size() - prefix);
                } else {
                    result.address = value;
                }
                return result;
            });
    program.add_argument("-o", "--output")
            .help("Directory: output")
            .default_value(std::string("."));
    program.parse_args(argc, argv);
    manifest = program.get<std::string>("manifest");
    verify = program.get<bool>("-v");
    exist = program.get<bool>("-e");
    langs = program.get<std::vector<std::string>>("-l");
    path = program.get<std::optional<std::regex>>("-p");
    json = program.get<std::optional<int>>("-j");
    upgrade = program.get<std::string>("-u");
    download = program.get<std::optional<Url>>("-d");
    output = program.get<std::string>("-o");
}


