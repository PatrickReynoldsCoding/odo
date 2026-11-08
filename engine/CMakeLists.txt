cmake_minimum_required(VERSION 3.19)
project(mpe_engine C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MPE_SANITIZE "" CACHE STRING "address, undefined or thread: build the engine and tests with that sanitizer")
if(MPE_SANITIZE)
  add_compile_options(-fsanitize=${MPE_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${MPE_SANITIZE})
endif()


# Where the instruments come from: the faust-mpe-instruments submodule.
set(MPE_INSTRUMENTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/mpe-instruments"
    CACHE PATH "checkout of faust-mpe-instruments")
set(FAUST_EXECUTABLE faust CACHE STRING "faust compiler")
execute_process(COMMAND ${FAUST_EXECUTABLE} --version OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE faust_rc)
if(NOT faust_rc EQUAL 0)
  message(FATAL_ERROR "cannot run '${FAUST_EXECUTABLE}'. Install Faust, or pass -DFAUST_EXECUTABLE=...")
endif()
# Our own copy of the few Faust headers needed (see third_party/faust/README.md).
set(FAUST_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/faust/include")

# Which instruments are compiled in:
#   ON        every instrument package (.odoi), made from the instruments by their tools/pack.py
#             (into MPE_PACKAGES_DIR) and compiled from the package: the same files a computer MESH
#             loads while running. Needs python3.
#   SOURCES   straight from instruments/<group>/<name>.dsp (as before packages)
#   OFF       none: a MESH that loads packages while running (MPE_LIBFAUST)
set(MPE_BUILTIN_INSTRUMENTS ON CACHE STRING "ON (from packages), SOURCES or OFF")
set(MPE_PACKAGES_DIR "${MPE_INSTRUMENTS_DIR}/build/packages" CACHE PATH "where the instrument packages are made")
option(MPE_LIBFAUST "load instrument packages while running, with libfaust (computers)" OFF)

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${GEN_DIR}")

# Each instrument to compile in: its id, group, .dsp, -I folder; packages also their aliases and version.
set(INSTR_IDS "")
if(MPE_BUILTIN_INSTRUMENTS STREQUAL "SOURCES")
  file(GLOB_RECURSE DSP_FILES RELATIVE "${MPE_INSTRUMENTS_DIR}/instruments" CONFIGURE_DEPENDS
       "${MPE_INSTRUMENTS_DIR}/instruments/*/*.dsp")
  list(SORT DSP_FILES)
  if(NOT DSP_FILES)
    message(FATAL_ERROR "no .dsp files under ${MPE_INSTRUMENTS_DIR}/instruments (git submodule update --init)")
  endif()
  foreach(rel ${DSP_FILES})
    get_filename_component(grp "${rel}" DIRECTORY)
    get_filename_component(stem "${rel}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" fn "${grp}_${stem}")
    list(APPEND INSTR_IDS "${fn}")
    set(I_${fn}_ID "${grp}/${stem}")
    set(I_${fn}_GROUP "${grp}")
    set(I_${fn}_DSP "${MPE_INSTRUMENTS_DIR}/instruments/${rel}")
    set(I_${fn}_INC "${MPE_INSTRUMENTS_DIR}/lib")
    set(I_${fn}_ALIASES "")
    set(I_${fn}_VERSION "")
  endforeach()
  file(GLOB MPE_LIBS "${MPE_INSTRUMENTS_DIR}/lib/*.lib")
elseif(MPE_BUILTIN_INSTRUMENTS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(COMMAND ${Python3_EXECUTABLE} "${MPE_INSTRUMENTS_DIR}/tools/pack.py" -o "${MPE_PACKAGES_DIR}"
                          --sounds "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/assets/packs"
                  RESULT_VARIABLE pack_rc)
  if(NOT pack_rc EQUAL 0)
    message(FATAL_ERROR "packing the instruments failed (${MPE_INSTRUMENTS_DIR}/tools/pack.py)")
  endif()
  file(GLOB PACKAGES "${MPE_PACKAGES_DIR}/*.odoi")
  list(SORT PACKAGES)
  set(MPE_LIBS "")
  foreach(pkg ${PACKAGES})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pkg}")
    get_filename_component(stem "${pkg}" NAME_WLE)        # "cello", or a kept old version: "cello@1.0.0"
    set(dir "${GEN_DIR}/packages/${stem}")
    file(REMOVE_RECURSE "${dir}")
    file(ARCHIVE_EXTRACT INPUT "${pkg}" DESTINATION "${dir}")
    file(READ "${dir}/instrument.json" man)
    string(JSON id GET "${man}" id)
    string(JSON grp GET "${man}" tags 0)
    string(JSON ver GET "${man}" version)
    string(JSON nal LENGTH "${man}" aliases)
    set(aliases "")
    if(nal GREATER 0)
      math(EXPR last "${nal} - 1")
      foreach(k RANGE ${last})
        string(JSON a GET "${man}" aliases ${k})
        list(APPEND aliases "${a}")
      endforeach()
    endif()
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" fn "${id}")
    if(stem MATCHES "@")                                   # an old version, beside the current one
      string(REGEX REPLACE "[^A-Za-z0-9]" "_" vfn "${ver}")
      set(fn "${fn}_v${vfn}")
    endif()
    list(APPEND INSTR_IDS "${fn}")
    set(I_${fn}_ID "${id}")
    set(I_${fn}_GROUP "${grp}")
    set(I_${fn}_DSP "${dir}/instrument.dsp")
    set(I_${fn}_INC "${dir}/lib")
    set(I_${fn}_ALIASES "${aliases}")
    set(I_${fn}_VERSION "${ver}")
  endforeach()
  if(NOT INSTR_IDS)
    message(FATAL_ERROR "no instrument packages in ${MPE_PACKAGES_DIR}")
  endif()
endif()

set(INSTR_SOURCES "")
set(REG_DECL "")
foreach(fn ${INSTR_IDS})
  set(id "${I_${fn}_ID}")
  set(dsp "${I_${fn}_DSP}")
  set(inc "${I_${fn}_INC}")
  set(cls "Inst_${fn}")
  set(gen "${GEN_DIR}/${fn}.cpp")
  file(GLOB inc_libs "${inc}/*.lib")
  add_custom_command(
    OUTPUT "${gen}"
    COMMAND ${FAUST_EXECUTABLE} -I "${inc}" -lang cpp -i -vec -vs 32 -cn ${cls} -o "${gen}" "${dsp}"
    DEPENDS "${dsp}" ${inc_libs}
    COMMENT "faust ${id}")
  # An instrument that defines `post` (the stage all its voices share) gets it compiled too.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dsp}")
  file(STRINGS "${dsp}" has_post REGEX "^post *=")
  set(I_${fn}_POST "${has_post}")
  set(wrap "${GEN_DIR}/${fn}_entry.cpp")
  set(deps "${gen}")
  set(postdefs "")
  if(has_post)
    set(pgen "${GEN_DIR}/${fn}.post.cpp")
    add_custom_command(
      OUTPUT "${pgen}"
      COMMAND ${FAUST_EXECUTABLE} -I "${inc}" -lang cpp -i -vec -vs 32 -pn post -cn ${cls}_post -o "${pgen}" "${dsp}"
      DEPENDS "${dsp}" ${inc_libs}
      COMMENT "faust ${id} (post)")
    set(postdefs "#define GENPOST \"${pgen}\"\n#define CLASSPOST ${cls}_post\n")
    list(APPEND deps "${pgen}")
  endif()
  file(WRITE "${wrap}.tmp" "#define GEN \"${gen}\"\n#define CLASS ${cls}\n#define FN ${fn}\n${postdefs}#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/instrument_entry.cpp\"\n")
  configure_file("${wrap}.tmp" "${wrap}" COPYONLY)
  set_source_files_properties("${wrap}" PROPERTIES OBJECT_DEPENDS "${deps}"
                              COMPILE_FLAGS "-w -I${FAUST_INCLUDE_DIR}")
  list(APPEND INSTR_SOURCES "${wrap}")
  string(APPEND REG_DECL "dsp* mpe_create_${fn}();\nconst char* mpe_meta_${fn}(const char*);\ndsp* mpe_create_post_${fn}();\n")
endforeach()

# Effects from engine/fx/*.dsp: compiled like the instruments, but not in the instrument table.
foreach(fx reverb)
  set(fn "fx_${fx}")
  set(gen "${GEN_DIR}/${fn}.cpp")
  add_custom_command(
    OUTPUT "${gen}"
    COMMAND ${FAUST_EXECUTABLE} -lang cpp -i -cn Fx_${fx} -o "${gen}" "${CMAKE_CURRENT_SOURCE_DIR}/fx/${fx}.dsp"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fx/${fx}.dsp"
    COMMENT "faust fx/${fx}")
  set(wrap "${GEN_DIR}/${fn}_entry.cpp")
  file(WRITE "${wrap}" "#define GEN \"${gen}\"\n#define CLASS Fx_${fx}\n#define FN ${fn}\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/src/instrument_entry.cpp\"\n")
  set_source_files_properties("${wrap}" PROPERTIES OBJECT_DEPENDS "${gen}"
                              COMPILE_FLAGS "-w -I${FAUST_INCLUDE_DIR}")
  list(APPEND INSTR_SOURCES "${wrap}")
endforeach()

# builtins.cpp: the compiled-in instruments, for src/registry.cpp. Names come from each .dsp's `declare name`.
set(REG "#include \"mpe/engine.h\"\n${REG_DECL}\nnamespace mpe {\n")
string(APPEND REG "void builtinInstruments(std::vector<InstrumentInfo>& v) {\n  (void)v;\n")
foreach(fn ${INSTR_IDS})
  string(REGEX REPLACE "^.*/" "" stem "${I_${fn}_ID}")
  set(al "")
  foreach(a ${I_${fn}_ALIASES})
    string(APPEND al "\"${a}\", ")
  endforeach()
  string(APPEND REG "  { const char* n = mpe_meta_${fn}(\"name\"); const char* d = mpe_meta_${fn}(\"description\");\n")
  string(APPEND REG "    v.push_back({ \"${I_${fn}_ID}\", \"${I_${fn}_GROUP}\", n[0] ? n : \"${stem}\", d, mpe_create_${fn}, mpe_meta_${fn},\n")
  if(I_${fn}_POST)
    set(post "mpe_create_post_${fn}")
  else()
    set(post "nullptr")
  endif()
  string(APPEND REG "                  ${post}, { ${al}}, \"${I_${fn}_VERSION}\" }); }\n")
endforeach()
string(APPEND REG "}\n}  // namespace mpe\n")
file(WRITE "${GEN_DIR}/builtins.cpp.tmp" "${REG}")
configure_file("${GEN_DIR}/builtins.cpp.tmp" "${GEN_DIR}/builtins.cpp" COPYONLY)

# wasm3 runs controller packages (.odoc); miniz reads packages (zip).
file(GLOB WASM3_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm3/source/*.c")
list(FILTER WASM3_SOURCES EXCLUDE REGEX "m3_api_")          # WASI, libc and the tracer: a controller gets none
add_library(mpe_thirdparty STATIC ${WASM3_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.c")
target_include_directories(mpe_thirdparty SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm3/source"
                                                         "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")
target_compile_options(mpe_thirdparty PRIVATE -w -O2)
target_compile_definitions(mpe_thirdparty PUBLIC d_m3HasWASI=0 d_m3HasUVWASI=0 d_m3HasMetaWASI=0 d_m3HasTracer=0)

set(ENGINE_SOURCES src/clean.cpp src/engine.cpp src/breath.cpp src/looper.cpp src/registry.cpp src/package.cpp
                   src/controller.cpp src/wasm_dsp.cpp src/rail.cpp src/preset.cpp "${GEN_DIR}/builtins.cpp" ${INSTR_SOURCES})
if(MPE_LIBFAUST)
  list(APPEND ENGINE_SOURCES src/faust_loader.cpp)
endif()
add_library(mpe_engine STATIC ${ENGINE_SOURCES})
target_link_libraries(mpe_engine PUBLIC mpe_thirdparty)
if(MPE_LIBFAUST)
  # Loaded while running (src/faust_loader.cpp says why), so not linked: only its headers are used.
  find_library(LIBFAUST faust REQUIRED)
  target_link_libraries(mpe_engine PUBLIC ${CMAKE_DL_LIBS})
  target_compile_definitions(mpe_engine PUBLIC MPE_LIBFAUST=1)
endif()
target_include_directories(mpe_engine PUBLIC include)
target_include_directories(mpe_engine SYSTEM PUBLIC "${FAUST_INCLUDE_DIR}")
target_compile_options(mpe_engine PRIVATE -Wall -Wextra)
# DSP is always optimised, also in an Android debug build (which is -O0 otherwise: the
# instruments then take several times the CPU and the phone numbers mean nothing).
if(NOT MPE_SANITIZE)
  target_compile_options(mpe_engine PRIVATE -O3)
endif()

option(MPE_BUILD_TESTS "build the tests and tools" ON)
if(MPE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(tools)
endif()
