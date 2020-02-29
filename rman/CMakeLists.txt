cmake_minimum_required(VERSION 3.8)

project(rman LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(zstd)
add_library(rman
    src/json.hpp
    src/manifest.hpp
    src/manifest.cpp
    src/manifest_info.hpp
    src/manifest_info.cpp
)
target_include_directories(rman PUBLIC src/)
target_link_libraries(rman PUBLIC zstd)
