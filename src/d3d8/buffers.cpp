/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Vertex and index buffers.
 *
 * The application writes into a system-memory copy through Lock(); the
 * modified range is pushed to the GPU buffer lazily, right before a draw call
 * reads from it, so several locks between two draws cost a single upload
 * (core/buffer_storage.h).
 */
#include "d3d8/resources.h"

namespace dxwebgl2 {

VertexBuffer::VertexBuffer(Device *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool)
    : ResourceImpl<IDirect3DVertexBuffer8>(dev), m_storage(dev->GetBackend(), BufferKind::Vertex, length, usage),
      m_fvf(fvf), m_usage(usage), m_pool(pool)
{
}

HRESULT VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC *d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = D3DFMT_VERTEXDATA;
    d->Type = D3DRTYPE_VERTEXBUFFER;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = m_storage.Size();
    d->FVF = m_fvf;
    return D3D_OK;
}

IndexBuffer::IndexBuffer(Device *dev, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool)
    : ResourceImpl<IDirect3DIndexBuffer8>(dev), m_storage(dev->GetBackend(), BufferKind::Index, length, usage),
      m_format(format), m_usage(usage), m_pool(pool)
{
}

HRESULT IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC *d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_INDEXBUFFER;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = m_storage.Size();
    return D3D_OK;
}

} // namespace dxwebgl2
