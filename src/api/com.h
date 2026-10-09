/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * COM plumbing shared by the Direct3D front ends: reference counted IUnknown base and an
 * intrusive smart pointer.
 */
#pragma once

#include "core/api_types.h"

#include <utility>

namespace dxwebgl2 {

//------------------------------------------------------------------------------
// COM plumbing
//------------------------------------------------------------------------------
class Device;

/// IUnknown implementation shared by all objects (reference counting only;
/// QueryInterface hands out the object for any interface, as applications of this API never
/// ask for anything but the interface they already have).
template <class Iface>
class Unknown : public Iface
{
public:
    virtual ~Unknown() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<Iface *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --m_refs;
        if (r == 0) OnZeroRefs();
        return r;
    }

protected:
    virtual void OnZeroRefs() { delete this; }
    ULONG m_refs = 1;
};

/// Intrusive smart pointer.
template <class T>
class Ref
{
public:
    Ref() = default;
    Ref(T *p) : m_p(p) { if (m_p) m_p->AddRef(); }
    Ref(const Ref &o) : m_p(o.m_p) { if (m_p) m_p->AddRef(); }
    Ref(Ref &&o) noexcept : m_p(o.m_p) { o.m_p = nullptr; }
    ~Ref() { if (m_p) m_p->Release(); }
    Ref &operator=(const Ref &o) { Ref t(o); std::swap(m_p, t.m_p); return *this; }
    Ref &operator=(T *p) { Ref t(p); std::swap(m_p, t.m_p); return *this; }
    T *operator->() const { return m_p; }
    T *get() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }
private:
    T *m_p = nullptr;
};

} // namespace dxwebgl2
