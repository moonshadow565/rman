#include "cli.hpp"
#include "error.hpp"
#include <cinttypes>
#include <argparse.hpp>
#include <filesystem>

namespace fs = std::filesystem;
using namespace rman;

static inline constexpr auto DEFAULT_URL = "http://lol.secure.dyn.riotcdn.net/channels/public";

static std::vector<std::string> parse_list(std::string const& value) {
    std::vector<std::string> result = {};
    size_t next = 0;
    size_t end = value.size();
    auto skip_whitespace = [&](size_t cur) -> size_t {
        while (cur != end && (value[cur] == ' ' || value[cur] == ',')) {
            cur++;
        }
        return cur;
    };
    auto get_next = [&](size_t cur) -> size_t {
        while (cur != end && (value[cur] != ' ' && value[cur] != ',')) {
            cur++;
        }
        return cur;
    };
    while(next != end) {
        auto start = skip_whitespace(next);
        next = get_next(start);
        auto size = next - start;
        if (size > 0) {
            result.push_back(value.substr(start, size));
        }
    }
    return result;
}

static std::string clean_path(std::string path) {
    for (auto& c: path) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

void CLI::parse(int argc, char ** argv) {
    argparse::ArgumentParser program("fckrman");
    program.add_argument("action")
            .help("action: list, bundles, chunks, download, json")
            .required()
            .action([](std::string const& value){
                if (value == "list" || value == "ls") {
                    return Action::List;
                }
                if (value == "bundles" || value == "bl") {
                    return Action::ListBundles;
                }
                if (value == "chunks" || value == "ch") {
                    return Action::ListChunks;
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
    program.add_argument("-v", "--verify")
            .help("Skip: verified chunks.")
            .default_value(false)
            .implicit_value(true);
    program.add_argument("-e", "--exist")
            .help("Skip: existing files.")
            .default_value(false)
            .implicit_value(true);
    program.add_argument("-n", "--nowrite")
            .help("Skip: writing files to disk.")
            .default_value(false)
            .implicit_value(true);
    program.add_argument("-l", "--lang")
            .help("Filter: language(none for international files).")
            .default_value(std::string{});
    program.add_argument("-p", "--path")
            .help("Filter: path with regex match.")
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
    program.add_argument("-o", "--output")
            .help("Directory: to store and verify files from.")
            .default_value(std::string("."));
    program.add_argument("-d", "--download")
            .help("Url: to download from.")
            .default_value(std::string(DEFAULT_URL));
    program.add_argument("-a", "--archive")
            .help("Directory: to use as cache archive for bundles.")
            .default_value(std::string{});
    program.add_argument("-m", "--mode")
            .help("Mode: of range downloading: full, one, multi.")
            .default_value(RangeMode::Multi)
            .action([](std::string const& value) -> RangeMode {
                if (value == "full") {
                    return RangeMode::Full;
                } else if (value == "one") {
                    return RangeMode::One;
                } else if (value == "multi") {
                    return RangeMode::Multi;
                } else {
                    throw std::runtime_error("Bad range download mode!");
                }
            });
    program.add_argument("-r", "--retry")
            .help("Number: of retrys for failed bundles.")
            .default_value(uint32_t{0})
            .action([](std::string const& value) -> uint32_t {
                auto result = std::stoul(value);
                return result;
            });
    program.add_argument("-c", "--connections")
            .default_value(uint32_t{64})
            .help("Number: of connections per downloaded file.")
            .action([](std::string const& value) -> uint32_t {
                auto result = std::stoul(value);
                if (result < 1) {
                    throw std::runtime_error("Minimum number of connections is 1!");
                }
                return result;
            });
    program.add_argument("--curl-verbose")
            .help("Curl: verbose logging.")
            .default_value(false)
            .implicit_value(true);
    program.add_argument("--curl-buffer")
            .help("Curl: buffer size in bytes [1024, 524288].")
            .default_value(long{0})
            .action([](std::string const& value) -> long {
                auto result = std::stoul(value);
                if (result < 1024 || result > 524288) {
                    throw std::runtime_error("Invalid curl buffer value!");
                }
                return result;
            });
    program.add_argument("--curl-proxy")
            .help("Curl: proxy.")
            .default_value(std::string{});
    program.add_argument("--curl-useragent")
            .help("Curl: user agent string.")
            .default_value(std::string{});
    program.add_argument("--curl-cookiefile")
            .help("Curl: cookie file or '-' to disable cookie engine.")
            .default_value(std::string{});
    program.add_argument("--curl-cookielist")
            .help("Curl: cookie list string.")
            .default_value(std::string{});

    program.parse_args(argc, argv);

    manifest = program.get<std::string>("manifest");
    action = program.get<Action>("action");
    verify = program.get<bool>("-v");
    exist = program.get<bool>("-e");
    nowrite = program.get<bool>("-n");
    langs = parse_list(program.get<std::string>("-l"));
    path = program.get<std::optional<std::regex>>("-p");
    upgrade = program.get<std::string>("-u");
    output = program.get<std::string>("-o");
    download.prefix = clean_path(program.get<std::string>("-d"));
    download.archive = clean_path(program.get<std::string>("-a"));
    download.range_mode = program.get<RangeMode>("-m");
    download.retry = program.get<uint32_t>("-r");
    download.connections = program.get<uint32_t>("-c");
    download.curl_verbose = program.get<bool>("--curl-verbose");
    download.curl_buffer = program.get<long>("--curl-buffer");
    download.curl_proxy = program.get<std::string>("--curl-proxy");
    download.curl_useragent = program.get<std::string>("--curl-useragent");
    download.curl_cookiefile = program.get<std::string>("--curl-cookiefile");
    download.curl_cookielist = program.get<std::string>("--curl-cookielist");

    auto protocol = download.prefix.substr(0, download.prefix.find_first_of(':'));
    rman_assert(!protocol.empty());
    if (protocol == "ftp" || protocol == "file") {
        if (download.range_mode == RangeMode::Multi) {
            download.range_mode = RangeMode::One;
        }
    } else if (protocol == "http" || protocol == "https") {
    } else {
        download.range_mode = RangeMode::Full;
    }

    if (!download.archive.empty()) {
        if (download.range_mode == RangeMode::Multi) {
            download.range_mode = RangeMode::One;
        }
        rman_rethrow(fs::create_directories(download.archive));
        rman_rethrow(fs::create_directories(fs::path(download.archive) / "bundles"));
        download.archive = fs::absolute(download.archive).generic_string();
    }
}
