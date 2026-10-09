/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Vertex layouts from FVF codes; layouts are attribute lists bound to the D3DVSDE_* register numbers,
 * which double as GL attribute locations.
 */
#include "core/types.h"

namespace dxwebgl2 {

UINT FVFVertexSize(DWORD fvf)
{
    UINT size = 0;
    switch (fvf & D3DFVF_POSITION_MASK)
    {
    case D3DFVF_XYZ: size += 12; break;
    case D3DFVF_XYZRHW: size += 16; break;
    case D3DFVF_XYZB1: size += 16; break;
    case D3DFVF_XYZB2: size += 20; break;
    case D3DFVF_XYZB3: size += 24; break;
    case D3DFVF_XYZB4: size += 28; break;
    case D3DFVF_XYZB5: size += 32; break;
    }
    if (fvf & D3DFVF_NORMAL) size += 12;
    if (fvf & D3DFVF_PSIZE) size += 4;
    if (fvf & D3DFVF_DIFFUSE) size += 4;
    if (fvf & D3DFVF_SPECULAR) size += 4;
    const UINT texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (UINT i = 0; i < texCount; ++i)
    {
        switch ((fvf >> (16 + i * 2)) & 3)
        {
        case D3DFVF_TEXTUREFORMAT1: size += 4; break;
        case D3DFVF_TEXTUREFORMAT2: size += 8; break;
        case D3DFVF_TEXTUREFORMAT3: size += 12; break;
        case D3DFVF_TEXTUREFORMAT4: size += 16; break;
        }
    }
    return size;
}

uint32_t NextVertexLayoutId()
{
    static uint32_t next = 1;
    return next++;
}

const VertexLayout *LayoutFromFVF(DWORD fvf)
{
    static std::unordered_map<DWORD, VertexLayout *> cache;
    auto it = cache.find(fvf);
    if (it != cache.end()) return it->second;

    VertexLayout *l = new VertexLayout();
    l->fvf = fvf;
    l->id = NextVertexLayoutId();
    uint16_t off = 0;
    auto add = [&](uint8_t reg, uint8_t size, uint8_t glType, uint8_t norm, uint8_t d3dColor, uint16_t bytes) {
        VertexElement &e = l->elems[l->count++];
        e.reg = reg; e.stream = 0; e.offset = off; e.size = size; e.glType = glType; e.normalized = norm; e.d3dColor = d3dColor;
        off += bytes;
    };
    switch (fvf & D3DFVF_POSITION_MASK)
    {
    case D3DFVF_XYZ: add(D3DVSDE_POSITION, 3, 0, 0, 0, 12); break;
    case D3DFVF_XYZRHW: add(D3DVSDE_POSITION, 4, 0, 0, 0, 16); break;
    case D3DFVF_XYZB1: case D3DFVF_XYZB2: case D3DFVF_XYZB3: case D3DFVF_XYZB4: case D3DFVF_XYZB5:
    {
        // Blend weights are skipped: engines that skin on the CPU do not need the weights.
        int weights = (int)(((fvf & D3DFVF_POSITION_MASK) - D3DFVF_XYZB1) / 2) + 1;
        add(D3DVSDE_POSITION, 3, 0, 0, 0, 12);
        off += (uint16_t)(weights * 4);
        break;
    }
    }
    if (fvf & D3DFVF_NORMAL) add(D3DVSDE_NORMAL, 3, 0, 0, 0, 12);
    if (fvf & D3DFVF_PSIZE) add(D3DVSDE_PSIZE, 1, 0, 0, 0, 4);
    if (fvf & D3DFVF_DIFFUSE) add(D3DVSDE_DIFFUSE, 4, 1, 1, 1, 4);
    if (fvf & D3DFVF_SPECULAR) add(D3DVSDE_SPECULAR, 4, 1, 1, 1, 4);
    const UINT texCount = Min<UINT>((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT, 8);
    for (UINT i = 0; i < texCount; ++i)
    {
        switch ((fvf >> (16 + i * 2)) & 3)
        {
        case D3DFVF_TEXTUREFORMAT1: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 1, 0, 0, 0, 4); break;
        case D3DFVF_TEXTUREFORMAT2: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 2, 0, 0, 0, 8); break;
        case D3DFVF_TEXTUREFORMAT3: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 3, 0, 0, 0, 12); break;
        case D3DFVF_TEXTUREFORMAT4: add((uint8_t)(D3DVSDE_TEXCOORD0 + i), 4, 0, 0, 0, 16); break;
        }
    }
    l->stride[0] = off;
    cache[fvf] = l;
    return l;
}

} // namespace dxwebgl2
