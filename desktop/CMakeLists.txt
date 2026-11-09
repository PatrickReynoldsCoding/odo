cmake_minimum_required(VERSION 3.19)
project(odo_desktop C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The engine, with no instruments compiled in: they come from packages, compiled by libfaust.
set(MPE_BUILTIN_INSTRUMENTS OFF CACHE STRING "" FORCE)
set(MPE_LIBFAUST ON CACHE BOOL "" FORCE)
set(MPE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(../engine engine)

find_package(SDL3 REQUIRED CONFIG)
# odo for computers' own version (its releases are tagged desktop-v<version>).
set(ODO_DESKTOP_VERSION "0.0.32")
# -DODO_TEST_VERSION=0.0.0: an older-numbered build, to try the updater with.
if(ODO_TEST_VERSION)
  set(ODO_DESKTOP_VERSION "${ODO_TEST_VERSION}")
endif()
# The fonts (ui/fonts, the same files odo on Android uses), carried in the binary.
set(ODO_FONTS Roboto-Regular Roboto-Bold NotoSansSymbols-Regular-Subset NotoSansSymbols-Bold-Subset)
set(ODO_FONT_FILES "")
foreach(f ${ODO_FONTS})
  list(APPEND ODO_FONT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../ui/fonts/${f}.ttf")
endforeach()
string(REPLACE "-" "_" ODO_FONT_NAMES "${ODO_FONTS}")
# Lists go to the script joined by | (a ; would split the command).
string(REPLACE ";" "|" ODO_FONT_FILES_ARG "${ODO_FONT_FILES}")
string(REPLACE ";" "|" ODO_FONT_NAMES_ARG "${ODO_FONT_NAMES}")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fonts.cpp"
  COMMAND "${CMAKE_COMMAND}" -DOUT=${CMAKE_CURRENT_BINARY_DIR}/fonts.cpp "-DFILES=${ODO_FONT_FILES_ARG}" "-DNAMES=${ODO_FONT_NAMES_ARG}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake"
  DEPENDS ${ODO_FONT_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake"
  VERBATIM)
# stb_truetype (public domain, desktop/third_party), built on its own so its warnings stay out of ours.
add_library(odo_stb STATIC stb_truetype.cpp)
target_include_directories(odo_stb PUBLIC third_party)
target_compile_options(odo_stb PRIVATE -w)

add_executable(odo main.cpp draw.cpp update.cpp gestures.cpp "${CMAKE_CURRENT_BINARY_DIR}/fonts.cpp")
target_compile_definitions(odo PRIVATE ODO_DESKTOP_VERSION="${ODO_DESKTOP_VERSION}")
target_link_libraries(odo PRIVATE mpe_engine odo_stb SDL3::SDL3)
target_compile_options(odo PRIVATE -Wall -Wextra)
