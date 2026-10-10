#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""Checks the architecture of src/:

    src/core     the API independent core: formats, buffers, textures, programs, fixed-function and shader
                 translation, and the backend interface (core/backend.h). Must not include anything from src/d3d8
                 or src/api and must not mention a COM interface (IDirect3D*), so that a Direct3D 9 front end can
                 use it unchanged. Makes no GPU API call: no WebGL/GLES header, no gl*() call (the GL enum values
                 of the format table in core/format.cpp are the one exception, see EXCEPTIONS).
    src/core/gl  the WebGL2 backend: the only place that includes the GLES/WebGL headers and calls WebGL.
                 May include src/core.
    src/core/webgpu (later) the WebGPU backend, with the same rules as src/core/gl for its API.
    src/api      helpers shared by the front ends (COM plumbing). May include src/core (not a backend).
    src/d3d8     the Direct3D 8 front end. May include src/core (not a backend) and src/api; renders through the
                 Backend interface only: no GPU API header, call, type or constant.
    src/compat   link compatibility with the original WebD3D8 symbol names; only the public header.

Exits with status 1 and lists the offending lines otherwise.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# Directory below src/ -> include prefixes it may use ("..." quoted includes).
ALLOWED_PREFIXES = {
    "core": ("core/", "dxwebgl2/"),
    "core/gl": ("core/", "dxwebgl2/"),
    "core/webgpu": ("core/", "dxwebgl2/"),
    "api": ("core/", "api/", "dxwebgl2/"),
    "d3d8": ("core/", "api/", "d3d8/", "dxwebgl2/"),
    "compat": ("dxwebgl2/",),
}
BACKEND_DIRS = ("core/gl", "core/webgpu")
# Layers that must not reach into a backend's headers or a GPU API.
GPU_FREE = ("core", "api", "d3d8", "compat")
# Files allowed to include a GPU API header without calling it.
EXCEPTIONS = {
    "src/core/format.cpp": "includes <GLES3/gl3.h> for the GL enum values of the WebGL2 storage columns of the format table",
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
SYS_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+<([^>]+)>')
COM_RE = re.compile(r"\bIDirect3D\w*|\bID3DX\w*|\bD3DCAPS8\b")
GPU_HEADER_RE = re.compile(r"^(GLES\d?/|GL/|webgl/|webgpu/|emscripten/html5_webgl\.h$|emscripten/html5_webgpu\.h$)")
GL_CALL_RE = re.compile(r"\bgl[A-Z]\w*\s*\(|\bemscripten_webgl_\w+\s*\(|\bwgpu[A-Z]\w*\s*\(")
GL_TOKEN_RE = re.compile(r"\bGL(?:uint|int|enum|float|sizei|sizeiptr|intptr|boolean|bitfield|ubyte)\b|\bGL_[A-Z0-9_]+\b|\bWGPU\w+")


def strip_comment(line):
    """The code part of a line (drops // comments and lines inside /* */ blocks that start with * or /*)."""
    s = line.lstrip()
    if s.startswith(("//", "*", "/*")):
        return ""
    return line.split("//", 1)[0]


def layer_of(rel):
    """'core/gl/gl_draw.cpp' -> 'core/gl', 'core/types.h' -> 'core', 'd3d8/device.cpp' -> 'd3d8'."""
    parts = rel.split("/")
    if parts[0] == "core" and len(parts) > 2:
        return "core/" + parts[1]
    return parts[0]


problems = []
for dirpath, _, files in os.walk(SRC):
    for name in sorted(files):
        if not name.endswith((".h", ".cpp")):
            continue
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, SRC).replace(os.sep, "/")
        layer = layer_of(rel)
        repo_rel = "src/" + rel
        if layer not in ALLOWED_PREFIXES:
            problems.append("%s: unknown layer src/%s (add it to tools/check_layering.py)" % (repo_rel, layer))
            continue
        prefixes = ALLOWED_PREFIXES[layer]
        gpu_free = layer in GPU_FREE and repo_rel not in EXCEPTIONS
        for lineno, line in enumerate(open(path, encoding="utf-8"), 1):
            where = "%s:%d" % (repo_rel, lineno)
            m = INCLUDE_RE.match(line)
            if m:
                inc = m.group(1)
                if not inc.startswith(prefixes):
                    problems.append("%s: src/%s may not include \"%s\"" % (where, layer, inc))
                if layer in GPU_FREE and inc.startswith(tuple(d + "/" for d in BACKEND_DIRS)):
                    problems.append("%s: src/%s may not include a backend header \"%s\"" % (where, layer, inc))
                for b in BACKEND_DIRS:
                    other = [d for d in BACKEND_DIRS if d != b]
                    if layer == b and inc.startswith(tuple(d + "/" for d in other)):
                        problems.append("%s: backend %s may not include another backend's header \"%s\"" % (where, b, inc))
            m = SYS_INCLUDE_RE.match(line)
            if m and layer in GPU_FREE and repo_rel not in EXCEPTIONS and GPU_HEADER_RE.match(m.group(1)):
                problems.append("%s: src/%s may not include the GPU API header <%s> (only the backends do)" % (where, layer, m.group(1)))
            code = strip_comment(line)
            if layer.startswith("core") and COM_RE.search(code):
                problems.append("%s: src/core mentions a Direct3D interface: %s" % (where, line.strip()))
            if gpu_free and GL_CALL_RE.search(code):
                problems.append("%s: src/%s calls the GPU API (only the backends do): %s" % (where, layer, line.strip()))
            if layer in ("d3d8", "api", "compat") and GL_TOKEN_RE.search(code):
                problems.append("%s: src/%s uses a GPU API type or constant: %s" % (where, layer, line.strip()))

if problems:
    print("\n".join(problems))
    print("layering check FAILED (%d problems)" % len(problems))
    sys.exit(1)
print("layering OK")
