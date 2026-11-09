find_package(Threads REQUIRED)
foreach(t test_clean test_engine test_replay test_threads test_perform test_phone test_looper test_sounds test_preset test_mix)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${t}.cpp)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} mpe_engine Threads::Threads)
    add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
endforeach()

# The Hex keyboard's tunings and layouts (controllers/hex/tuning.h), its maths alone.
add_executable(test_hex test_hex.cpp)
target_link_libraries(test_hex mpe_engine Threads::Threads)
add_test(NAME test_hex COMMAND test_hex WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Scala tunings (mpe/scala.h): .scl files, as a controller's settings, on the rail and in the Hex keyboard.
add_executable(test_scala test_scala.cpp)
target_link_libraries(test_scala mpe_engine Threads::Threads)
target_compile_definitions(test_scala PRIVATE MPE_TEST_CONTROLLER_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}/../../controllers")
add_test(NAME test_scala COMMAND test_scala WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# The rail's settings model, against the controllers' own manifests (no build of them needed).
add_executable(test_rail test_rail.cpp)
target_link_libraries(test_rail mpe_engine Threads::Threads)
target_compile_definitions(test_rail PRIVATE MPE_TEST_CONTROLLER_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}/../../controllers")
add_test(NAME test_rail COMMAND test_rail WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Packages: needs the instrument packages (made at configure time) and the controllers, built
# here with clang's WebAssembly target. Without clang, this test is left out.
find_program(CLANG clang)
if(MPE_BUILTIN_INSTRUMENTS AND NOT MPE_BUILTIN_INSTRUMENTS STREQUAL "SOURCES" AND CLANG)
  set(ctl "${CMAKE_CURRENT_SOURCE_DIR}/../../controllers")
  file(GLOB ctl_sources "${ctl}/*/*.c" "${ctl}/*/controller.json" "${ctl}/include/*.h" "${ctl}/*/*.h")
  add_custom_command(OUTPUT "${ctl}/build/tonnetz.odoc" COMMAND "${ctl}/build.sh" DEPENDS ${ctl_sources} "${ctl}/build.sh"
                     COMMENT "controllers")
  add_custom_target(controllers DEPENDS "${ctl}/build/tonnetz.odoc")
  add_executable(test_packages test_packages.cpp)
  add_dependencies(test_packages controllers)
  target_link_libraries(test_packages mpe_engine Threads::Threads)
  target_compile_definitions(test_packages PRIVATE MPE_TEST_PACKAGES="${MPE_PACKAGES_DIR}"
                             MPE_TEST_CONTROLLERS="${ctl}/build" MPE_TEST_CACHE="${CMAKE_CURRENT_BINARY_DIR}/cache")
  add_test(NAME test_packages COMMAND test_packages WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Takes (mpe/take.h): recorded and written by hand, on the controllers (so it needs them built,
# with clang, as test_packages does); and the example takes in takes/.
if(CLANG)
  set(ctl "${CMAKE_CURRENT_SOURCE_DIR}/../../controllers")
  if(NOT TARGET controllers)
    file(GLOB ctl_sources "${ctl}/*/*.c" "${ctl}/*/controller.json" "${ctl}/include/*.h")
    add_custom_command(OUTPUT "${ctl}/build/tonnetz.odoc" COMMAND "${ctl}/build.sh" DEPENDS ${ctl_sources} "${ctl}/build.sh"
                       COMMENT "controllers")
    add_custom_target(controllers DEPENDS "${ctl}/build/tonnetz.odoc")
  endif()
  add_executable(test_take test_take.cpp)
  add_dependencies(test_take controllers)
  target_link_libraries(test_take mpe_engine Threads::Threads)
  target_compile_definitions(test_take PRIVATE MPE_TEST_CONTROLLERS="${ctl}/build" MPE_TEST_TAKES="${CMAKE_CURRENT_SOURCE_DIR}/../../takes")
  add_test(NAME test_take COMMAND test_take WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
