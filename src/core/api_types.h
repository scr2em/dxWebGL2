/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * The API vocabulary the core shares with the Direct3D front ends.
 *
 * The core does not implement or call any COM interface (tools/check_layering.py enforces that no
 * IDirect3D* type is mentioned below src/core), but it speaks in Direct3D terms where the numbers are the same in Direct3D 8 and 9: D3DFORMAT, D3DPOOL, the FVF and
 * shader token bit layouts, D3DMATRIX, the render state and texture stage state numbering. This is the
 * single header that decides where those come from; a Direct3D 9 front end switches it to
 * <d3d9types.h> (a superset of what the core uses), see docs/ROADMAP.md.
 */
#pragma once

#include <dxwebgl2/win32_min.h>

#include <d3d8.h>
