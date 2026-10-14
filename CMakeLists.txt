cmake_minimum_required(VERSION 3.16)
project(failoring LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(cloth_core STATIC
  cloth_core/src/cloth.cpp
  cloth_core/src/cloth_implicit.cpp
  cloth_core/src/mesher.cpp
  cloth_core/src/material.cpp
  cloth_core/src/mesh_sizing.cpp
  cloth_core/src/sparse_chol.cpp
  cloth_core/src/metrics.cpp
  cloth_core/src/sdf_grid.cpp
  cloth_core/src/skin.cpp
  cloth_core/src/cloth_skin.cpp)
target_include_directories(cloth_core PUBLIC cloth_core/include)
target_compile_options(cloth_core PRIVATE -Wall -Wextra)

# Optional threading of the SDF bake (sdf_grid.cpp uses #pragma omp only under #ifdef _OPENMP). Off by default.
option(FL_OPENMP "Build cloth_core with OpenMP (parallel parts of the SDF bake)" OFF)
if(FL_OPENMP)
  find_package(OpenMP REQUIRED COMPONENTS CXX)
  target_link_libraries(cloth_core PUBLIC OpenMP::OpenMP_CXX)
endif()

enable_testing()
set(FL_MATERIALS_PATH "${CMAKE_SOURCE_DIR}/materials/fabrics.json")
set(FL_OUT_DIR "${CMAKE_SOURCE_DIR}/out")

foreach(t test_basics test_cantilever test_drape test_garment test_sdf_grid test_soft_skin)
  add_executable(${t} cloth_core/tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE cloth_core)
  target_compile_definitions(${t} PRIVATE FL_MATERIALS_PATH="${FL_MATERIALS_PATH}" FL_OUT_DIR="${FL_OUT_DIR}")
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Native timing (not in ctest): cmake -DFL_BENCH=ON, then build/bench_native.
option(FL_BENCH "Build the native timing benchmark (bench_native), not run by ctest" OFF)
if(FL_BENCH)
  add_executable(bench_native cloth_core/tests/bench_native.cpp)
  target_link_libraries(bench_native PRIVATE cloth_core)
  target_compile_definitions(bench_native PRIVATE FL_MATERIALS_PATH="${FL_MATERIALS_PATH}")
  add_executable(bench_bake cloth_core/tests/bench_bake.cpp)
  target_link_libraries(bench_bake PRIVATE cloth_core)
endif()
