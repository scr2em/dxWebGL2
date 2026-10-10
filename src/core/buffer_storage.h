/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * CPU-shadowed GPU buffer storage used by vertex and index buffers.
 */
#pragma once

#include "core/backend.h"

namespace dxwebgl2 {

/// Shared CPU-shadow + GPU buffer logic of vertex and index buffers.
class BufferStorage : public GpuObject
{
public:
    BufferStorage(Backend *backend, BufferKind kind, UINT size, DWORD usage);
    ~BufferStorage() override;
    void OnContextLost() override;
    void OnContextRestored() override;
    HRESULT Lock(UINT offset, UINT size, BYTE **ppData, DWORD flags);
    HRESULT Unlock();
    /// Uploads the pending dirty range to the GPU buffer. Returns the GPU buffer (null while the context is lost).
    GpuBuffer *Flush();
    GpuBuffer *Gpu() const { return m_buf; }
    BufferKind Kind() const { return m_kind; }
    UINT Size() const { return (UINT)m_data.size(); }
    const uint8_t *Data() const { return m_data.data(); }

private:
    Backend *m_backend;
    BufferKind m_kind;
    GpuBuffer *m_buf = nullptr;
    std::vector<uint8_t> m_data;
    [[maybe_unused]] DWORD m_usage;
    bool m_locked = false;
    UINT m_lockOffset = 0, m_lockSize = 0;
    DWORD m_lockFlags = 0;
    UINT m_dirtyBegin = 0, m_dirtyEnd = 0; // [begin,end) pending upload
    bool m_allocated = false;
};

} // namespace dxwebgl2
