#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""Every source file must start with an SPDX license identifier; files of dxWebGL2 itself also carry the
copyright line "Copyright (C) 2026 dxWebGL2 contributors". Third party files (third_party/) only need an SPDX
identifier of their own license.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", "build", "node_modules", "__pycache__"}
EXTENSIONS = (".h", ".hpp", ".cpp", ".c", ".inl", ".inc", ".mjs", ".js", ".py", ".sh", ".cmake", ".html", ".in", ".txt")
THIRD_PARTY = os.path.join("third_party", "")

problems = []
for dp, dn, fn in os.walk(ROOT):
    dn[:] = [d for d in dn if d not in SKIP_DIRS]
    for f in sorted(fn):
        rel = os.path.relpath(os.path.join(dp, f), ROOT)
        if not (f.endswith(EXTENSIONS) or f == "CMakeLists.txt"):
            continue
        if f.endswith(".txt") and f != "CMakeLists.txt":
            continue
        if rel.endswith(".patch"):
            continue
        head = "".join(open(os.path.join(dp, f), encoding="utf-8", errors="replace").readlines()[:12])
        if not re.search(r"SPDX-License-Identifier:\s*\S+", head):
            problems.append("%s: no SPDX-License-Identifier in the first lines" % rel)
        elif not rel.startswith(THIRD_PARTY) and "Copyright (C) 2026 dxWebGL2 contributors" not in head:
            problems.append("%s: no 'Copyright (C) 2026 dxWebGL2 contributors' line" % rel)
        elif not rel.startswith(THIRD_PARTY) and "GPL-3.0-or-later" not in head:
            problems.append("%s: SPDX identifier is not GPL-3.0-or-later" % rel)

if problems:
    print("\n".join(problems))
    print("SPDX check FAILED (%d files)" % len(problems))
    sys.exit(1)
print("SPDX headers OK")
