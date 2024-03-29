#include <miniz.h>

#include "ar.hpp"

using namespace rlib;

static mz_bool mz_zip_reader_file_offset(mz_zip_archive *pZip, const mz_zip_archive_file_stat *pStat, mz_uint64 *pOff) {
#define MZ_ZIP_LOCAL_DIR_HEADER_SIG 0x04034b50
#define MZ_ZIP_LOCAL_DIR_HEADER_SIZE 30
#define MZ_ZIP_LDH_FILENAME_LEN_OFS 26
#define MZ_ZIP_LDH_EXTRA_LEN_OFS 28
    mz_uint32 local_header_u32[(MZ_ZIP_LOCAL_DIR_HEADER_SIZE + sizeof(mz_uint32) - 1) / sizeof(mz_uint32)];
    mz_uint8 *pLocal_header = (mz_uint8 *)local_header_u32;

    mz_uint64 cur_file_ofs = pStat->m_local_header_ofs;
    if (pZip->m_pRead(pZip->m_pIO_opaque, cur_file_ofs, pLocal_header, MZ_ZIP_LOCAL_DIR_HEADER_SIZE) !=
        MZ_ZIP_LOCAL_DIR_HEADER_SIZE) {
        pZip->m_last_error = MZ_ZIP_FILE_READ_FAILED;
        return MZ_FALSE;
    }

    if (MZ_READ_LE32(pLocal_header) != MZ_ZIP_LOCAL_DIR_HEADER_SIG) {
        pZip->m_last_error = MZ_ZIP_INVALID_HEADER_OR_CORRUPTED;
        return MZ_FALSE;
    }

    cur_file_ofs += MZ_ZIP_LOCAL_DIR_HEADER_SIZE + MZ_READ_LE16(pLocal_header + MZ_ZIP_LDH_FILENAME_LEN_OFS) +
                    MZ_READ_LE16(pLocal_header + MZ_ZIP_LDH_EXTRA_LEN_OFS);

    if ((cur_file_ofs + pStat->m_comp_size) > pZip->m_archive_size) {
        pZip->m_last_error = MZ_ZIP_INVALID_HEADER_OR_CORRUPTED;
        return MZ_FALSE;
    }

    *pOff = cur_file_ofs;
    return MZ_TRUE;
#undef MZ_ZIP_LOCAL_DIR_HEADER_SIG
#undef MZ_ZIP_LOCAL_DIR_HEADER_SIZE
#undef MZ_ZIP_LDH_FILENAME_LEN_OFS
#undef MZ_ZIP_LDH_EXTRA_LEN_OFS
}

struct Ar::ZIP : public mz_zip_archive {
    static constexpr auto FLAGS = MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY;
    static constexpr auto SIGS = std::array{0x04034b50u, 0x02014b50u};
    ZIP(IO const &io)
        : mz_zip_archive{
              .m_pRead = [](void *pOpaque, mz_uint64 file_ofs, void *pBuf, size_t n) -> size_t {
                  auto const &io = *(IO const *)pOpaque;
                  auto const size = io.size();
                  file_ofs = std::min((std::size_t)file_ofs, size);
                  n = std::min(size - (std::size_t)file_ofs, n);
                  if (!io.read((std::size_t)file_ofs, {(char *)pBuf, n})) return 0;
                  return n;
              },
              .m_pIO_opaque = (void *)(void const *)&io,
          } {}
    ZIP(ZIP const &) = delete;
    ~ZIP() { mz_zip_end(this); }
};

auto Ar::process_try_zip(IO const &io, offset_cb cb, Entry const &top_entry) const -> bool {
    // only consider whole files for .zip spliting
    if (top_entry.offset != 0 || top_entry.size != io.size() || top_entry.size < 22) return false;

    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);

    // check if the file is actually zip
    auto sig = std::uint32_t{};
    if (!reader.read(sig) || (sig != ZIP::SIGS[0] && sig != ZIP::SIGS[1])) return false;

    auto zip = ZIP(io);
    rlib_ar_assert(mz_zip_reader_init(&zip, top_entry.size, ZIP::FLAGS));

    auto entries = std::vector<Entry>((std::size_t)zip.m_total_files);
    for (std::size_t i = 0; i != zip.m_total_files; ++i) {
        auto stat = mz_zip_archive_file_stat{};
        rlib_ar_assert(mz_zip_reader_file_stat(&zip, i, &stat));

        auto offset = std::uint64_t{};
        rlib_ar_assert(mz_zip_reader_file_offset(&zip, &stat, &offset));
        rlib_ar_assert(reader.contains(offset, stat.m_comp_size));

        entries[i] = Entry{
            .offset = top_entry.offset + offset,
            .size = stat.m_comp_size,
            .high_entropy = stat.m_method != 0,
            .nest = stat.m_method == 0,
        };
    }

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}
