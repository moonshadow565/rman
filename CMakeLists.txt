cmake_minimum_required(VERSION 3.20)

project(rman LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(dep)

add_library(rlib STATIC
    lib/rlib/ar.hpp
    lib/rlib/ar.cpp
    lib/rlib/ar_fsb.cpp
    lib/rlib/ar_fsb5.cpp
    lib/rlib/ar_load.cpp
    lib/rlib/ar_mac.cpp
    lib/rlib/ar_pe.cpp
    lib/rlib/ar_wad.cpp
    lib/rlib/ar_wpk.cpp
    lib/rlib/ar_zip.cpp
    lib/rlib/common.hpp
    lib/rlib/common.cpp
    lib/rlib/iofile.cpp
    lib/rlib/iofile.hpp
    lib/rlib/rads_rls.cpp
    lib/rlib/rads_rls.hpp
    lib/rlib/rads_sln.cpp
    lib/rlib/rads_sln.hpp
    lib/rlib/rbundle.cpp
    lib/rlib/rbundle.hpp
    lib/rlib/rcache.hpp
    lib/rlib/rcache.cpp
    lib/rlib/rcdn.hpp
    lib/rlib/rcdn.cpp
    lib/rlib/rchunk.hpp
    lib/rlib/rchunk.cpp
    lib/rlib/rmanifest.cpp
    lib/rlib/rmanifest.hpp
)
target_include_directories(rlib PUBLIC lib/)
target_link_libraries(rlib PUBLIC argparse libcurl zstd digestpp fmt json miniz)

add_executable(rman-dl src/rman_dl.cpp)
target_link_libraries(rman-dl PRIVATE rlib)

add_executable(rman-ls src/rman_ls.cpp)
target_link_libraries(rman-ls PRIVATE rlib)

add_executable(rman-bl src/rman_bl.cpp)
target_link_libraries(rman-bl PRIVATE rlib)

add_executable(rman-make src/rman_make.cpp)
target_link_libraries(rman-make PRIVATE rlib)

add_executable(rman-rads src/rman_rads.cpp)
target_link_libraries(rman-rads PRIVATE rlib)

add_executable(rman-chk src/rman_chk.cpp)
target_link_libraries(rman-chk PRIVATE rlib)

add_executable(rbun-chk src/rbun_chk.cpp)
target_link_libraries(rbun-chk PRIVATE rlib)

add_executable(rbun-ex src/rbun_ex.cpp)
target_link_libraries(rbun-ex PRIVATE rlib)

add_executable(rbun-ls src/rbun_ls.cpp)
target_link_libraries(rbun-ls PRIVATE rlib)

add_executable(rbun-merge src/rbun_merge.cpp)
target_link_libraries(rbun-merge PRIVATE rlib)

add_executable(rbun-usage src/rbun_usage.cpp)
target_link_libraries(rbun-usage PRIVATE rlib)

