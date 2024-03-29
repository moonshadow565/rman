#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcdn.hpp>
#include <rlib/rfile.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string manifest = {};
        std::string updatefrommanfiest = {};
        std::string output = {};
        bool no_verify = {};
        bool no_write = {};
        bool no_progress = {};
        RFile::Match match = {};
        RCache::Options cache = {};
        RCDN::Options cdn = {};
    } cli = {};
    std::unique_ptr<RCache> cache = {};
    std::unique_ptr<RCDN> cdn = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Downloads or repairs files in manifest.");
        // Common options
        program.add_argument("manifest").help("Manifest file to read from.").required();
        program.add_argument("output")
            .help("Output directory to store and verify files from.")
            .default_value(std::string("."));
        program.add_argument("-l", "--filter-lang")
            .help("Filter by language(none for international files) with regex match.")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const& value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });
        program.add_argument("-p", "--filter-path")
            .help("Filter by path with regex match.")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const& value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });
        program.add_argument("-u", "--update")
            .help("Filter: update from old manifest.")
            .default_value(std::string(""));
        program.add_argument("--no-verify")
            .help("Force force full without verify.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-write").help("Do not write to file.").default_value(false).implicit_value(true);
        program.add_argument("--no-progress").help("Do not print progress.").default_value(false).implicit_value(true);

        // Cache options
        program.add_argument("--cache").help("Cache file path.").default_value(std::string{""});
        program.add_argument("--cache-readonly")
            .help("Do not write to cache.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cache-newonly")
            .help("Force create new part regardless of size.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cache-buffer")
            .help("Size for cache buffer in megabytes [1, 4096]")
            .default_value(std::uint32_t{32})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--cache-limit")
            .help("Size for cache bundle limit in gigabytes [0, 4096]")
            .default_value(std::uint32_t{4})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 4096u);
            });

        // CDN options
        program.add_argument("--cdn")
            .help("Source url to download files from.")
            .default_value(std::string("http://lol.secure.dyn.riotcdn.net/channels/public"));
        program.add_argument("--cdn-lowspeed-time")
            .help("Curl seconds that the transfer speed should be below.")
            .default_value(std::size_t{0})
            .action([](std::string const& value) -> std::size_t { return (std::size_t)std::stoul(value); });
        program.add_argument("--cdn-lowspeed-limit")
            .help("Curl average transfer speed in killobytes per second that the transfer should be above.")
            .default_value(std::size_t{64})
            .action([](std::string const& value) -> std::size_t { return (std::size_t)std::stoul(value); });
        program.add_argument("--cdn-retry")
            .help("Number of retries to download from url.")
            .default_value(std::uint32_t{3})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 8u);
            });
        program.add_argument("--cdn-workers")
            .default_value(std::uint32_t{32})
            .help("Number of connections per downloaded file.")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 64u);
            });
        program.add_argument("--cdn-interval")
            .help("Curl poll interval in miliseconds.")
            .default_value(int{100})
            .action([](std::string const& value) -> int { return std::clamp((int)std::stoul(value), 0, 30000); });
        program.add_argument("--cdn-verbose").help("Curl: verbose logging.").default_value(false).implicit_value(true);
        program.add_argument("--cdn-buffer")
            .help("Curl buffer size in killobytes [1, 512].")
            .default_value(long{512})
            .action(
                [](std::string const& value) -> long { return std::clamp((long)std::stoul(value), 1l, 512l) * 1024; });
        program.add_argument("--cdn-proxy").help("Curl: proxy.").default_value(std::string{});
        program.add_argument("--cdn-useragent").help("Curl: user agent string.").default_value(std::string{});
        program.add_argument("--cdn-cookiefile")
            .help("Curl cookie file or '-' to disable cookie engine.")
            .default_value(std::string{});
        program.add_argument("--cdn-cookielist").help("Curl: cookie list string.").default_value(std::string{});

        program.parse_args(argc, argv);

        cli.manifest = program.get<std::string>("manifest");
        cli.output = program.get<std::string>("output");
        cli.updatefrommanfiest = program.get<std::string>("--update");

        cli.no_verify = program.get<bool>("--no-verify");
        cli.no_write = program.get<bool>("--no-write");
        cli.no_progress = program.get<bool>("--no-progress");
        cli.match.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.match.path = program.get<std::optional<std::regex>>("--filter-path");

        cli.cache = {
            .path = program.get<std::string>("--cache"),
            .readonly = program.get<bool>("--cache-readonly"),
            .newonly = program.get<bool>("--cache-newonly"),
            .flush_size = program.get<std::uint32_t>("--cache-buffer") * MiB,
            .max_size = program.get<std::uint32_t>("--cache-limit") * GiB,
        };

        cli.cdn = {
            .url = clean_path(program.get<std::string>("--cdn")),
            .verbose = program.get<bool>("--cdn-verbose"),
            .buffer = program.get<long>("--cdn-buffer"),
            .interval = program.get<int>("--cdn-interval"),
            .retry = program.get<std::uint32_t>("--cdn-retry"),
            .workers = program.get<std::uint32_t>("--cdn-workers"),
            .proxy = program.get<std::string>("--cdn-proxy"),
            .useragent = program.get<std::string>("--cdn-useragent"),
            .cookiefile = program.get<std::string>("--cdn-cookiefile"),
            .cookielist = program.get<std::string>("--cdn-cookielist"),
            .low_speed_limit = program.get<std::size_t>("--cdn-lowspeed-limit") * KiB,
            .low_speed_time = program.get<std::size_t>("--cdn-lowspeed-time"),
        };
    }

    auto run() -> void {
        rlib_trace("Manifest file: %s", cli.manifest.c_str());
        if (!RFile::has_known_bundle(cli.manifest)) {
            cli.cdn.url.clear();
        }

        if (cli.cdn.url.empty()) {
            cli.cache.readonly = true;
        }

        if (!cli.no_write) {
            fs::create_directories(cli.output);
        }

        if (!cli.cache.path.empty()) {
            cache = std::make_unique<RCache>(cli.cache);
        }

        cdn = std::make_unique<RCDN>(cli.cdn, cache.get());

        auto skipids = std::unordered_map<std::string, FileID>{};
        if (!cli.updatefrommanfiest.empty()) {
            rlib_trace("Update from file: %s", cli.updatefrommanfiest.c_str());
            RFile::read_file(cli.updatefrommanfiest, [&, this](RFile const& rfile) {
                if (cli.match(rfile)) {
                    skipids[rfile.path] = rfile.fileId;
                }
                return true;
            });
        }

        auto files = std::vector<RFile>{};
        RFile::read_file(cli.manifest, [&, this](RFile& rfile) {
            if (auto i = skipids.find(rfile.path); i != skipids.cend() &&  i->second == rfile.fileId) {
                return true;
            }
            if (cli.match(rfile)) {
                files.emplace_back(std::move(rfile));
            }
            return true;
        });
        for (std::uint32_t index = files.size(); auto const& rfile : files) {
            download_file(rfile, index--);
        }
    }

    auto download_file(RFile const& rfile, std::uint32_t index) -> void {
        std::cout << "START: " << rfile.path << std::endl;
        auto path = fs::path(cli.output) / rfile.path;
        rlib_trace("Path: %s", path.generic_string().c_str());
        auto done = std::uint64_t{};
        auto bad_chunks = std::vector<RChunk::Dst>{};

        if (!rfile.chunks) {
            if (rfile.size) {
                rlib_assert(cache.get());
                bad_chunks = cache->get_chunks(rfile.fileId);
                rlib_assert(!bad_chunks.empty());
            }
        } else {
            bad_chunks = *rfile.chunks;
        }

        if (!cli.no_verify && !bad_chunks.empty()) {
            progress_bar p("VERIFIED", cli.no_progress, index, done, rfile.size);
            RChunk::Dst::verify(path, bad_chunks, [&](RChunk::Dst const& chunk, std::span<char const> data) {
                done += chunk.uncompressed_size;
                p.update(done);
            });
        }

        auto outfile = std::unique_ptr<IO::File>();
        if (!cli.no_write) {
            outfile = std::make_unique<IO::File>(path, IO::WRITE);
            rlib_assert(outfile->resize(0, rfile.size));
        }

        if (!bad_chunks.empty() && cdn) {
            progress_bar p("DOWNLOAD", cli.no_progress, index, done, rfile.size);
            bad_chunks = cdn->get(std::move(bad_chunks), [&](RChunk::Dst const& chunk, std::span<char const> data) {
                if (outfile) {
                    rlib_assert(outfile->write(chunk.uncompressed_offset, data));
                }
                done += chunk.uncompressed_size;
                p.update(done);
            });
        }

        if (!bad_chunks.empty()) {
            std::cout << "FAIL!" << std::endl;
        } else {
            if (outfile) {
                outfile = nullptr;
                if (rfile.permissions & 01) {
                    fs::permissions(path,
                                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                                    fs::perm_options::add);
                }
            }
            std::cout << "OK!" << std::endl;
        }
    }
};

int main(int argc, char** argv) {
    auto main = Main{};
    try {
        main.parse_args(argc, argv);
        main.run();
    } catch (std::exception const& e) {
        std::cerr << e.what() << std::endl;
        for (auto const& error : error_stack()) {
            std::cerr << error << std::endl;
        }
        error_stack().clear();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
