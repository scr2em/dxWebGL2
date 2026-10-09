/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * CPU-shadowed GL buffer storage used by vertex and index buffers.
 */
#pragma once

#include "core/types.h"

namespace dxwebgl2 {

class GLState;

/// Shared CPU-shadow + GL buffer logic of vertex and index buffers.
class BufferStorage : public GLObject
{
public:
    BufferStorage(GLState *dev, GLenum target, UINT size, DWORD usage);
    ~BufferStorage() override;
    void OnContextLost() override { m_buf = 0; m_allocated = false; }
    void OnContextRestored() override;
    HRESULT Lock(UINT offset, UINT size, BYTE **ppData, DWORD flags);
    HRESULT Unlock();
    /// Uploads the pending dirty range to the GL buffer. Returns the GL buffer.
    GLuint Flush();
    GLuint Id() const { return m_buf; }
    UINT Size() const { return (UINT)m_data.size(); }
    const uint8_t *Data() const { return m_data.data(); }

private:
    GLState *m_dev;
    GLenum m_target;
    GLuint m_buf = 0;
    std::vector<uint8_t> m_data;
    [[maybe_unused]] DWORD m_usage;
    bool m_locked = false;
    UINT m_lockOffset = 0, m_lockSize = 0;
    DWORD m_lockFlags = 0;
    UINT m_dirtyBegin = 0, m_dirtyEnd = 0; // [begin,end) pending upload
    bool m_allocated = false;
};

} // namespace dxwebgl2
