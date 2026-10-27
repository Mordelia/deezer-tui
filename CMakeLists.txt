cmake_minimum_required(VERSION 3.20)
project(deezer-tui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v7.0.3
  GIT_SHALLOW TRUE
)

set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_MODULES  OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ftxui)

add_executable(deezer-tui
  src/main.cpp
  src/api/account.cpp
  src/api/auth.cpp
  src/api/deezer.cpp
  src/api/http.cpp
  src/api/stream.cpp
  src/api/types.cpp
  src/app/config.cpp
  src/app/playback.cpp
  src/app/browser.cpp
  src/app/session.cpp
  src/app/state.cpp
  src/app/tui.cpp
  src/audio/byte_buffer.cpp
  src/audio/player.cpp
  src/cli/play.cpp
  src/cli/selftest.cpp
  src/cli/show_config.cpp
  src/crypto/blowfish.cpp
  src/mpris/server.cpp
)

target_include_directories(deezer-tui PRIVATE src)

target_compile_options(deezer-tui PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(deezer-tui PRIVATE
  ftxui::ftxui
  CURL::libcurl
  nlohmann_json::nlohmann_json
  OpenSSL::Crypto
  Threads::Threads
  PkgConfig::MPV
  PkgConfig::SYSTEMD
)
