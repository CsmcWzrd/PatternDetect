cmake_minimum_required(VERSION 3.15)
project(PatternDetect VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PATTERNDETECT_ENABLE_ZLIB "Enable .gz log reading using zlib when available" ON)
option(PATTERNDETECT_ENABLE_OPENSSL "Enable TLS report destinations using OpenSSL" OFF)

add_executable(patterndetect
    src/main.cpp
)

target_include_directories(patterndetect PRIVATE src)

if(MSVC)
    target_compile_options(patterndetect PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(patterndetect PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(patterndetect PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(PATTERNDETECT_ENABLE_ZLIB)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_compile_definitions(patterndetect PRIVATE PATTERNDETECT_HAS_ZLIB=1)
        target_link_libraries(patterndetect PRIVATE ZLIB::ZLIB)
    endif()
endif()

if(PATTERNDETECT_ENABLE_OPENSSL)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(patterndetect PRIVATE PATTERNDETECT_HAS_OPENSSL=1)
    target_link_libraries(patterndetect PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
    target_link_libraries(patterndetect PRIVATE ws2_32)
endif()

install(TARGETS patterndetect RUNTIME DESTINATION bin)
