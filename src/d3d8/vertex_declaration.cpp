/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Vertex layouts from Direct3D 8 D3DVSD_* declarations.
 */
#include "d3d8/device.h"

namespace dxwebgl2 {

bool ParseVertexDeclaration(const DWORD *decl, VertexLayout &out, std::vector<float> *constants)
{
    out = VertexLayout();
    out.id = NextVertexLayoutId();
    uint16_t offsets[MAX_STREAMS] = {};
    unsigned stream = 0;
    for (int guard = 0; guard < 512; ++guard)
    {
        DWORD t = decl[guard];
        if (t == D3DVSD_END()) return true;
        const DWORD type = (t & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;
        switch (type)
        {
        case D3DVSD_TOKEN_NOP: break;
        case D3DVSD_TOKEN_STREAM:
            stream = t & D3DVSD_STREAMNUMBERMASK;
            if (stream >= MAX_STREAMS) return false;
            break;
        case D3DVSD_TOKEN_STREAMDATA:
            if (t & 0x10000000)
            {
                offsets[stream] += (uint16_t)(4 * ((t & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT));
            }
            else
            {
                const DWORD reg = t & D3DVSD_VERTEXREGMASK;
                const DWORD dtype = (t & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT;
                if (out.count >= 16 || reg >= 16) return false;
                VertexElement &e = out.elems[out.count++];
                e.reg = (uint8_t)reg;
                e.stream = (uint8_t)stream;
                e.offset = offsets[stream];
                e.d3dColor = 0;
                e.normalized = 0;
                e.glType = 0;
                uint16_t bytes = 0;
                switch (dtype)
                {
                case D3DVSDT_FLOAT1: e.size = 1; bytes = 4; break;
                case D3DVSDT_FLOAT2: e.size = 2; bytes = 8; break;
                case D3DVSDT_FLOAT3: e.size = 3; bytes = 12; break;
                case D3DVSDT_FLOAT4: e.size = 4; bytes = 16; break;
                case D3DVSDT_D3DCOLOR: e.size = 4; e.glType = 1; e.normalized = 1; e.d3dColor = 1; bytes = 4; break;
                case D3DVSDT_UBYTE4: e.size = 4; e.glType = 1; bytes = 4; break;
                case D3DVSDT_SHORT2: e.size = 2; e.glType = 2; bytes = 4; break;
                case D3DVSDT_SHORT4: e.size = 4; e.glType = 2; bytes = 8; break;
                default: return false;
                }
                offsets[stream] += bytes;
            }
            break;
        case D3DVSD_TOKEN_CONSTMEM:
        {
            const DWORD count = (t & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT;
            const DWORD addr = t & D3DVSD_CONSTADDRESSMASK;
            for (DWORD i = 0; i < count; ++i)
            {
                if (constants)
                {
                    constants->push_back((float)(addr + i));
                    for (int c = 0; c < 4; ++c) constants->push_back(DwordToFloat(decl[guard + 1 + i * 4 + c]));
                }
            }
            guard += (int)count * 4;
            break;
        }
        case D3DVSD_TOKEN_EXT:
            guard += (int)((t & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT);
            break;
        default: break;
        }
    }
    return false;
}

} // namespace dxwebgl2
