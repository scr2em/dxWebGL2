/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * ID3DXMatrixStack / D3DXCreateMatrixStack.
 *
 * The stack always holds at least one matrix (the identity after creation); Pop() on the last one succeeds
 * and does nothing. The non-"Local" operations right-multiply the top matrix (top = top * M, the new
 * transformation is applied last, relative to the world frame), the "Local" ones left-multiply it
 * (top = M * top, relative to the object's own frame).
 */
#include "d3d8/d3d8_headers.h"
#include "api/com.h"

#include <vector>

using namespace dxwebgl2;

namespace {

class MatrixStack final : public Unknown<ID3DXMatrixStack>
{
public:
    MatrixStack() : m_stack(1) { D3DXMatrixIdentity(&m_stack.back()); }

    HRESULT STDMETHODCALLTYPE Pop() override
    {
        if (m_stack.size() > 1) m_stack.pop_back();
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Push() override
    {
        m_stack.push_back(m_stack.back());
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LoadIdentity() override
    {
        D3DXMatrixIdentity(&Top());
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LoadMatrix(const D3DXMATRIX *m) override
    {
        if (!m) return D3DERR_INVALIDCALL;
        Top() = *m;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE MultMatrix(const D3DXMATRIX *m) override { return m ? Right(*m) : D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE MultMatrixLocal(const D3DXMATRIX *m) override { return m ? Left(*m) : D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE RotateAxis(const D3DXVECTOR3 *axis, FLOAT angle) override
    {
        if (!axis) return D3DERR_INVALIDCALL;
        D3DXMATRIX r;
        D3DXMatrixRotationAxis(&r, axis, angle);
        return Right(r);
    }
    HRESULT STDMETHODCALLTYPE RotateAxisLocal(const D3DXVECTOR3 *axis, FLOAT angle) override
    {
        if (!axis) return D3DERR_INVALIDCALL;
        D3DXMATRIX r;
        D3DXMatrixRotationAxis(&r, axis, angle);
        return Left(r);
    }
    HRESULT STDMETHODCALLTYPE RotateYawPitchRoll(FLOAT yaw, FLOAT pitch, FLOAT roll) override
    {
        D3DXMATRIX r;
        D3DXMatrixRotationYawPitchRoll(&r, yaw, pitch, roll);
        return Right(r);
    }
    HRESULT STDMETHODCALLTYPE RotateYawPitchRollLocal(FLOAT yaw, FLOAT pitch, FLOAT roll) override
    {
        D3DXMATRIX r;
        D3DXMatrixRotationYawPitchRoll(&r, yaw, pitch, roll);
        return Left(r);
    }
    HRESULT STDMETHODCALLTYPE Scale(FLOAT x, FLOAT y, FLOAT z) override
    {
        D3DXMATRIX s;
        D3DXMatrixScaling(&s, x, y, z);
        return Right(s);
    }
    HRESULT STDMETHODCALLTYPE ScaleLocal(FLOAT x, FLOAT y, FLOAT z) override
    {
        D3DXMATRIX s;
        D3DXMatrixScaling(&s, x, y, z);
        return Left(s);
    }
    HRESULT STDMETHODCALLTYPE Translate(FLOAT x, FLOAT y, FLOAT z) override
    {
        D3DXMATRIX t;
        D3DXMatrixTranslation(&t, x, y, z);
        return Right(t);
    }
    HRESULT STDMETHODCALLTYPE TranslateLocal(FLOAT x, FLOAT y, FLOAT z) override
    {
        D3DXMATRIX t;
        D3DXMatrixTranslation(&t, x, y, z);
        return Left(t);
    }
    D3DXMATRIX *STDMETHODCALLTYPE GetTop() override { return &Top(); }

private:
    D3DXMATRIX &Top() { return m_stack.back(); }
    HRESULT Right(const D3DXMATRIX &m)
    {
        D3DXMatrixMultiply(&Top(), &Top(), &m);
        return D3D_OK;
    }
    HRESULT Left(const D3DXMATRIX &m)
    {
        D3DXMatrixMultiply(&Top(), &m, &Top());
        return D3D_OK;
    }

    std::vector<D3DXMATRIX> m_stack;
};

} // namespace

extern "C" HRESULT WINAPI D3DXCreateMatrixStack(DWORD, LPD3DXMATRIXSTACK *ppStack)
{
    if (!ppStack) return D3DERR_INVALIDCALL;
    *ppStack = new MatrixStack();
    return D3D_OK;
}
