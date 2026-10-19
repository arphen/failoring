# Godot GDExtension: builds libfailoring.<platform>.so into godot/bin.
# Usage (from repo root):  cmake -S gdextension -B build-gd -DCMAKE_BUILD_TYPE=Release && cmake --build build-gd -j
cmake_minimum_required(VERSION 3.16)
project(failoring_gdextension CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(GODOT_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/godot-cpp)
add_subdirectory(${GODOT_CPP_DIR} ${CMAKE_BINARY_DIR}/godot-cpp)

# cloth_core is a plain C++ static library (no Godot dependency)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../cloth_core)
add_library(cloth_core STATIC
  ${CORE}/src/cloth.cpp ${CORE}/src/cloth_implicit.cpp ${CORE}/src/cloth_static.cpp ${CORE}/src/cloth_friction.cpp ${CORE}/src/cloth_vbd.cpp ${CORE}/src/linear_ext.cpp ${CORE}/src/cloth_contact_al.cpp ${CORE}/src/mesher.cpp ${CORE}/src/material.cpp
  ${CORE}/src/mesh_sizing.cpp ${CORE}/src/sparse_chol.cpp ${CORE}/src/metrics.cpp ${CORE}/src/sdf_grid.cpp ${CORE}/src/skin.cpp ${CORE}/src/cloth_skin.cpp ${CORE}/src/barrier_contact.cpp ${CORE}/src/barrier_ccd.cpp ${CORE}/src/barrier_selfcontact.cpp ${CORE}/src/cloth_barrier.cpp)
target_include_directories(cloth_core PUBLIC ${CORE}/include)

# crochet_core is a plain C++ static library (no Godot dependency); the
# extension does not link it yet -- T7.1 wires the editor entry points.
set(CROCHET_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../crochet_core)
add_library(crochet_core STATIC
  ${CROCHET_CORE}/src/yarn.cpp ${CROCHET_CORE}/src/stitch_model.cpp ${CROCHET_CORE}/src/parse.cpp
  ${CROCHET_CORE}/src/graph.cpp ${CROCHET_CORE}/src/crochet_mesh.cpp ${CROCHET_CORE}/src/crochet_model.cpp ${CROCHET_CORE}/src/crochet_layout.cpp ${CROCHET_CORE}/src/snapshot.cpp ${CROCHET_CORE}/src/crochet_relax.cpp ${CROCHET_CORE}/src/crochet_scene.cpp ${CROCHET_CORE}/src/crochet_metrics.cpp ${CROCHET_CORE}/src/crochet_session.cpp ${CROCHET_CORE}/src/crochet_run.cpp ${CROCHET_CORE}/src/crochet_calibration.cpp ${CROCHET_CORE}/src/crochet_hinge_fit.cpp
  ${CROCHET_CORE}/src/crochet_rod.cpp ${CROCHET_CORE}/src/yarn_render.cpp ${CROCHET_CORE}/src/yarn_work.cpp ${CROCHET_CORE}/src/yarn_sim.cpp)
target_include_directories(crochet_core PUBLIC ${CROCHET_CORE}/include)
target_link_libraries(crochet_core PUBLIC cloth_core)

add_library(failoring SHARED src/failoring_sim.cpp src/gpu_cg.cpp src/register_types.cpp src/crochet_bindings.cpp src/yarn_bindings.cpp src/yarn_sim_bindings.cpp)
target_link_libraries(failoring PRIVATE godot-cpp cloth_core crochet_core)
set_target_properties(failoring PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../godot/bin)
