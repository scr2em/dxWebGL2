#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""Checks the architecture of src/:

    src/core   the WebGL2 core: formats, GL state, buffers, textures, programs, fixed-function and shader
               translation. Must not include anything from src/d3d8 or src/api and must not mention a COM
               interface (IDirect3D*), so that a Direct3D 9 front end can use it unchanged.
    src/api    helpers shared by the front ends (COM plumbing). May include src/core.
    src/d3d8   the Direct3D 8 front end. May include src/core and src/api.
    src/compat link compatibility with the original WebD3D8 symbol names; only the public header.

Exits with status 1 and lists the offending lines otherwise.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

ALLOWED_PREFIXES = {
    "core": ("core/", "dxwebgl2/"),
    "api": ("core/", "api/", "dxwebgl2/"),
    "d3d8": ("core/", "api/", "d3d8/", "dxwebgl2/"),
    "compat": ("dxwebgl2/",),
}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
COM_RE = re.compile(r"\bIDirect3D\w*|\bID3DX\w*|\bD3DCAPS8\b")

problems = []
for layer, prefixes in ALLOWED_PREFIXES.items():
    base = os.path.join(SRC, layer)
    if not os.path.isdir(base):
        continue
    for name in sorted(os.listdir(base)):
        if not name.endswith((".h", ".cpp")):
            continue
        path = os.path.join(base, name)
        for lineno, line in enumerate(open(path, encoding="utf-8"), 1):
            m = INCLUDE_RE.match(line)
            if m and not m.group(1).startswith(prefixes):
                problems.append("%s:%d: src/%s may not include \"%s\"" % (os.path.relpath(path, ROOT), lineno, layer, m.group(1)))
            if layer == "core" and COM_RE.search(line) and not line.lstrip().startswith(("//", "*", "/*")):
                problems.append("%s:%d: src/core mentions a Direct3D interface: %s" % (os.path.relpath(path, ROOT), lineno, line.strip()))

if problems:
    print("\n".join(problems))
    print("layering check FAILED (%d problems)" % len(problems))
    sys.exit(1)
print("layering OK")
