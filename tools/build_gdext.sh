#!/bin/sh
# Rebuilds the Godot GDExtension that the editor loads: godot/bin/libfailoring.so (linux) or
# godot/bin/libfailoring.dylib (macOS), from gdextension/ and cloth_core/ at the current commit.
#
# Why: godot/bin/ holds a committed build that is often older than the sources. With a stale library the editor
# fails at start with: Native class "FailoringSim" not found. The rebuilt file is a local build product: do NOT commit
# it (git status will show godot/bin/ as modified after a rebuild; leave it that way, or `git checkout -- godot/bin`).
#
# Usage (from the repo root):
#   tools/build_gdext.sh            build with all cores
#   JOBS=2 tools/build_gdext.sh     limit parallel jobs (the godot-cpp bindings take 15-30 min on 2 cores)
#   GODOT_CPP_DIR=/path tools/build_gdext.sh   use an existing godot-cpp 4.3 checkout instead of cloning one
#   FL_MACOS_ARCHS="arm64;x86_64" tools/build_gdext.sh   macOS universal build (default: this machine's arch)
#
# Needs: cmake >= 3.16, a C++17 compiler, git (only for the first clone of godot-cpp).
# godot-cpp is cloned into third_party/ and the CMake build directory is build-gd/ (both gitignored).
# After a rebuild, the first run of the project needs an import pass so Godot registers the extension:
#   godot --headless --path godot --import
# (the editor does this on its own; see docs/NATIVE_NOTES.md).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG=godot-4.3-stable
GODOT_CPP_DIR="${GODOT_CPP_DIR:-$ROOT/third_party/godot-cpp}"
BUILD_DIR="$ROOT/build-gd"
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null) || echo 2)}"

for tool in cmake git; do
	command -v "$tool" >/dev/null 2>&1 || { echo "build_gdext: $tool not found" >&2; exit 1; }
done

if [ ! -f "$GODOT_CPP_DIR/CMakeLists.txt" ]; then
	echo "build_gdext: cloning godot-cpp $TAG into $GODOT_CPP_DIR"
	mkdir -p "$(dirname "$GODOT_CPP_DIR")"
	git clone --depth 1 --branch "$TAG" https://github.com/godotengine/godot-cpp "$GODOT_CPP_DIR"
fi

# The gdextension CMakeLists expects godot-cpp at ../third_party/godot-cpp. Point it there when GODOT_CPP_DIR is elsewhere.
if [ "$GODOT_CPP_DIR" != "$ROOT/third_party/godot-cpp" ]; then
	mkdir -p "$ROOT/third_party"
	if [ ! -e "$ROOT/third_party/godot-cpp" ]; then
		ln -s "$(cd "$GODOT_CPP_DIR" && pwd)" "$ROOT/third_party/godot-cpp"
	fi
fi

EXTRA=""
case "$(uname -s)" in
	Darwin)
		LIB=libfailoring.dylib
		if [ -n "${FL_MACOS_ARCHS:-}" ]; then
			EXTRA="-DCMAKE_OSX_ARCHITECTURES=$FL_MACOS_ARCHS"
		fi
		;;
	Linux)
		LIB=libfailoring.so
		;;
	*)
		echo "build_gdext: unsupported platform $(uname -s) (linux and macOS only; Windows needs its own build)" >&2
		exit 1
		;;
esac

cmake -S "$ROOT/gdextension" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release $EXTRA
cmake --build "$BUILD_DIR" -j "$JOBS" --target failoring

if [ ! -f "$ROOT/godot/bin/$LIB" ]; then
	echo "build_gdext: expected $ROOT/godot/bin/$LIB but it was not written" >&2
	exit 1
fi
echo "build_gdext: wrote godot/bin/$LIB ($(wc -c < "$ROOT/godot/bin/$LIB") bytes)"
echo "build_gdext: do not commit godot/bin/$LIB (see the comment at the top of this script)"
