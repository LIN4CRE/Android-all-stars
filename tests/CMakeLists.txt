# Host tests for SMAS Mobile: the platform-independent part of the app (the
# emulation core, the game modules, the application, saves, the game-file
# check, backups, the on-screen controls) built for the machine you are on and
# run headless, with a stand-in for Android (tests/host/fake_platform.cpp).
#
#   cmake -S tests -B out/tests -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build out/tests
#   ctest --test-dir out/tests --output-on-failure
#
# No game data is needed or used: where a test needs "a game file" it makes a
# 2 MiB buffer in memory holding only a cartridge header's title.
cmake_minimum_required(VERSION 3.18)
project(SmasMobileTests CXX)

get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
set(SMAS_SRC ${ROOT}/src)
include(${SMAS_SRC}/sources.cmake)

# The app's version (the same file the Android build reads).
file(STRINGS ${ROOT}/version.properties VERSION_LINES)
foreach(line ${VERSION_LINES})
  if(line MATCHES "^versionName=(.*)$")
    set(SMAS_VERSION_NAME ${CMAKE_MATCH_1})
  endif()
endforeach()
if(NOT SMAS_VERSION_NAME)
  message(FATAL_ERROR "versionName not found in version.properties")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(SMAS_SANITIZE "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SMAS_TSAN "Build the tests with ThreadSanitizer" OFF)

add_library(smas_app STATIC ${SMAS_CORE_SOURCES} ${SMAS_APP_SOURCES} host/fake_platform.cpp)
target_include_directories(smas_app PUBLIC ${SMAS_SRC} ${ROOT}/third_party/stb ${CMAKE_CURRENT_SOURCE_DIR}/host)
target_compile_definitions(smas_app PUBLIC SMAS_VERSION_NAME="${SMAS_VERSION_NAME}")
# (the same warnings the Android build compiles with, plus more)
target_compile_options(smas_app PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
find_package(Threads REQUIRED)
target_link_libraries(smas_app PUBLIC Threads::Threads)
if(SMAS_SANITIZE)
  target_compile_options(smas_app PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  target_link_options(smas_app PUBLIC -fsanitize=address,undefined)
endif()
if(SMAS_TSAN)
  target_compile_options(smas_app PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(smas_app PUBLIC -fsanitize=thread)
endif()

add_executable(smas_tests
  host/check_main.cpp
  test_fileio.cpp
  test_savestore.cpp
  test_romcheck.cpp
  test_zip_backup.cpp
  test_touch.cpp
)
target_link_libraries(smas_tests PRIVATE smas_app)

enable_testing()
add_test(NAME smas_tests COMMAND smas_tests)

# The preview tool: draws the hub, the menus and the on-screen controls with a
# software canvas and saves them as pictures (see preview_main.cpp), for
# looking at layout without a phone. Off by default; it needs two TrueType
# fonts on the host (DejaVu Sans by default).
#
#   cmake -S tests -B out/tests -DSMAS_PREVIEW=ON && cmake --build out/tests --target smas_preview
#   out/tests/smas_preview out/preview && python3 tools/ppm2png.py out/preview
option(SMAS_PREVIEW "Build the preview tool (pictures of the app's screens, drawn on the host)" OFF)
if(SMAS_PREVIEW)
  add_executable(smas_preview preview_main.cpp host/soft_canvas.cpp)
  target_compile_definitions(smas_preview PRIVATE SMAS_REPO_DIR="${ROOT}")
  target_link_libraries(smas_preview PRIVATE smas_app)
endif()
