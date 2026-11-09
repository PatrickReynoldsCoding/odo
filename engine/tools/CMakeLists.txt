add_executable(bench bench.cpp)
target_link_libraries(bench mpe_engine)
add_executable(bench_wasm bench_wasm.cpp)
target_link_libraries(bench_wasm mpe_engine)
# Takes (mpe/take.h): check one, expand a score into touches. tools/take builds and runs it.
add_executable(odo-take take.cpp)
target_link_libraries(odo-take mpe_engine)
target_compile_definitions(odo-take PRIVATE ODO_CONTROLLERS="${CMAKE_CURRENT_SOURCE_DIR}/../../controllers/build")
