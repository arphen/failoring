#!/bin/sh
# Builds wasm/failoring.wasm with wasi-sdk 24 (see docs/ARCHITECTURE.md). Usage: W=/path/to/wasi-sdk tools/build_wasm.sh
set -e
W=${W:-$HOME/toolchain/wasi-sdk-24.0-x86_64-linux}
SRC="wasm/wasm_api.cpp cloth_core/src/cloth.cpp cloth_core/src/cloth_implicit.cpp cloth_core/src/cloth_static.cpp cloth_core/src/cloth_friction.cpp cloth_core/src/cloth_vbd.cpp cloth_core/src/linear_ext.cpp cloth_core/src/cloth_contact_al.cpp cloth_core/src/mesher.cpp cloth_core/src/mesh_sizing.cpp cloth_core/src/sparse_chol.cpp cloth_core/src/sdf_grid.cpp cloth_core/src/skin.cpp cloth_core/src/cloth_skin.cpp cloth_core/src/barrier_contact.cpp cloth_core/src/barrier_ccd.cpp cloth_core/src/barrier_selfcontact.cpp cloth_core/src/cloth_barrier.cpp"
$W/bin/clang++ --target=wasm32-wasi --sysroot=$W/share/wasi-sysroot -O3 -msimd128 -fno-math-errno -flto -std=c++17 -fno-exceptions -mexec-model=reactor -Icloth_core/include $SRC -Wl,--export-memory -Wl,--initial-memory=33554432 -Wl,--max-memory=1073741824 -o wasm/failoring.wasm
ls -la wasm/failoring.wasm
