/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * CPU-shadowed GL buffer storage: Lock() writes into a system-memory copy, the modified range is
 * pushed to the GL buffer lazily, right before a draw call reads from it, so several locks between
 * two draws cost a single upload.
 */
#include "core/buffer_storage.h"
#include "core/gl_state.h"

namespace dxwebgl2 {

BufferStorage::BufferStorage(GLState *dev, GLenum target, UINT size, DWORD usage)
    : GLObject(dev), m_dev(dev), m_target(target), m_data(size, 0), m_usage(usage)
{
    glGenBuffers(1, &m_buf);
}

void BufferStorage::OnContextRestored()
{
    // The system memory copy is authoritative: the whole buffer is uploaded again by the next Flush().
    glGenBuffers(1, &m_buf);
    m_allocated = false;
    m_dirtyBegin = m_dirtyEnd = 0;
}

BufferStorage::~BufferStorage()
{
    if (m_buf)
    {
        m_dev->ForgetBuffer(m_buf);
        glDeleteBuffers(1, &m_buf);
    }
}

HRESULT BufferStorage::Lock(UINT offset, UINT size, BYTE **ppData, DWORD flags)
{
    if (!ppData) return D3DERR_INVALIDCALL;
    if (m_locked) return D3DERR_INVALIDCALL;
    if (offset > m_data.size()) return D3DERR_INVALIDCALL;
    if (size == 0) size = (UINT)m_data.size() - offset;
    if ((uint64_t)offset + size > m_data.size()) return D3DERR_INVALIDCALL;
    m_locked = true;
    m_lockOffset = offset;
    m_lockSize = size;
    m_lockFlags = flags;
    *ppData = m_data.data() + offset;
    return D3D_OK;
}

HRESULT BufferStorage::Unlock()
{
    if (!m_locked) return D3DERR_INVALIDCALL;
    m_locked = false;
    if (m_lockFlags & D3DLOCK_READONLY) return D3D_OK;
    UINT b = m_lockOffset, e = m_lockOffset + m_lockSize;
    if (m_dirtyBegin == m_dirtyEnd) { m_dirtyBegin = b; m_dirtyEnd = e; }
    else { m_dirtyBegin = Min(m_dirtyBegin, b); m_dirtyEnd = Max(m_dirtyEnd, e); }
    return D3D_OK;
}

GLuint BufferStorage::Flush()
{
    if (!m_allocated)
    {
        // WebGL fixes a buffer's role on first use: index buffers may only ever
        // be bound to ELEMENT_ARRAY_BUFFER, vertex buffers to ARRAY_BUFFER, so
        // uploads go through the buffer's own target.
        m_dev->BindBuffer(m_target, m_buf);
        glBufferData(m_target, (GLsizeiptr)m_data.size(), m_data.data(), GL_DYNAMIC_DRAW);
        m_allocated = true;
        m_dirtyBegin = m_dirtyEnd = 0;
    }
    else if (m_dirtyBegin != m_dirtyEnd)
    {
        m_dev->BindBuffer(m_target, m_buf);
        glBufferSubData(m_target, m_dirtyBegin, m_dirtyEnd - m_dirtyBegin, m_data.data() + m_dirtyBegin);
        m_dirtyBegin = m_dirtyEnd = 0;
    }
    return m_buf;
}

} // namespace dxwebgl2
