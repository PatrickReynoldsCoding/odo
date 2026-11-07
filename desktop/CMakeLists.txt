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
set(ODO_DESKTOP_VERSION "0.0.12")
# -DODO_TEST_VERSION=0.0.0: an older-numbered build, to try the updater with.
if(ODO_TEST_VERSION)
  set(ODO_DESKTOP_VERSION "${ODO_TEST_VERSION}")
endif()
add_executable(odo main.cpp draw.cpp update.cpp)
target_compile_definitions(odo PRIVATE ODO_DESKTOP_VERSION="${ODO_DESKTOP_VERSION}")
target_link_libraries(odo PRIVATE mpe_engine SDL3::SDL3)
target_compile_options(odo PRIVATE -Wall -Wextra)
