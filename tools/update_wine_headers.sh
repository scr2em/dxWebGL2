#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
#
# Re-creates third_party/wine/ from the pinned upstream Wine commit and the dxWebGL2 patch, or moves the pin.
#
#   tools/update_wine_headers.sh                 # verify: fetch the pinned commit, apply the patch, compare
#   tools/update_wine_headers.sh --write         # same, and overwrite third_party/wine/include with the result
#   tools/update_wine_headers.sh --commit SHA    # use another Wine commit (implies --write; edit WINE_COMMIT below and
#                                                # third_party/wine/PROVENANCE.md afterwards, and regenerate the patch if
#                                                # it no longer applies)
#
# Needs git and network access to https://github.com/wine-mirror/wine.
set -euo pipefail

WINE_REPO=https://github.com/wine-mirror/wine.git
WINE_COMMIT=20315474eb2a129234d13a9d91376ec7ea752d4c

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WRITE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --write) WRITE=1 ;;
        --commit) shift; WINE_COMMIT="$1"; WRITE=1 ;;
        -h|--help) sed -n '2,/^set -e/p' "$0" | sed 's/^# \{0,1\}//' | head -n -1; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git init -q "$TMP/wine"
git -C "$TMP/wine" remote add origin "$WINE_REPO"
git -C "$TMP/wine" fetch -q --depth 1 --filter=blob:none origin "$WINE_COMMIT"
git -C "$TMP/wine" sparse-checkout init --no-cone
git -C "$TMP/wine" sparse-checkout set /include/d3d8.h /include/d3d8types.h /include/d3d8caps.h /COPYING.LIB /VERSION
git -C "$TMP/wine" checkout -q FETCH_HEAD

echo "Wine $(cat "$TMP/wine/VERSION") at $(git -C "$TMP/wine" rev-parse HEAD)"
mkdir -p "$TMP/out/include"
cp "$TMP/wine/include/"d3d8.h "$TMP/wine/include/"d3d8types.h "$TMP/wine/include/"d3d8caps.h "$TMP/out/include/"
(cd "$TMP/out" && patch -p1 --no-backup-if-mismatch < "$ROOT/third_party/wine/patches/0001-dxwebgl2-integration.patch")

if diff -r "$TMP/out/include" "$ROOT/third_party/wine/include" >/dev/null; then
    echo "third_party/wine/include is exactly the pinned Wine headers plus the patch."
elif [ "$WRITE" = 1 ]; then
    cp "$TMP/out/include/"*.h "$ROOT/third_party/wine/include/"
    cp "$TMP/wine/COPYING.LIB" "$ROOT/third_party/wine/COPYING.LIB"
    echo "third_party/wine updated."
else
    diff -r "$TMP/out/include" "$ROOT/third_party/wine/include" || true
    echo "third_party/wine/include differs from the pinned Wine headers plus the patch (see above)." >&2
    exit 1
fi
