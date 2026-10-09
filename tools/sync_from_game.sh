#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
#
# Brings the changes of Dependencies/WebD3D8 in the Generals Zero Hour web port repository over into this repository:
# the same mechanical transformation as for the first import is applied to the old and to the new upstream files, and
# the two results are merged three-way with the files here (hand edits are kept, overlaps become conflict markers).
#
#   tools/sync_from_game.sh [--game-repo PATH] [--source worktree|REV] [--dry-run] [--record-base] [--no-record]
#
# The game repository defaults to $DXWEBGL2_GAME_REPO or /home/user/CnC_Generals_Zero_Hour. See docs/SYNC.md.
set -euo pipefail
exec python3 "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sync/sync_from_game.py" "$@"
