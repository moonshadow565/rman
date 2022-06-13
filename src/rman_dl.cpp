#include <argparse.hpp>
#include <iomanip>
#include <iostream>
#include <rlib/error.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcdn.hpp>
#include <rlib/rmanifest.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string manifest = {};
        std::string output = {};
        bool no_verify = {};
        bool no_write = {};
        bool no_progress = {};
        std::uint32_t retry = {};
        RMAN::Filter filter = {};
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
        program.add_argument("--cache-buffer")
            .help("Size for cache buffer in killobytes [64, 1048576]")
            .default_value(std::uint32_t{32 * 1024 * 1024})
            .scan<'u', std::uint32_t>()
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 64u, 1024u * 1024) * 1024u;
            });

        // CDN options
        program.add_argument("--cdn")
            .help("Source url to download files from.")
            .default_value(std::string("http://lol.secure.dyn.riotcdn.net/channels/public"));
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

        cli.no_verify = program.get<bool>("--no-verify");
        cli.no_write = program.get<bool>("--no-write");
        cli.no_progress = program.get<bool>("--no-progress");
        cli.filter.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.filter.path = program.get<std::optional<std::regex>>("--filter-path");

        cli.cache = {
            .path = program.get<std::string>("--cache"),
            .readonly = program.get<bool>("--cache-readonly"),
            .flush_size = program.get<std::uint32_t>("--cache-buffer"),
        };

        cli.retry = program.get<std::uint32_t>("--cdn-retry");
        cli.cdn = {
            .url = clean_path(program.get<std::string>("--cdn")),
            .verbose = program.get<bool>("--cdn-verbose"),
            .buffer = program.get<long>("--cdn-buffer"),
            .workers = program.get<std::uint32_t>("--cdn-workers"),
            .proxy = program.get<std::string>("--cdn-proxy"),
            .useragent = program.get<std::string>("--cdn-useragent"),
            .cookiefile = program.get<std::string>("--cdn-cookiefile"),
            .cookielist = program.get<std::string>("--cdn-cookielist"),
        };
    }

    auto run() -> void {
        rlib_trace("Manifest file: %s", cli.manifest.c_str());
        auto infile = IOFile(cli.manifest, false);
        auto manifest = RMAN::read(infile.copy(0, infile.size()));

        if (!cli.no_write) {
            fs::create_directories(cli.output);
        }

        if (!cli.cache.path.empty()) {
            if (cli.cdn.url.empty()) {
                cli.cache.readonly = false;
            }
            cache = std::make_unique<RCache>(cli.cache);
        }

        if (!cli.cdn.url.empty()) {
            cdn = std::make_unique<RCDN>(cli.cdn, cache.get());
        }

        for (auto const& rfile : manifest.files) {
            if (!rfile.matches(cli.filter)) continue;
            std::cout << "START: " << rfile.path << std::endl;
            if (download_file(rfile)) {
                std::cout << "OK: " << rfile.path << std::endl;
            } else {
                std::cout << "FAIL: " << rfile.path << std::endl;
            }
        }
    }

    auto download_file(RMAN::File const& rfile) -> bool {
        rlib_trace("Path: %s", rfile.path.c_str());
        auto bad_chunks = rfile.verify(fs::path(cli.output) / rfile.path, cli.no_verify);
        if (!bad_chunks) {
            return true;
        }

        auto outfile = IOFile();
        if (!cli.no_write) {
            outfile = IOFile(fs::path(cli.output) / rfile.path, true);
            rlib_assert(outfile.resize(0, rfile.size));
        }

        auto done = std::uint64_t{};
        auto total = std::uint64_t{};
        for (auto const& chunk : *bad_chunks) total += chunk.uncompressed_size;

        if (!bad_chunks->empty() && cache) {
            auto yield_func = [&] { std::cout << progress("\rUNCACHED", 0, done, total) << std::flush; };
            yield_func();
            bad_chunks = cache->run(
                std::move(*bad_chunks),
                [&](RBUN::ChunkDst const& chunk, std::span<char const> data) {
                    if (outfile) {
                        rlib_assert(outfile.write(chunk.uncompressed_offset, data));
                    }
                    done += chunk.uncompressed_size;
                },
                cli.no_progress ? RCache::yield_cb() : yield_func);
        }

        for (std::uint32_t retry = 1; !bad_chunks->empty() && cdn && retry <= cli.retry; ++retry) {
            auto yield_func = [&] { std::cout << progress("\rDOWNLOAD", retry, done, total) << std::flush; };
            yield_func();
            bad_chunks = cdn->run(
                std::move(*bad_chunks),
                [&](RBUN::ChunkDst const& chunk, std::span<char const> data) {
                    if (outfile) {
                        rlib_assert(outfile.write(chunk.uncompressed_offset, data));
                    }
                    done += chunk.uncompressed_size;
                },
                cli.no_progress ? RCDN::yield_cb() : yield_func);
        }

        std::cout << "\n";

        return bad_chunks->empty();
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