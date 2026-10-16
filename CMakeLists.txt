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
  cloth_core/src/cloth_static.cpp
  cloth_core/src/cloth_friction.cpp
  cloth_core/src/cloth_vbd.cpp
  cloth_core/src/linear_ext.cpp
  cloth_core/src/cloth_contact_al.cpp
  cloth_core/src/mesher.cpp
  cloth_core/src/material.cpp
  cloth_core/src/mesh_sizing.cpp
  cloth_core/src/sparse_chol.cpp
  cloth_core/src/metrics.cpp
  cloth_core/src/sdf_grid.cpp
  cloth_core/src/skin.cpp
  cloth_core/src/cloth_skin.cpp
  cloth_core/src/barrier_contact.cpp
  cloth_core/src/barrier_ccd.cpp
  cloth_core/src/barrier_selfcontact.cpp
  cloth_core/src/cloth_barrier.cpp)
target_include_directories(cloth_core PUBLIC cloth_core/include)
target_compile_options(cloth_core PRIVATE -Wall -Wextra)

# Optional threading of the SDF bake (sdf_grid.cpp uses #pragma omp only under #ifdef _OPENMP). Off by default.
option(FL_OPENMP "Build cloth_core with OpenMP (parallel parts of the SDF bake)" OFF)
if(FL_OPENMP)
  find_package(OpenMP REQUIRED COMPONENTS CXX)
  target_link_libraries(cloth_core PUBLIC OpenMP::OpenMP_CXX)
endif()

# Crochet core: yarn data, pattern language, stitch graph, mesh and scenes.
# Static and Godot-free; it links cloth_core but cloth_core does not depend on it.
add_library(crochet_core STATIC
  crochet_core/src/yarn.cpp)
target_include_directories(crochet_core PUBLIC crochet_core/include)
target_link_libraries(crochet_core PUBLIC cloth_core)
target_compile_options(crochet_core PRIVATE -Wall -Wextra)

enable_testing()
set(FL_MATERIALS_PATH "${CMAKE_SOURCE_DIR}/materials/fabrics.json")
set(FL_YARNS_PATH "${CMAKE_SOURCE_DIR}/materials/yarns.json")
set(FL_OUT_DIR "${CMAKE_SOURCE_DIR}/out")

foreach(t test_basics test_cantilever test_drape test_garment test_sdf_grid test_soft_skin test_barrier test_passthrough test_layers test_skin_barrier test_wedge test_static test_friction test_intrinsic)
  add_executable(${t} cloth_core/tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE cloth_core)
  target_compile_definitions(${t} PRIVATE FL_MATERIALS_PATH="${FL_MATERIALS_PATH}" FL_OUT_DIR="${FL_OUT_DIR}")
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# crochet_core test: links crochet_core and reads the yarn database.
add_executable(test_crochet_yarn crochet_core/tests/test_crochet_yarn.cpp)
target_link_libraries(test_crochet_yarn PRIVATE crochet_core)
target_include_directories(test_crochet_yarn PRIVATE cloth_core/tests)
target_compile_definitions(test_crochet_yarn PRIVATE FL_YARNS_PATH="${FL_YARNS_PATH}")
add_test(NAME test_crochet_yarn COMMAND test_crochet_yarn)

# Native timing (not in ctest): cmake -DFL_BENCH=ON, then build/bench_native.
option(FL_BENCH "Build the native timing benchmark (bench_native), not run by ctest" OFF)
if(FL_BENCH)
  add_executable(bench_native cloth_core/tests/bench_native.cpp)
  target_link_libraries(bench_native PRIVATE cloth_core)
  target_compile_definitions(bench_native PRIVATE FL_MATERIALS_PATH="${FL_MATERIALS_PATH}")
  add_executable(bench_bake cloth_core/tests/bench_bake.cpp)
  target_link_libraries(bench_bake PRIVATE cloth_core)
endif()
