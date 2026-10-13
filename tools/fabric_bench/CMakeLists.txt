# fabric_bench (virtual textile tests), fabric_fit (swatch measurements -> preset), their tests.
add_library(fabric_bench_lib STATIC fabric_bench.cpp)
target_link_libraries(fabric_bench_lib PUBLIC cloth_core)
target_include_directories(fabric_bench_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(fabric_bench_lib PRIVATE -Wall -Wextra)

add_executable(fabric_bench fabric_bench_main.cpp)
target_link_libraries(fabric_bench PRIVATE fabric_bench_lib)
target_compile_definitions(fabric_bench PRIVATE FL_MATERIALS_PATH="${FL_MATERIALS_PATH}")
