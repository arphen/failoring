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
  crochet_core/src/yarn.cpp
  crochet_core/src/stitch_model.cpp
  crochet_core/src/parse.cpp
  crochet_core/src/graph.cpp
  crochet_core/src/crochet_mesh.cpp
  crochet_core/src/crochet_model.cpp
  crochet_core/src/crochet_layout.cpp
  crochet_core/src/snapshot.cpp)
target_include_directories(crochet_core PUBLIC crochet_core/include)
target_link_libraries(crochet_core PUBLIC cloth_core)
target_compile_options(crochet_core PRIVATE -Wall -Wextra)

enable_testing()
set(FL_MATERIALS_PATH "${CMAKE_SOURCE_DIR}/materials/fabrics.json")
set(FL_YARNS_PATH "${CMAKE_SOURCE_DIR}/materials/yarns.json")
set(FL_PATTERNS_DIR "${CMAKE_SOURCE_DIR}/patterns")
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

# crochet_core test: the stitch mechanics model (plan section 4).
add_executable(test_crochet_model crochet_core/tests/test_crochet_model.cpp)
target_link_libraries(test_crochet_model PRIVATE crochet_core)
target_include_directories(test_crochet_model PRIVATE cloth_core/tests)
target_compile_definitions(test_crochet_model PRIVATE FL_YARNS_PATH="${FL_YARNS_PATH}")
add_test(NAME test_crochet_model COMMAND test_crochet_model)

# crochet_core test: the CrochetPARADE export importer plus `#!` extension
# lines (the decision-B bridge, plan sections 5.0/5.1). Reads the committed
# exports under patterns/: no node at test time.
add_executable(test_crochet_parse crochet_core/tests/test_crochet_parse.cpp)
target_link_libraries(test_crochet_parse PRIVATE crochet_core)
target_include_directories(test_crochet_parse PRIVATE cloth_core/tests)
target_compile_definitions(test_crochet_parse PRIVATE FL_PATTERNS_DIR="${FL_PATTERNS_DIR}")
add_test(NAME test_crochet_parse COMMAND test_crochet_parse)

# crochet_core test: the mesh topology (plan sections 6.1-6.4, hygiene test 6.10.4):
# faces, rest shapes and orientation of every fixture. Reads the committed exports
# and the yarn database; no node at test time.
add_executable(test_crochet_mesh crochet_core/tests/test_crochet_mesh.cpp)
target_link_libraries(test_crochet_mesh PRIVATE crochet_core)
target_include_directories(test_crochet_mesh PRIVATE cloth_core/tests)
target_compile_definitions(test_crochet_mesh PRIVATE FL_PATTERNS_DIR="${FL_PATTERNS_DIR}" FL_YARNS_PATH="${FL_YARNS_PATH}")
add_test(NAME test_crochet_mesh COMMAND test_crochet_mesh)

# crochet_core test: the initial layout of every fixture (plan section 6.8). Writes PNG snapshots when
# FL_LAYOUT_SHOTS names a directory (docs/crochet-shots/layout/ holds the committed set).
add_executable(test_crochet_layout crochet_core/tests/test_crochet_layout.cpp)
target_link_libraries(test_crochet_layout PRIVATE crochet_core)
target_include_directories(test_crochet_layout PRIVATE cloth_core/tests)
target_compile_definitions(test_crochet_layout PRIVATE FL_PATTERNS_DIR="${FL_PATTERNS_DIR}" FL_YARNS_PATH="${FL_YARNS_PATH}")
add_test(NAME test_crochet_layout COMMAND test_crochet_layout)

# Native timing (not in ctest): cmake -DFL_BENCH=ON, then build/bench_native.
option(FL_BENCH "Build the native timing benchmark (bench_native), not run by ctest" OFF)
if(FL_BENCH)
  add_executable(bench_native cloth_core/tests/bench_native.cpp)
  target_link_libraries(bench_native PRIVATE cloth_core)
  target_compile_definitions(bench_native PRIVATE FL_MATERIALS_PATH="${FL_MATERIALS_PATH}")
  add_executable(bench_bake cloth_core/tests/bench_bake.cpp)
  target_link_libraries(bench_bake PRIVATE cloth_core)
endif()
