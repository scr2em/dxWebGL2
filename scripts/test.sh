#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
#
# Builds the library (with the WebGPU backend), the tests and the examples with Emscripten and runs everything:
#
#   1. repository checks (SPDX headers, architecture layering, COM interface golden file, generated files)
#   2. unit and integration tests in node (header ABI, D3DX math, wide strings, project windows.h, WebD3D8 compatibility)
#   3. install + consuming the package (find_package, add_subdirectory, FetchContent)
#   4. in headless Chromium (SwiftShader provides WebGL2 and WebGPU), once per backend: the runtime test, the feature
#      tests, the frame hash regression and the examples; then the pictures of the two backends are compared
#
# Usage: scripts/test.sh [--clean] [--no-browser] [--build-dir DIR] [--jobs N] [--backend webgl2|webgpu|both]
#
# Environment:
#   EMSDK                     emsdk directory (emsdk_env.sh is sourced when emcmake is not on the PATH; default ~/emsdk)
#   NODE_PATH                 directory containing the `playwright` package   (default /opt/node22/lib/node_modules)
#   PLAYWRIGHT_BROWSERS_PATH  Playwright browser cache                         (default /opt/pw-browsers)
#   CHROMIUM_PATH             Chromium executable                              (default: the Playwright one)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build"
CLEAN=0
BROWSER=1
JOBS="$(nproc 2>/dev/null || echo 4)"
BACKENDS="webgl2 webgpu"

while [ $# -gt 0 ]; do
    case "$1" in
        --clean) CLEAN=1 ;;
        --no-browser) BROWSER=0 ;;
        --build-dir) shift; BUILD_DIR="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" ;;
        --jobs) shift; JOBS="$1" ;;
        --backend) shift; case "$1" in both) BACKENDS="webgl2 webgpu" ;; webgl2|webgpu) BACKENDS="$1" ;; *) echo "unknown backend: $1" >&2; exit 2 ;; esac ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | sed 's/^# \{0,1\}//' | head -n -1; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

if ! command -v emcmake >/dev/null 2>&1; then
    EMSDK_DIR="${EMSDK:-$HOME/emsdk}"
    if [ -f "$EMSDK_DIR/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$EMSDK_DIR/emsdk_env.sh" >/dev/null 2>&1
    fi
fi
for tool in emcmake cmake ninja node python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: '$tool' not found (set EMSDK or put it on the PATH)" >&2; exit 2; }
done

export NODE_PATH="${NODE_PATH:-/opt/node22/lib/node_modules}"
export PLAYWRIGHT_BROWSERS_PATH="${PLAYWRIGHT_BROWSERS_PATH:-/opt/pw-browsers}"
NODE_BROWSER="$(command -v node)"
# The Playwright install may belong to a different node than the one of the Emscripten SDK.
[ -x /opt/node22/bin/node ] && NODE_BROWSER=/opt/node22/bin/node

step() { printf '\n==> %s\n' "$*"; }

[ "$CLEAN" = 1 ] && rm -rf "$BUILD_DIR"

step "Configure ($BUILD_DIR)"
emcmake cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DDXWEBGL2_BUILD_PTHREADS=ON -DDXWEBGL2_BUILD_TESTS=ON -DDXWEBGL2_BUILD_EXAMPLES=ON \
    -DDXWEBGL2_WARNINGS_AS_ERRORS=ON -DDXWEBGL2_WEBGPU=ON

step "Build"
cmake --build "$BUILD_DIR" -j "$JOBS"

step "Repository checks and node tests (ctest)"
(cd "$BUILD_DIR" && ctest --output-on-failure)

step "Install and consume the package (find_package, add_subdirectory, FetchContent)"
"$ROOT/tests/consumer/run.sh" "$ROOT" "$BUILD_DIR" "$BUILD_DIR/consumers"

if [ "$BROWSER" = 1 ]; then
    status=0
    for backend in $BACKENDS; do
        # The WebGL2 runs pass no backend option (the default selects WebGL2): their commands are those of the WebGL2-only
        # test suite, and so are their screenshots' names.
        if [ "$backend" = webgl2 ]; then bopt=""; suffix=""; else bopt="--backend=$backend"; suffix="_$backend"; fi

        step "Runtime test in headless Chromium ($backend)"
        # The last mode renders ~10 s from a thread that never returns to its event loop and checks that frames keep being shown.
        for mode in "" --shaders --scene2 --scene3 --no-s3tc --reset --implicit "--frames=600 --yield"; do
            printf '  d3d8_runtime_test %-12s ' "${mode:-(default)}"
            if out="$("$NODE_BROWSER" "$ROOT/tests/runner/run_test.mjs" "$BUILD_DIR/tests" "$BUILD_DIR/shot${mode//-/_}${suffix}.png" $mode $bopt 2>&1)"; then
                echo "$out" | grep -E 'TEST_RESULT' || true
            else
                echo "FAILED"; echo "$out" | tail -40; status=1
            fi
        done

        step "Feature tests in headless Chromium ($backend: context loss, multisampling, shader instructions, emulations, diagnostics, ...)"
        if out="$("$NODE_BROWSER" "$ROOT/tests/runner/run_features.mjs" "$BUILD_DIR/tests" $bopt 2>&1)"; then
            echo "$out" | grep -E '^(==|RESULT)|XFAIL' | sed 's/^/  /'
        else
            echo "FAILED"; echo "$out" | tail -60; status=1
        fi

        step "Frame hash regression ($backend; tests/runner/reference_hashes.json; Chromium + SwiftShader)"
        if out="$("$NODE_BROWSER" "$ROOT/tests/runner/run_regression.mjs" "$BUILD_DIR/tests" $bopt 2>&1)"; then
            echo "$out" | sed 's/^/  /'
        else
            echo "FAILED"; echo "$out" | tail -20; status=1
        fi

        step "Examples in headless Chromium ($backend)"
        for ex in hello_triangle textured_cube; do
            printf '  %-16s ' "$ex"
            if out="$("$NODE_BROWSER" "$ROOT/tests/runner/run_example.mjs" "$BUILD_DIR/examples" $ex.html "$BUILD_DIR/$ex${suffix}.png" \
                        --frames=40 --expect=5,5,32,48,64 --expect-not=320,240,32,48,64,30 --expect-colors=8 $bopt 2>&1)"; then
                echo "$out" | grep RESULT
            else
                echo "FAILED"; echo "$out" | tail -20; status=1
            fi
        done
    done

    if [ "$BACKENDS" = "webgl2 webgpu" ]; then
        step "WebGL2 and WebGPU pictures compared (tolerance 8 per channel, at most 0.5% of the pixels beyond it)"
        if out="$("$NODE_BROWSER" "$ROOT/tests/runner/compare_backends.mjs" "$BUILD_DIR/tests" --out="$BUILD_DIR/compare" 2>&1)"; then
            echo "$out" | sed 's/^/  /'
        else
            echo "FAILED"; echo "$out" | tail -20; status=1
        fi
    fi
    [ "$status" = 0 ] || { echo; echo "SOME BROWSER TESTS FAILED"; exit 1; }
fi

step "All tests passed"
