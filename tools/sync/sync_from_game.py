#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""Re-imports the Direct3D 8 on WebGL2 layer (Dependencies/WebD3D8) from the C&C Generals Zero Hour
web port repository into this repository.

dxWebGL2 started as an extraction of that directory, restructured (core / d3d8 split, renamed API,
generic wording, SPDX headers). While both repositories evolve, this tool carries upstream changes
over:

  1. the upstream files are read (working tree of the game repo by default, or a git revision),
  2. the same mechanical transformation that produced this repository (file mapping, splitting of
     the files that were divided, renames, license headers, include paths, wording rules - see
     sync_rules.py) is applied to BOTH the last imported upstream snapshot (tools/sync/upstream_base.tar.xz)
     and the new upstream files,
  3. every resulting file is merged three-way (git merge-file) with the file in this repository:
         base = transform(last import), theirs = transform(new upstream), ours = current file.
     Hand edits made in this repository are kept; overlapping edits leave conflict markers.

Usage:
  tools/sync_from_game.sh                      # merge the game repo's working tree into this repo
  tools/sync_from_game.sh --dry-run            # only report what would change
  tools/sync_from_game.sh --source HEAD        # take a committed revision instead of the working tree
  tools/sync_from_game.sh --record-base        # only remember the current upstream state as the base of the next sync

A merge without conflicts also records the upstream state it took as the new base (tools/sync/upstream_base.tar.xz), so
the next run only sees newer upstream changes. When there were conflicts the base is NOT recorded: resolve the conflict
markers (<<<<<<<), build, run scripts/test.sh and then run `tools/sync_from_game.sh --record-base`.
Afterwards review `git diff` and commit.
"""
import argparse
import hashlib
import io
import os
import re
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sync_rules as rules  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
BASE_TAR = os.path.join(HERE, "upstream_base.tar.xz")
BASE_INFO = os.path.join(HERE, "upstream_base.txt")
UPSTREAM_SUBDIR = "Dependencies/WebD3D8"
DEFAULT_GAME_REPO = os.environ.get("DXWEBGL2_GAME_REPO", "/home/user/CnC_Generals_Zero_Hour")


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def read_tree(path):
    """All text files below path as {relative path: text}."""
    out = {}
    for dp, dn, fn in os.walk(path):
        dn[:] = sorted(d for d in dn if d not in ("node_modules", ".git"))
        for f in sorted(fn):
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, path).replace(os.sep, "/")
            with open(full, "rb") as fh:
                data = fh.read()
            if rel.endswith(".png") or b"\0" in data:
                continue
            out[rel] = data.decode("utf-8")
    return out


def read_git_rev(repo, rev):
    blob = subprocess.run(["git", "-C", repo, "archive", rev, UPSTREAM_SUBDIR], check=True, capture_output=True).stdout
    out = {}
    with tarfile.open(fileobj=io.BytesIO(blob)) as tf:
        for m in tf.getmembers():
            if not m.isfile():
                continue
            rel = m.name[len(UPSTREAM_SUBDIR) + 1:]
            data = tf.extractfile(m).read()
            if rel.endswith(".png") or b"\0" in data:
                continue
            out[rel] = data.decode("utf-8")
    return out


def read_base():
    if not os.path.exists(BASE_TAR):
        return {}
    out = {}
    with tarfile.open(BASE_TAR) as tf:
        for m in tf.getmembers():
            if m.isfile():
                out[m.name] = tf.extractfile(m).read().decode("utf-8")
    return out


def write_base(raw):
    """Deterministic tarball of the raw upstream snapshot."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:xz") as tf:
        for name in sorted(raw):
            data = raw[name].encode("utf-8")
            ti = tarfile.TarInfo(name)
            ti.size = len(data)
            ti.mtime = 0
            ti.mode = 0o644
            tf.addfile(ti, io.BytesIO(data))
    with open(BASE_TAR, "wb") as f:
        f.write(buf.getvalue())


def record_base(new_raw, desc, game_repo):
    write_base(new_raw)
    digest = hashlib.sha256("".join(new_raw[k] for k in sorted(new_raw)).encode()).hexdigest()[:16]
    rev = ""
    try:
        rev = run(["git", "-C", game_repo, "rev-parse", "HEAD"]).strip()
    except Exception:
        pass
    with open(BASE_INFO, "w") as f:
        f.write("source: %s\ngame repository HEAD at that time: %s\ncontent digest: %s\nfiles: %d\n"
                % (desc, rev, digest, len(new_raw)))
    print("base snapshot recorded (%s)" % digest)


def merge3(ours, base, theirs):
    """git merge-file; returns (text, conflict count)."""
    with tempfile.TemporaryDirectory() as td:
        paths = []
        for name, text in (("ours", ours), ("base", base), ("theirs", theirs)):
            p = os.path.join(td, name)
            with open(p, "w", encoding="utf-8", newline="") as f:
                f.write(text)
            paths.append(p)
        r = subprocess.run(["git", "merge-file", "-p", "-L", "dxwebgl2 (this repository)", "-L", "last import",
                            "-L", "new upstream"] + paths, capture_output=True, text=True)
        if r.returncode < 0 or r.returncode > 127:
            raise SystemExit("git merge-file failed: " + r.stderr)
        return r.stdout, r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game-repo", default=DEFAULT_GAME_REPO, help="path of the game repository (default: %(default)s)")
    ap.add_argument("--source", default="worktree",
                    help="'worktree' (default: the files as they are on disk) or a git revision of the game repository")
    ap.add_argument("--base-rev", help="take the 'last import' snapshot from this revision of the game repository "
                                       "instead of tools/sync/upstream_base.tar.xz")
    ap.add_argument("--dry-run", action="store_true", help="do not write anything, only report")
    ap.add_argument("--record-base", action="store_true",
                    help="do not merge: only store the upstream state as the base of the next sync (after resolving conflicts by hand)")
    ap.add_argument("--no-record", action="store_true",
                    help="do not store the upstream state as the new base after a merge without conflicts")
    ap.add_argument("--self-check", action="store_true",
                    help="only check that the stored base snapshot transforms without errors (no game repository needed)")
    ap.add_argument("--init", action="store_true", help="first import only: no base snapshot, create the files that do not exist yet")
    a = ap.parse_args()

    if a.self_check:
        base_raw = read_base()
        if not base_raw:
            raise SystemExit("no base snapshot")
        warnings = []
        out = rules.transform(base_raw, warnings=warnings)
        print("base snapshot (%d files) transforms into %d files, %d warnings" % (len(base_raw), len(out), len(warnings)))
        for w in warnings:
            print("warning: " + w)
        return 1 if warnings else 0

    # --- upstream (new) -------------------------------------------------------
    if a.source == "worktree":
        src = os.path.join(a.game_repo, UPSTREAM_SUBDIR)
        if not os.path.isdir(src):
            raise SystemExit("not found: %s (use --game-repo)" % src)
        new_raw = read_tree(src)
        desc = "working tree of %s" % a.game_repo
    else:
        new_raw = read_git_rev(a.game_repo, a.source)
        desc = "revision %s of %s" % (a.source, a.game_repo)
    print("upstream: %s (%d files)" % (desc, len(new_raw)))

    if a.record_base:
        record_base(new_raw, desc, a.game_repo)
        return 0

    # --- last import ----------------------------------------------------------
    if a.base_rev:
        base_raw = read_git_rev(a.game_repo, a.base_rev)
    elif a.init:
        base_raw = {}
    else:
        base_raw = read_base()
        if not base_raw:
            raise SystemExit("no base snapshot (tools/sync/upstream_base.tar.xz); use --base-rev REV or --init")

    warnings = []
    base_out = rules.transform(base_raw, warnings=[]) if base_raw else {}
    new_out = rules.transform(new_raw, warnings=warnings)

    changed, conflicts, created, same, removed = [], [], [], [], []
    plan = {}
    for dest in sorted(set(base_out) | set(new_out)):
        b, t = base_out.get(dest), new_out.get(dest)
        path = os.path.join(ROOT, dest)
        ours = open(path, encoding="utf-8", newline="").read() if os.path.exists(path) else None
        if t is None:
            removed.append(dest)  # upstream no longer has it: never delete automatically
            continue
        if ours is None:
            plan[dest] = t
            created.append(dest)
            continue
        if b is None:
            b = ""
        if t == b:
            same.append(dest)
            continue
        if ours == b:
            plan[dest] = t
            changed.append(dest)
            continue
        merged, nconf = merge3(ours, b, t)
        if merged == ours:
            same.append(dest)
            continue
        plan[dest] = merged
        (conflicts if nconf else changed).append(dest)
        if nconf:
            warnings.append("%s: %d conflict(s), look for <<<<<<<" % (dest, nconf))

    def show(title, items):
        if items:
            print("%s (%d):" % (title, len(items)))
            for i in items:
                print("   " + i)

    show("new files", created)
    show("updated (merged cleanly)", changed)
    show("CONFLICTS", conflicts)
    show("removed upstream (left in place, delete by hand if wanted)", removed)
    print("unchanged: %d files" % len(same))
    for w in warnings:
        print("warning: " + w)

    if a.dry_run:
        print("dry run: nothing written")
        return 0
    for dest, text in plan.items():
        path = os.path.join(ROOT, dest)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
    if not a.no_record:
        if conflicts:
            print("the base snapshot was NOT updated: resolve the conflicts, build, test, then run "
                  "`tools/sync_from_game.sh --record-base`")
        else:
            record_base(new_raw, desc, a.game_repo)
    # Generated compatibility files follow the public header.
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_webd3d8_compat.py")], check=True)
    print("done. Review with `git diff`, build and run scripts/test.sh.")
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
