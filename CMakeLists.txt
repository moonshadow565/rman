cmake_minimum_required(VERSION 3.8)

project(fckrman LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(FCKRMAN_CURL "Curl support" ON)
if (FCKRMAN_CURL)
    find_package (CURL COMPONENTS HTTP)
endif()
find_package (Threads REQUIRED)

add_subdirectory(zstd)
add_executable(fckrman
    include/argparse.hpp
    include/fltbf.hpp
    include/httplib.h
    include/json.hpp
    include/sha2.hpp
    src/cli.hpp
    src/cli.cpp
    src/download.hpp
    src/download.cpp
    src/file.hpp
    src/file.cpp
    src/error.hpp
    src/error.cpp
    src/manifest.hpp
    src/manifest.cpp
    src/main.cpp
)
target_link_libraries(fckrman PRIVATE zstd Threads::Threads)
target_include_directories(fckrman PRIVATE include/)
if (FCKRMAN_CURL)
    if(CURL_FOUND)
        target_link_libraries(fckrman PRIVATE CURL::libcurl)
        target_compile_definitions(fckrman PRIVATE -DFCKRMAN_CURL)
    endif()
endif()
