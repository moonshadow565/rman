cmake_minimum_required(VERSION 3.1)

project(httplib LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(httplib STATIC
    src/httplib.h
    src/httplib.cc
)
target_include_directories(httplib PUBLIC src)
