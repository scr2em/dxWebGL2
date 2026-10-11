/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Backend selection, the registry of objects that own GPU objects, and the CPU emulation of primitive types
 * shared by the backends.
 */
#include "core/backend.h"

namespace dxwebgl2 {

Backend *CreateWebGL2Backend(); // core/gl/gl_backend.cpp
#ifdef DXWEBGL2_HAVE_WEBGPU
/// core/webgpu/wgpu_backend.cpp: the WebGPU backend with its adapter and device, or null with the reason in
/// `unavailable` when WebGPU cannot start (the build cannot suspend, no navigator.gpu, no adapter, requestDevice failed).
Backend *CreateWebGPUBackend(std::string &unavailable);
#endif

void PipelineState::Invalidate()
{
    memset(this, 0xFF, sizeof *this);
}

//------------------------------------------------------------------------------
// Selection
//------------------------------------------------------------------------------
Backend *CreateBackend()
{
    Config &cfg = GetConfig();
    Backend *b = nullptr;
    switch (cfg.backend)
    {
    case DXWEBGL2_BACKEND_WEBGPU:
#ifdef DXWEBGL2_HAVE_WEBGPU
    {
        std::string why;
        b = CreateWebGPUBackend(why);
        if (!b)
        {
            Log("the WebGPU backend is not available, using WebGL2: %s", why.c_str());
            DXW_HIT(Approximated, "WebGPU backend requested, WebGL2 used (WebGPU not available)");
        }
    }
#else
        Log("this build has no WebGPU backend (configure with -DDXWEBGL2_WEBGPU=ON), using WebGL2");
        DXW_HIT(Approximated, "WebGPU backend requested, WebGL2 used (not built)");
#endif
        break;
    case DXWEBGL2_BACKEND_AUTO:
        // WebGPU when the build has it, can wait for it (JSPI) and the browser offers an adapter and a device; WebGL2
        // otherwise, with one log line and nothing else (WebGL2 is a full backend, not a degraded mode).
#ifdef DXWEBGL2_HAVE_WEBGPU
        // A canvas keeps the kind of context it was given first: once a device of this canvas rendered through
        // WebGL2, the canvas cannot get a WebGPU context any more.
        if (cfg.activeBackend == DXWEBGL2_BACKEND_WEBGL2 && cfg.activeCanvas == cfg.canvas) break;
        {
            std::string why;
            b = CreateWebGPUBackend(why);
            if (!b) Log("WebGPU is not available, using WebGL2: %s", why.c_str());
        }
#endif
        break;
    case DXWEBGL2_BACKEND_WEBGL2:
        break;
    default:
        Log("unknown backend %d requested, using WebGL2", cfg.backend);
        break;
    }
    if (!b) b = CreateWebGL2Backend();
    cfg.activeBackend = (int)b->Kind();
    cfg.activeCanvas = cfg.canvas;
    return b;
}

//------------------------------------------------------------------------------
// Registry of the objects that own GPU objects
//------------------------------------------------------------------------------
GpuObject::GpuObject(Backend *backend) : m_registry(backend)
{
    if (backend) backend->RegisterObject(this);
}

GpuObject::~GpuObject()
{
    if (m_registry) m_registry->UnregisterObject(this);
}

void Backend::RegisterObject(GpuObject *o)
{
    o->m_prev = nullptr;
    o->m_next = m_objects;
    if (m_objects) m_objects->m_prev = o;
    m_objects = o;
}

void Backend::UnregisterObject(GpuObject *o)
{
    if (o->m_prev) o->m_prev->m_next = o->m_next;
    else if (m_objects == o) m_objects = o->m_next;
    if (o->m_next) o->m_next->m_prev = o->m_prev;
    o->m_prev = o->m_next = nullptr;
}

void Backend::OnContextLost()
{
    NotifyContextLost();
}

void Backend::NotifyContextLost()
{
    for (GpuObject *o = m_objects; o; o = o->m_next) o->OnContextLost();
}

void Backend::NotifyContextRestored()
{
    for (GpuObject *o = m_objects; o; o = o->m_next) o->OnContextRestored();
}

//------------------------------------------------------------------------------
// Primitive emulation
//------------------------------------------------------------------------------
void ExpandTriangles(Topology topology, uint32_t primitiveCount, const IndexFetch &fetch, bool rotate, std::vector<uint32_t> &out)
{
    out.clear();
    out.reserve((size_t)primitiveCount * 3);
    for (uint32_t p = 0; p < primitiveCount; ++p)
    {
        uint32_t a, b, c;
        if (topology == Topology::TriangleList) { a = fetch(p * 3); b = fetch(p * 3 + 1); c = fetch(p * 3 + 2); }
        else if (topology == Topology::TriangleStrip) { a = fetch(p); b = fetch(p + 1 + (p & 1)); c = fetch(p + 2 - (p & 1)); }
        else { a = fetch(0); b = fetch(p + 1); c = fetch(p + 2); }
        if (rotate)
        {
            // (a,b,c) keeps its winding as (b,c,a); the vertex Direct3D shades with ends up last.
            out.push_back(b); out.push_back(c); out.push_back(a);
        }
        else { out.push_back(a); out.push_back(b); out.push_back(c); }
    }
}

void TrianglesToLines(const std::vector<uint32_t> &tri, std::vector<uint32_t> &out)
{
    out.clear();
    out.reserve(tri.size() * 2);
    for (size_t i = 0; i + 2 < tri.size(); i += 3)
    {
        out.push_back(tri[i]); out.push_back(tri[i + 1]);
        out.push_back(tri[i + 1]); out.push_back(tri[i + 2]);
        out.push_back(tri[i + 2]); out.push_back(tri[i]);
    }
}

} // namespace dxwebgl2

extern "C" {

void dxwebgl2_SetBackend(int backend) { dxwebgl2::GetConfig().backend = backend; }

int dxwebgl2_GetBackend(void) { return dxwebgl2::GetConfig().activeBackend; }

} // extern "C"
