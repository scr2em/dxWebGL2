/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Declarations shared between d3d8.cpp and device.cpp.
 */
#pragma once

#include "d3d8/device.h"

namespace dxwebgl2 {

/// Fills D3DCAPS8; `gl` may be null before a context exists.
void FillDeviceCaps(D3DCAPS8 *caps, const GpuCaps *gl);
void SetRendererString(const std::string &s);
IDirect3D8 *CreateDirect3D8();

} // namespace dxwebgl2
