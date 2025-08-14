cmake_minimum_required(VERSION 3.20)

# This is need on some linuxes because threading is "optional"
find_package (Threads REQUIRED)

# Manage all our dependencies with cmake and git
include(FetchContent)

# Fetch curl library
# Disable most of useless protocols, doc generation, utilities and tests
FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-7_83_1
)
FetchContent_GetProperties(curl)
if(NOT curl_POPULATED)
    FetchContent_Populate(curl)
    if (WIN32)
        set(curl_CURL_USE_SCHANNEL ON CACHE INTERNAL "Dont need")
    endif()
    set(BUILD_CURL_EXE OFF CACHE INTERNAL "Dont need")
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Dont need")
    set(BUILD_TESTING OFF CACHE INTERNAL "Dont need")
    set(USE_ZLIB OFF CACHE INTERNAL "Dont need")
    set(CURL_USE_LIBSSH OFF CACHE INTERNAL "Dont need")
    set(CURL_USE_LIBSSH2 OFF CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_DICT ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_GOPHER ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_IMAP ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_LDAP ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_LDAPS ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_MQTT ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_POP3 ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_RTSP ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_SMTP ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_TELNET ON CACHE INTERNAL "Dont need")
    set(CURL_DISABLE_TFTP ON CACHE INTERNAL "Dont need")
    add_subdirectory(${curl_SOURCE_DIR} ${curl_BINARY_DIR})
endif()

# zstd has to be built manually since it doesn't have cmake support
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG        v1.5.2
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_GetProperties(zstd)
if(NOT zstd_POPULATED)
    FetchContent_Populate(zstd)
    file(GLOB zstd_SRCS
        ${zstd_SOURCE_DIR}/lib/common/*.c
        ${zstd_SOURCE_DIR}/lib/compress/*.c
        ${zstd_SOURCE_DIR}/lib/decompress/*.c
    )
    add_library(zstd STATIC ${zstd_SRCS})
    target_include_directories(zstd PUBLIC ${zstd_SOURCE_DIR}/lib)
    target_include_directories(zstd PRIVATE ${zstd_SOURCE_DIR}/lib/common)
    target_compile_definitions(zstd PRIVATE -DZSTD_DISABLE_ASM)
    target_compile_definitions(zstd PUBLIC -DXXH_STATIC_LINKING_ONLY)
    target_compile_definitions(zstd INTERFACE -DZSTD_STATIC_LINKING_ONLY)
    if(MSVC)
        target_compile_options(zstd PRIVATE /wd4267)
    endif()
endif()

# argparse is header only library, just include it
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
    GIT_TAG        997da9255618311d1fcb0135ce86022729d1f1cb #v2.9
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_GetProperties(argparse)
if(NOT argparse_POPULATED)
    FetchContent_Populate(argparse)
    add_library(argparse INTERFACE)
    target_include_directories(argparse INTERFACE ${argparse_SOURCE_DIR}/include/argparse)
endif()

FetchContent_Declare(
    digestpp
    GIT_REPOSITORY https://github.com/kerukuro/digestpp.git
    GIT_TAG        34ff2eeae397ed744d972d86b5a20f603b029fbd
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_GetProperties(digestpp)
if(NOT digestpp_POPULATED)
    FetchContent_Populate(digestpp)
    add_library(digestpp INTERFACE)
    target_include_directories(digestpp INTERFACE ${digestpp_SOURCE_DIR})
endif()

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        8.1.1
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(
    json_struct
    URL https://github.com/jorgen/json_struct/archive/de80d452b3cc1688dfc2967dbfef5cb501e925d3.zip
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
)
FetchContent_GetProperties(json_struct)
if(NOT json_struct_POPULATED)
    FetchContent_Populate(json_struct)
    add_library(json_struct INTERFACE)
    target_include_directories(json_struct INTERFACE ${json_struct_SOURCE_DIR}/include)
endif()

FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        293d4db1b7d0ffee9756d035b9ac6f7431ef8492
)

FetchContent_GetProperties(miniz)
if(NOT miniz_POPULATED)
    FetchContent_Populate(miniz)
    add_subdirectory(${miniz_SOURCE_DIR} ${miniz_BINARY_DIR})
    target_compile_definitions(miniz PUBLIC
        -DMINIZ_NO_DEFLATE_APIS=1
        -DMINIZ_NO_ZLIB_APIS=1
        -DMINIZ_NO_TIME=1
        -DMINIZ_NO_STDIO=1
        -DMINIZ_DISABLE_ZIP_READER_CRC32_CHECKS=1
    )
endif()

FetchContent_Declare(
    blake3
    GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
    GIT_TAG        df610ddc3b93841ffc59a87e3da659a15910eb46
)
FetchContent_GetProperties(blake3)
if(NOT blake3_POPULATED)
    FetchContent_Populate(blake3)
    add_subdirectory(${blake3_SOURCE_DIR}/c ${blake3_BINARY_DIR})
endif()
