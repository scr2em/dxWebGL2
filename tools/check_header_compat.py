#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""Compares dxWebGL2's Direct3D 8 headers with another set of Direct3D 8 headers.

dxWebGL2 does not ship Microsoft's DirectX SDK headers, but code written against them
must compile and behave the same. This maintainer tool checks that, given a copy of the
reference headers (for example the DirectX 8 SDK, or the "min-dx8-sdk" excerpt):

  1. interface vtables: method names, order and parameter types of every COM interface
     (the order is the binary interface),
  2. structure sizes and member offsets, and the values of every constant / enumerator
     that exists in both header sets (compiled for the same target and executed),
  3. the names that exist in only one of the two sets.

Nothing of the reference headers is copied anywhere; the tool only reads them.

Example (wasm32, reference headers patched for Emscripten as the game port does):

  tools/check_header_compat.py \\
      --reference-dir /path/to/dx8 \\
      --reference-flags "-I/path/to/WebCompat/include -fshort-wchar -DBUILD_WITH_D3D8" \\
      --ours-flags "-Iinclude/dx8 -Ithird_party/wine/include -Iinclude"

Needs Emscripten (em++ on PATH) and node.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HEADERS = ["d3d8.h", "d3d8types.h", "d3d8caps.h"]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def read_headers(dirs):
    out = {}
    for h in HEADERS:
        for d in dirs:
            p = os.path.join(d, h)
            if os.path.exists(p):
                out[h] = strip_comments(open(p, encoding="latin-1").read())
                break
    return out


# ----------------------------------------------------------------------------
# Interfaces
# ----------------------------------------------------------------------------
IFACE_RE = re.compile(r"DECLARE_INTERFACE(?:_IID)?_\(\s*(\w+)\s*,\s*(\w+)[^)]*\)\s*\{(.*?)\n\};", re.S)
METHOD_RE = re.compile(r"STDMETHOD(?:_\(\s*([^,)]+?)\s*,\s*(\w+)\s*\)|\(\s*(\w+)\s*\))\s*\(([^;]*?)\)\s*PURE\s*;", re.S)


def norm_param(p):
    p = p.strip()
    p = re.sub(r"\bstruct\b|\binterface\b|\bCONST\b|\bconst\b", " ", p)
    p = re.sub(r"\s+", " ", p).strip()
    if not p:
        return ""
    # Drop the parameter name (last identifier) when a type precedes it.
    m = re.match(r"^(.*?)(\w+)(\s*\[[^\]]*\])?$", p)
    if m and m.group(1).strip() and not m.group(1).strip().endswith(("unsigned", "signed")):
        p = m.group(1).strip() + ("*" if m.group(3) else "")
    return re.sub(r"\s+", "", p)


def parse_interfaces(text):
    ifaces = {}
    for m in IFACE_RE.finditer(text):
        name, base, body = m.group(1), m.group(2), m.group(3)
        methods = []
        for mm in METHOD_RE.finditer(body):
            ret = (mm.group(1) or "HRESULT").strip()
            mname = mm.group(2) or mm.group(3)
            args = mm.group(4)
            args = re.sub(r"^\s*THIS_?", "", args.strip())
            params = [norm_param(a) for a in args.split(",")] if args.strip() else []
            params = [p for p in params if p]
            methods.append((mname, re.sub(r"\s+", "", ret), params))
        ifaces[name] = (base, methods)
    return ifaces


def golden_text(text):
    """Stable text form of the interfaces of a header set (used for the golden file)."""
    lines = []
    for name, (base, methods) in sorted(parse_interfaces(text).items()):
        lines.append("interface %s : %s" % (name, base))
        for i, (m, ret, params) in enumerate(methods):
            lines.append("  %2d %s %s(%s)" % (i, ret, m, ", ".join(params)))
    return "\n".join(lines) + "\n"


def compare_interfaces(ref_text, our_text):
    ref = parse_interfaces(ref_text)
    our = parse_interfaces(our_text)
    problems = 0
    for name in sorted(set(ref) | set(our)):
        if name not in our:
            print("  interface %s: missing in ours" % name)
            problems += 1
            continue
        if name not in ref:
            print("  interface %s: only in ours" % name)
            continue
        rb, rm = ref[name]
        ob, om = our[name]
        if rb != ob:
            print("  interface %s: base %s vs %s" % (name, rb, ob))
            problems += 1
        if [m[0] for m in rm] != [m[0] for m in om]:
            print("  interface %s: method order differs" % name)
            print("    ref : %s" % [m[0] for m in rm])
            print("    ours: %s" % [m[0] for m in om])
            problems += 1
            continue
        for a, b in zip(rm, om):
            if a != b:
                print("  interface %s::%s: signature %s vs %s" % (name, a[0], a[1:], b[1:]))
                problems += 1
    print("  %d interfaces compared, %d problems" % (len(set(ref) & set(our)), problems))
    return problems


# ----------------------------------------------------------------------------
# Structures and constants
# ----------------------------------------------------------------------------
STRUCT_RE = re.compile(r"typedef\s+struct\s+\w*\s*\{(.*?)\}\s*(\w+)\s*(?:,[^;]*)?;", re.S)


def parse_structs(texts):
    out = {}
    for text in texts.values():
        for m in STRUCT_RE.finditer(text):
            body, name = m.group(1), m.group(2)
            fields = []
            for line in body.split(";"):
                line = line.strip()
                if not line or "{" in line or "}" in line or line.startswith(("union", "struct")):
                    continue
                line = re.sub(r"\bDUMMY\w+\b", " ", line)
                # "type a, b[2], c" -> names
                mm = re.match(r"^((?:\w+\s+)*?[\w\*]+[\s\*]+)(\w+(?:\s*\[[^\]]*\])*(?:\s*,\s*\*?\w+(?:\s*\[[^\]]*\])*)*)$", line, re.S)
                if not mm:
                    continue
                for n in mm.group(2).split(","):
                    n = re.sub(r"\[.*", "", n).strip().lstrip("*")
                    if n:
                        fields.append(n)
            out.setdefault(name, [])
            for f in fields:
                if f not in out[name]:
                    out[name].append(f)
    return out


def identifiers(texts):
    ids = set()
    for text in texts.values():
        for m in re.finditer(r"\b(D3D[A-Z0-9_]+)\b", text):
            ids.add(m.group(1))
    return ids


def probe_source(structs, ids, prelude):
    lines = ["#include <stdio.h>", "#include <stddef.h>"] + prelude + ["int main() {"]
    for s in sorted(structs):
        lines.append('printf("sizeof(%s)=%%u\\n", (unsigned)sizeof(%s));' % (s, s))
        for f in structs[s]:
            lines.append('printf("offsetof(%s,%s)=%%u\\n", (unsigned)offsetof(%s, %s));' % (s, f, s, f))
    for i in sorted(ids):
        lines.append('printf("%s=%%lld\\n", (long long)(%s));' % (i, i))
    lines.append("return 0; }")
    return "\n".join(lines) + "\n"


def prune_and_build(structs, ids, prelude, flags, workdir, tag, cxx):
    """Compiles the probe, dropping every probe line the header set cannot compile.
    Returns (output lines, set of probes that were dropped)."""
    dropped = set()
    for _ in range(6):
        src = probe_source({k: list(v) for k, v in structs.items()}, ids, prelude)
        path = os.path.join(workdir, tag + ".cpp")
        open(path, "w").write(src)
        r = subprocess.run([cxx, "-fsyntax-only", "-ferror-limit=0", "-Wno-everything", "-std=c++17"] + flags + [path],
                           capture_output=True, text=True)
        if r.returncode == 0:
            break
        srclines = src.split("\n")
        bad = set()
        for m in re.finditer(r"%s:(\d+):\d+: error" % re.escape(path), r.stderr):
            ln = int(m.group(1)) - 1
            if 0 <= ln < len(srclines) and srclines[ln].startswith("printf"):
                bad.add(ln)
        if not bad:
            sys.stderr.write(r.stderr[:4000])
            raise SystemExit("%s: probe does not compile (not a missing name)" % tag)
        keep = []
        for i, l in enumerate(srclines):
            if i in bad:
                dropped.add(re.match(r'printf\("([^=]+)=', l).group(1))
            else:
                keep.append(l)
        # Rebuild structs/ids from the surviving lines.
        structs2, ids2 = {}, set()
        for l in keep:
            m = re.match(r'printf\("sizeof\((\w+)\)', l)
            if m:
                structs2.setdefault(m.group(1), [])
            m = re.match(r'printf\("offsetof\((\w+),(\w+)\)', l)
            if m:
                structs2.setdefault(m.group(1), []).append(m.group(2))
            m = re.match(r'printf\("(D3D\w+)=', l)
            if m:
                ids2.add(m.group(1))
        structs, ids = structs2, ids2
    else:
        raise SystemExit("%s: could not prune the probe" % tag)
    out = os.path.join(workdir, tag + ".js")
    r = subprocess.run([cxx, path, "-o", out, "-std=c++17", "-Wno-everything", "-sEXIT_RUNTIME=1"] + flags, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr[:4000])
        raise SystemExit("%s: link failed" % tag)
    return out, dropped


def run_node(js, node):
    r = subprocess.run([node, js], capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr[:2000])
        raise SystemExit("node failed")
    return dict(l.split("=", 1) for l in r.stdout.strip().split("\n") if "=" in l)


# Differences that are intentional and harmless.
KNOWN_BENIGN = {
    # Microsoft's mask is an int expression (7 << 29, negative), Wine's is unsigned; used with DWORD tokens only.
    "D3DVSD_TOKENTYPEMASK": "signedness of the constant",
}


def emit_expected(path, values, sizes_offsets, source_note):
    with open(path, "w") as f:
        f.write("// SPDX-License-Identifier: GPL-3.0-or-later\n")
        f.write("// Copyright (C) 2026 dxWebGL2 contributors\n")
        f.write("// Generated by tools/check_header_compat.py (%s).\n" % source_note)
        f.write("// Sizes, member offsets and constant values of the Direct3D 8 API compiled for wasm32.\n")
        f.write("// Do not edit: regenerate with the tool when the headers change on purpose.\n")
        for k in sorted(sizes_offsets):
            m = re.match(r"sizeof\((\w+)\)", k)
            if m:
                f.write("DXW_SIZE(%s, %s)\n" % (m.group(1), sizes_offsets[k]))
                continue
            m = re.match(r"offsetof\((\w+),(\w+)\)", k)
            f.write("DXW_OFFSET(%s, %s, %s)\n" % (m.group(1), m.group(2), sizes_offsets[k]))
        for k in sorted(values):
            f.write("DXW_VALUE(%s, %sLL)\n" % (k, values[k]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--reference-dir", help="directory with the reference d3d8.h, d3d8types.h, d3d8caps.h (needed except "
                                            "for --check-golden alone)")
    ap.add_argument("--check-golden", metavar="FILE", help="compare the interfaces/vtables of OUR headers with a golden file "
                                                          "(no reference headers needed; used by the tests)")
    ap.add_argument("--reference-flags", default="", help="extra compiler flags for the reference set (include paths for its windows.h...)")
    ap.add_argument("--ours-dir", nargs="*", default=None, help="directories with dxWebGL2's headers (default: the repository)")
    ap.add_argument("--ours-flags", default=None, help="compiler flags for our set (default: the repository include paths)")
    ap.add_argument("--emit-expected", metavar="FILE", help="write the reference sizes/offsets/values as DXW_* lines (for tests/)")
    ap.add_argument("--emit-golden", metavar="FILE", help="write the interface/vtable listing of OUR headers")
    ap.add_argument("--cxx", default="em++")
    ap.add_argument("--node", default="node")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ours_dirs = a.ours_dir or [os.path.join(root, "third_party/wine/include")]
    if a.check_golden and not a.reference_dir:
        our = read_headers(ours_dirs)
        if len(our) != len(HEADERS):
            raise SystemExit("headers not found in %s" % ours_dirs)
        current = golden_text("\n".join(our.values()))
        expected = open(a.check_golden).read()
        if current != expected:
            import difflib
            sys.stdout.writelines(difflib.unified_diff(expected.splitlines(True), current.splitlines(True), "golden", "headers"))
            print("the COM interfaces of the headers differ from %s (vtable order is the binary interface!)" % a.check_golden)
            return 1
        print("interfaces match %s (%d interfaces)" % (a.check_golden, current.count("interface ")))
        return 0
    if not a.reference_dir:
        raise SystemExit("--reference-dir is required (or use --check-golden alone)")
    ours_flags = (a.ours_flags.split() if a.ours_flags is not None
                  else ["-I" + os.path.join(root, "third_party/wine/include"), "-I" + os.path.join(root, "include"),
                        "-I" + os.path.join(root, "include/dx8")])
    ref_flags = ["-I" + a.reference_dir] + a.reference_flags.split()

    ref = read_headers([a.reference_dir])
    our = read_headers(ours_dirs)
    if len(ref) != len(HEADERS) or len(our) != len(HEADERS):
        raise SystemExit("missing headers (reference: %s, ours: %s)" % (sorted(ref), sorted(our)))

    if a.emit_golden:
        open(a.emit_golden, "w").write(golden_text("\n".join(our.values())))
    print("== interfaces ==")
    problems = compare_interfaces("\n".join(ref.values()), "\n".join(our.values()))

    structs = parse_structs(ref)
    for k, v in parse_structs(our).items():
        structs.setdefault(k, [])
        structs[k] += [f for f in v if f not in structs[k]]
    ids = identifiers(ref) | identifiers(our)
    ids -= set(structs)

    prelude = ['#include <d3d8.h>']
    with tempfile.TemporaryDirectory() as wd:
        print("== building probes ==")
        rjs, rdrop = prune_and_build(structs, ids, prelude, ref_flags, wd, "ref", a.cxx)
        ojs, odrop = prune_and_build(structs, ids, prelude, ours_flags, wd, "ours", a.cxx)
        rv, ov = run_node(rjs, a.node), run_node(ojs, a.node)

    print("== names present in only one header set ==")
    only_ref = sorted(x for x in odrop - rdrop if re.match(r"^(sizeof|offsetof)", x) is None)
    only_our = sorted(x for x in rdrop - odrop if re.match(r"^(sizeof|offsetof)", x) is None)
    print("  missing in ours (%d): %s" % (len(only_ref), " ".join(only_ref)))
    print("  only in ours    (%d): %s" % (len(only_our), " ".join(only_our)))
    missing_layout = sorted(x for x in (odrop ^ rdrop) if x.startswith(("sizeof", "offsetof")))
    if missing_layout:
        print("  layout probes available on one side only: %s" % " ".join(missing_layout))

    print("== values, sizes and offsets ==")
    diffs = 0
    for k in sorted(set(rv) & set(ov)):
        if rv[k] != ov[k]:
            if k in KNOWN_BENIGN:
                print("  known difference %s: reference %s, ours %s (%s)" % (k, rv[k], ov[k], KNOWN_BENIGN[k]))
                continue
            print("  DIFF %s: reference %s, ours %s" % (k, rv[k], ov[k]))
            diffs += 1
    if a.emit_expected:
        keep = (set(rv) & set(ov)) - set(KNOWN_BENIGN)
        so = {k: ov[k] for k in keep if k.startswith(("sizeof", "offsetof"))}
        vals = {k: ov[k] for k in keep if not k.startswith(("sizeof", "offsetof"))}
        emit_expected(a.emit_expected, vals, so, "reference headers compiled with %s" % a.cxx)
    print("  %d values compared, %d differences" % (len(set(rv) & set(ov)), diffs))
    return 1 if (problems or diffs or only_ref) else 0


if __name__ == "__main__":
    sys.exit(main())
