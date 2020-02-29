cmake_minimum_required(VERSION 3.8)

project(fuckingmanifestdownloader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package (CURL COMPONENTS HTTP)

add_subdirectory(rman)

add_executable(rman2json src/main_rman2json.cpp)
target_link_libraries(rman2json PRIVATE rman)

add_executable(rman2csv src/main_rman2csv.cpp)
target_link_libraries(rman2csv PRIVATE rman)

if(CURL_FOUND)
    add_executable(rmandl src/main_rmandl.cpp)
    target_link_libraries(rmandl PRIVATE rman CURL::libcurl)
else()
    message(WARNING "No curl found, downloader will not be built")
endif()
