/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * d3dx8.h
 *
 * The D3DX 8 subset implemented by dxWebGL2: math (d3dx8math.h), buffers,
 * error strings and the shader assembler (d3dx8core.h) and texture/surface
 * helpers (d3dx8tex.h). Meshes, shapes, effects, fonts and sprites of the full
 * D3DX 8 library are not provided, see docs/COVERAGE.md.
 */
#ifndef __D3DX8_H__
#define __D3DX8_H__

#include <d3d8.h>
#include <float.h>
#include <limits.h>

#ifndef D3DXINLINE
#ifdef __cplusplus
#define D3DXINLINE inline
#else
#define D3DXINLINE static inline
#endif
#endif

#define D3DX_DEFAULT ULONG_MAX
#define D3DX_DEFAULT_FLOAT FLT_MAX

#include "d3dx8math.h"
#include "d3dx8core.h"
#include "d3dx8tex.h"

#endif /* __D3DX8_H__ */
