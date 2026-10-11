#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
#
# Installs dxWebGL2 and builds tests/consumer against it in the three supported ways (installed package,
# add_subdirectory, FetchContent), running each result in node. Needs `emcmake` on the PATH. The device pages it builds
# too (consumer_device*.html) need a browser: tests/runner/run_auto.mjs opens them.
#
#   tests/consumer/run.sh <repository> <build-dir-of-dxwebgl2> <work-dir>
set -euo pipefail
REPO="$(cd "$1" && pwd)"; BUILD="$(cd "$2" && pwd)"; WORK="$3"
mkdir -p "$WORK"; WORK="$(cd "$WORK" && pwd)"
rm -rf "$WORK/install" "$WORK/find_package" "$WORK/subdirectory" "$WORK/fetchcontent"

cmake --install "$BUILD" --prefix "$WORK/install" >/dev/null
test -f "$WORK/install/lib/cmake/dxWebGL2/dxWebGL2Config.cmake"
test -f "$WORK/install/include/dxwebgl2/dx8/d3d8.h"
test -f "$WORK/install/include/dxwebgl2/dxwebgl2.h"
test -f "$WORK/install/include/WebD3D8/WebD3D8.h"

for mode in find_package subdirectory fetchcontent; do
    echo "-- consumer: $mode"
    emcmake cmake -S "$REPO/tests/consumer" -B "$WORK/$mode" -G Ninja -DCONSUMER_MODE=$mode \
        -DCMAKE_PREFIX_PATH="$WORK/install" -DCMAKE_FIND_ROOT_PATH="$WORK/install" -DDXWEBGL2_SOURCE_DIR="$REPO" >/dev/null
    cmake --build "$WORK/$mode" >/dev/null
    for exe in consumer consumer_pthreads; do
        [ -f "$WORK/$mode/$exe.js" ] || { echo "   (no $exe)"; continue; }
        out="$(node "$WORK/$mode/$exe.js")"
        echo "$out" | grep -q CONSUMER_OK || { echo "FAILED: $mode/$exe: $out"; exit 1; }
        echo "   $exe: $(echo "$out" | head -1)"
    done
done
echo "consumers OK"
