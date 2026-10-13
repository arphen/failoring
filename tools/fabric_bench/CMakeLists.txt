# fabric_bench (virtual textile tests) and its self-checks (test_fabric_bench).
add_library(fabric_bench_lib STATIC fabric_bench.cpp)
target_link_libraries(fabric_bench_lib PUBLIC cloth_core)
target_include_directories(fabric_bench_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(fabric_bench_lib PRIVATE -Wall -Wextra)

add_executable(fabric_bench fabric_bench_main.cpp)
target_link_libraries(fabric_bench PRIVATE fabric_bench_lib)
target_compile_definitions(fabric_bench PRIVATE FL_MATERIALS_PATH="${FL_MATERIALS_PATH}")

# Self-checks of the theory helpers and analysis functions (pure math, fast). Run by ctest.
add_executable(test_fabric_bench test_fabric_bench.cpp)
target_link_libraries(test_fabric_bench PRIVATE fabric_bench_lib)
add_test(NAME test_fabric_bench COMMAND test_fabric_bench)
