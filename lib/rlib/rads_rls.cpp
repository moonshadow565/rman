#include "rads_rls.hpp"

using namespace rlib;
using namespace rlib::rads;

struct RLS::Raw {
    static constexpr auto MAGIC = std::array{'R', 'L', 'S', 'M'};
    struct Version {
        std::array<std::uint8_t, 4> raw;
        operator std::string() const { return fmt::format("{}.{}.{}.{}", raw[3], raw[2], raw[1], raw[0]); }
    };
    struct Header {
        std::array<char, 4> magic;
        std::uint16_t version_major;
        std::uint16_t version_minor;
        std::uint32_t project_name;
        Version release_version;
    };
    struct Folder {
        std::uint32_t name;
        std::uint32_t folders_start;
        std::uint32_t folders_count;
        std::uint32_t files_start;
        std::uint32_t files_count;
    };
    struct File {
        std::uint32_t name;
        Version version;
        std::array<std::uint8_t, 16> checksum;
        std::uint32_t deploy_mode;
        std::uint32_t size_uncompressed;
        std::uint32_t size_compressed;
        std::uint32_t date_low;
        std::uint32_t date_hi;
    };

    Header header = {};
    std::size_t folder_count = {};
    std::vector<Folder> folders = {};
    std::size_t file_count = {};
    std::vector<File> files = {};
    std::size_t strings_count = {};
    std::size_t strings_size = {};
    std::vector<std::string_view> strings = {};
};

template <typename Into, typename T = Into>
    requires(std::is_convertible_v<T, Into>&& std::is_trivially_copyable_v<T>)
static inline auto read_x(std::span<char const>& src, Into& into, T tmp) noexcept -> bool {
    if (auto size = sizeof(T); src.size() >= size) [[likely]] {
        std::memcpy(&tmp, src.data(), size);
        src = src.subspan(size);
        into = static_cast<Into>(tmp);
        return true;
    }
    return false;
}

template <typename T>
    requires(std::is_trivially_copyable_v<T>)
static inline auto read_n(std::span<char const>& src, std::vector<T>& val, std::size_t n) noexcept -> bool {
    if (auto size = n * sizeof(T); src.size() >= size) [[likely]] {
        val.clear();
        val.resize(n);
        std::memcpy(val.data(), src.data(), size);
        src = src.subspan(size);
        return true;
    };
    return false;
}

auto RLS::read(std::span<char const> src) -> RLS {
    auto raw = Raw{};
    // Read raw
    {
        rlib_assert(read_x(src, raw.header, Raw::Header{}));
        rlib_assert(raw.header.magic == Raw::MAGIC);
        rlib_assert(read_x(src, raw.folder_count, std::uint32_t{}));
        rlib_assert(read_n(src, raw.folders, raw.folder_count));
        rlib_assert(read_x(src, raw.file_count, std::uint32_t{}));
        rlib_assert(read_n(src, raw.files, raw.file_count));
        rlib_assert(read_x(src, raw.strings_count, std::uint32_t{}));
        rlib_assert(read_x(src, raw.strings_size, std::uint32_t{}));
        rlib_assert(raw.strings_size >= raw.strings_count);
        rlib_assert(src.size() >= raw.strings_size);
        raw.strings = std::vector<std::string_view>(raw.strings_count);
        auto string_iter = std::string_view{src.data(), raw.strings_size};
        for (auto s = std::size_t{}; !string_iter.empty() && s != raw.strings_count; ++s) {
            std::tie(raw.strings[s], string_iter) = str_split(string_iter, '\0');
        }
    }
    auto rls = RLS{};
    // Convert to something usable
    {
        auto folder_parents = std::vector<std::size_t>(raw.folder_count);
        auto file_parents = std::vector<std::size_t>(raw.file_count);
        // Build parent folder lookup map
        for (auto p = std::size_t{}; p != raw.folder_count; ++p) {
            auto const& parent = raw.folders[p];
            rlib_assert(raw.strings.size() >= parent.name);
            rlib_assert(in_range(parent.folders_start, parent.folders_count, raw.folder_count));
            rlib_assert(in_range(parent.files_start, parent.files_count, raw.file_count));
            for (auto c = std::size_t{}; c != parent.folders_count; ++c) {
                rlib_rethrow(folder_parents.at(parent.folders_start + c) = p);
            }
            for (auto c = std::size_t{}; c != parent.files_count; ++c) {
                rlib_rethrow(file_parents.at(parent.files_start + c) = p);
            }
        }
        rls.name = rlib_rethrow(raw.strings.at(raw.header.project_name));
        rls.version = static_cast<std::string>(raw.header.release_version);
        rls.langs = "none";
        rls.files = std::vector<File>(raw.files.size());
        auto visited = std::vector<bool>(raw.file_count);
        for (auto f = std::size_t{}; f != raw.file_count; ++f) {
            auto const& raw_file = raw.files[f];
            auto& file = rls.files[f];
            rlib_rethrow(file.name = raw.strings.at(raw_file.name));
            file.version = raw_file.version;
            visited.clear();
            visited.resize(raw.file_count);
            for (auto p = file_parents[f]; p; p = folder_parents[p]) {
                rlib_assert(!visited[p]);
                visited[p] = true;
                file.name = std::string(raw.strings[raw.folders[p].name]) + "/" + file.name;
            }
        }
    }
    return rls;
}
