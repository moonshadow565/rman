#define FUSE_USE_VERSION 31
#include <errno.h>
#include <fuse3/fuse.h>
#include <fuse3/fuse_common.h>
#include <sys/stat.h>

#include <argparse.hpp>
#include <cinttypes>
#include <compare>
#include <cstring>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rdir.hpp>
#include <rlib/rfile.hpp>

#ifdef _WIN32
#    define O_RDONLY 0
#    define O_WRONLY 1
#    define O_RDWR 2
#    define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#    define stat fuse_stat
#    define off_t fuse_off_t
#else
#    include <fcntl.h>
#endif

using namespace rlib;

struct Main {
    struct CLI {
        std::string output = {};
        RCache::Options cache = {};
        std::vector<std::string> manifests = {};
        RFile::Match match = {};
    } cli = {};
    fuse_args fargs = {};
    std::unique_ptr<RCache> cache = {};
    std::unique_ptr<RDirEntry> root = {};

    auto parse_args(int argc, char **argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Mounts manifests.");

        // Common options
        program.add_argument("output").help("output directory to mount in.").required();
        program.add_argument("bundle").help("bundle file path.").default_value(std::string{""});
        program.add_argument("manifests").help("Manifest files to read from.").remaining().required();

        program.add_argument("--fuse-debug").help("FUSE debug").default_value(false).implicit_value(true);

        // Filter options
        program.add_argument("-l", "--filter-lang")
            .help("Filter by language(none for international files) with regex match.")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const &value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });
        program.add_argument("-p", "--filter-path")
            .help("Filter by path with regex match.")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const &value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });

        program.parse_args(argc, argv);

        cli.output = program.get<std::string>("output");
        cli.manifests = program.get<std::vector<std::string>>("manifests");
        cli.match.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.match.path = program.get<std::optional<std::regex>>("--filter-path");

        cli.cache = {
            .path = program.get<std::string>("bundle"),
            .readonly = true,
        };

        [this](auto... args) mutable {
            fargs.argc = sizeof...(args);
            fargs.argv = (char **)calloc(sizeof...(args) + 1, sizeof(char *));
            auto i = 0;
            (void)((fargs.argv[i++] = strdup(args)), ...);
        }(argv[0],
          program.get<bool>("--fuse-debug") ? "-d" : "-f",
          "-o",
          "auto_unmount",
          "-o",
          "noforget",
          cli.output.c_str());
    }

    auto run() -> void {
        root = std::make_unique<RDirEntry>();

        std::cerr << "Collecting input manifests ... " << std::endl;
        auto paths = collect_files(cli.manifests, {});

        std::cerr << "Parsing input manifests ... " << std::endl;
        auto builder = root->builder();
        for (auto const &p : paths) {
            RFile::read_file(p, [&, this](RFile &rfile) {
                if (cli.match(rfile)) {
                    builder(rfile);
                }
                return true;
            });
        }
        builder = nullptr;

        if (!cli.cache.path.empty()) {
            std::cerr << "Parsing bundle ... " << std::endl;
            cache = std::make_unique<RCache>(cli.cache);
        }

        std::cerr << "Mounted!" << std::endl;
    }
};

static Main main_ = {};

static auto get_stats(RDirEntry const *entry, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = (entry->is_dir() ? S_IFDIR : S_IFREG) | 0444;
    stbuf->st_nlink = entry->nlink();
    stbuf->st_size = entry->size();
}

static auto get_entry(const char *cpath, struct fuse_file_info const *fi) -> RDirEntry const * {
    if (fi && fi->fh) {
        return (RDirEntry const *)(void const *)(std::uintptr_t)fi->fh;
    }
    return main_.root->find(cpath + 1);
}

static void *impl_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void)conn;
    cfg->kernel_cache = 1;
    return NULL;
}

static int impl_getattr(const char *cpath, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    get_stats(entry, stbuf);
    return 0;
}

static int impl_opendir(const char *cpath, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (!entry->is_dir()) {
        return -ENOTDIR;
    }
    fi->fh = (std::uintptr_t)(void const *)entry;
    return 0;
}

static int impl_readdir(const char *cpath,
                        void *buf,
                        fuse_fill_dir_t filler,
                        off_t offset,
                        struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (!entry->is_dir()) {
        return -ENOTDIR;
    }
    auto children = entry->children();
    struct stat statbuf;
    for (auto i = offset; i < children.size(); ++i) {
        get_stats(&children[i], &statbuf);
        if (filler(buf, children[i].name().data(), &statbuf, i + 1, FUSE_FILL_DIR_PLUS)) {
            return 0;
        }
    }
    return 0;
}

static int impl_releasedir(const char *, struct fuse_file_info *) { return 0; }

static int impl_fsyncdir(const char *, int, struct fuse_file_info *) { return 0; }

static int impl_open(const char *cpath, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (entry->is_dir()) {
        return -EISDIR;
    }
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EROFS;
    }
    fi->keep_cache = 1;
    fi->fh = (std::uintptr_t)(void const *)entry;
    return 0;
}

static int impl_read(const char *cpath, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (entry->is_dir()) {
        return -EISDIR;
    }
    if (!main_.cache) {
        return -EIO;
    }
    auto real_size = entry->size();
    if (offset >= real_size) {
        return 0;
    }
    if (offset + size > real_size) {
        size = real_size - offset;
    }
    thread_local struct Last {
        ChunkID id = {};
        Buffer buffer = {};
    } last = {};
    auto done = std::size_t{};
    for (RChunk::Dst const &chunk : entry->chunks(offset, size)) {
        if (fuse_interrupted()) {
            return -EINTR;
        }
        if (chunk.uncompressed_offset == offset + done && size - done >= chunk.uncompressed_size) {
            if (!main_.cache->get_into(chunk, {buf + done, chunk.uncompressed_size})) {
                return -EIO;
            }
            done += chunk.uncompressed_size;
            continue;
        }
        if (last.id != chunk.chunkId) {
            last.id = ChunkID::None;
            if (!last.buffer.resize_destroy(chunk.uncompressed_size)) {
                return -ENOMEM;
            }
            if (!main_.cache->get_into(chunk, last.buffer)) {
                return -EIO;
            }
            last.id = chunk.chunkId;
        }
        auto src = std::span<char const>(last.buffer);
        if (auto const pos = (offset + done); pos > chunk.uncompressed_offset) {
            src = src.subspan(pos - chunk.uncompressed_offset);
        }
        if (auto const remain = size - done; remain < src.size()) {
            src = src.subspan(0, remain);
        }
        std::memcpy(buf + done, src.data(), src.size());
        done += src.size();
    }
    return done;
}

static int impl_flush(const char *, struct fuse_file_info *) { return 0; }

static int impl_release(const char *, struct fuse_file_info *) { return 0; }

static int impl_fsync(const char *, int, struct fuse_file_info *) { return 0; }

static const struct fuse_operations impl_oper = {
    .getattr = impl_getattr,

    .open = impl_open,
    .read = impl_read,
    .flush = impl_flush,
    .release = impl_release,
    .fsync = impl_fsync,

    .opendir = impl_opendir,
    .readdir = impl_readdir,
    .releasedir = impl_releasedir,
    .fsyncdir = impl_fsyncdir,

    .init = impl_init,
};

int main(int argc, char *argv[]) {
    try {
        main_.parse_args(argc, argv);
        main_.run();
    } catch (std::exception const &e) {
        std::cerr << e.what() << std::endl;
        for (auto const &error : error_stack()) {
            std::cerr << error << std::endl;
        }
        error_stack().clear();
        return EXIT_FAILURE;
    }
    return fuse_main(main_.fargs.argc, main_.fargs.argv, &impl_oper, NULL);
}
