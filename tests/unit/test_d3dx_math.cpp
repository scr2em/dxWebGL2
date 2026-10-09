/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Tests of the D3DX8 math functions (left-handed, row vectors, row-major matrices) and of the matrix stack.
 * Needs no GL context: runs in node.
 */
#include <d3d8.h>
#include <d3dx8.h>

#include <math.h>
#include <stdio.h>

static int g_fail = 0, g_checks = 0;

// Pointer to a copy of a temporary (clang rejects &T(...), the Microsoft compiler accepts it).
template <class T> static const T *P(const T &v)
{
    static T slots[16];
    static unsigned next = 0;
    T *s = &slots[next++ % 16];
    *s = v;
    return s;
}

static bool Near(float a, float b, float eps = 1e-4f) { return fabsf(a - b) <= eps * (1.0f + fabsf(b)); }

#define CHECK(c)                                                  \
    do {                                                          \
        ++g_checks;                                               \
        if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); ++g_fail; } \
    } while (0)

static bool NearV(const D3DXVECTOR3 &v, float x, float y, float z, float eps = 1e-4f)
{
    return Near(v.x, x, eps) && Near(v.y, y, eps) && Near(v.z, z, eps);
}

static bool NearM(const D3DXMATRIX &a, const D3DXMATRIX &b, float eps = 1e-4f)
{
    for (int i = 0; i < 16; ++i)
        if (!Near((&a._11)[i], (&b._11)[i], eps)) return false;
    return true;
}

static void TestVectors()
{
    D3DXVECTOR3 a(1, 2, 3), b(4, 5, 6), r;
    CHECK(Near(D3DXVec3Dot(&a, &b), 32.0f));
    D3DXVec3Cross(&r, &a, &b);
    CHECK(NearV(r, -3, 6, -3));
    CHECK(Near(D3DXVec3Length(&a), sqrtf(14.0f)) && Near(D3DXVec3LengthSq(&a), 14.0f));
    D3DXVec3Normalize(&r, &a);
    CHECK(Near(D3DXVec3Length(&r), 1.0f));
    D3DXVec3Lerp(&r, &a, &b, 0.5f);
    CHECK(NearV(r, 2.5f, 3.5f, 4.5f));
    D3DXVec3Minimize(&r, &a, &b);
    CHECK(NearV(r, 1, 2, 3));
    D3DXVec3Maximize(&r, &a, &b);
    CHECK(NearV(r, 4, 5, 6));
    r = a + b * 2.0f - D3DXVECTOR3(1, 1, 1);
    CHECK(NearV(r, 8, 11, 14));
    CHECK(a == D3DXVECTOR3(1, 2, 3) && a != b);

    D3DXVECTOR2 u(1, 0), v(0, 1), w;
    CHECK(Near(D3DXVec2CCW(&u, &v), 1.0f));
    D3DXVec2Normalize(&w, P(D3DXVECTOR2(3, 4)));
    CHECK(Near(w.x, 0.6f) && Near(w.y, 0.8f));

    D3DXVECTOR4 p(1, 2, 3, 4), q(2, 0, 1, 1), o;
    CHECK(Near(D3DXVec4Dot(&p, &q), 2 + 0 + 3 + 4));
    D3DXVec4Scale(&o, &p, 2.0f);
    CHECK(Near(o.w, 8.0f));
    D3DXVECTOR4 c1(1, 0, 0, 0), c2(0, 1, 0, 0), c3(0, 0, 1, 0);
    D3DXVec4Cross(&o, &c1, &c2, &c3);
    CHECK(Near(D3DXVec4Dot(&o, &c1), 0) && Near(D3DXVec4Dot(&o, &c2), 0) && Near(D3DXVec4Dot(&o, &c3), 0) &&
          Near(fabsf(o.w), 1.0f));

    // Curves: the end points.
    D3DXVECTOR3 v0(0, 0, 0), v1(1, 0, 0), v2(2, 1, 0), v3(3, 1, 0), t1(1, 0, 0), t2(1, 0, 0);
    D3DXVec3Hermite(&r, &v1, &t1, &v2, &t2, 0.0f);
    CHECK(NearV(r, 1, 0, 0));
    D3DXVec3Hermite(&r, &v1, &t1, &v2, &t2, 1.0f);
    CHECK(NearV(r, 2, 1, 0));
    D3DXVec3CatmullRom(&r, &v0, &v1, &v2, &v3, 0.0f);
    CHECK(NearV(r, 1, 0, 0));
    D3DXVec3CatmullRom(&r, &v0, &v1, &v2, &v3, 1.0f);
    CHECK(NearV(r, 2, 1, 0));
    D3DXVec3BaryCentric(&r, &v1, &v2, &v3, 0.0f, 0.0f);
    CHECK(NearV(r, 1, 0, 0));
    D3DXVec3BaryCentric(&r, &v1, &v2, &v3, 1.0f, 0.0f);
    CHECK(NearV(r, 2, 1, 0));
    D3DXVec3BaryCentric(&r, &v1, &v2, &v3, 0.0f, 1.0f);
    CHECK(NearV(r, 3, 1, 0));
}

static void TestMatrices()
{
    D3DXMATRIX m, n, inv, id;
    D3DXMatrixIdentity(&id);
    CHECK(D3DXMatrixIsIdentity(&id));

    // Row-vector convention: translation lives in the fourth row.
    D3DXMatrixTranslation(&m, 1, 2, 3);
    CHECK(m._41 == 1 && m._42 == 2 && m._43 == 3 && m._44 == 1);
    D3DXVECTOR3 p(1, 1, 1), r;
    D3DXVec3TransformCoord(&r, &p, &m);
    CHECK(NearV(r, 2, 3, 4));
    D3DXVec3TransformNormal(&r, &p, &m);
    CHECK(NearV(r, 1, 1, 1));
    D3DXVECTOR4 h;
    D3DXVec3Transform(&h, &p, &m);
    CHECK(Near(h.x, 2) && Near(h.w, 1));

    // Left-handed rotations: a positive angle about Y turns +Z towards +X.
    D3DXMatrixRotationY(&m, D3DX_PI / 2);
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 1)), &m);
    CHECK(NearV(r, 1, 0, 0));
    D3DXMatrixRotationX(&m, D3DX_PI / 2);
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 1, 0)), &m);
    CHECK(NearV(r, 0, 0, 1));
    D3DXMatrixRotationZ(&m, D3DX_PI / 2);
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(1, 0, 0)), &m);
    CHECK(NearV(r, 0, 1, 0));
    D3DXMatrixRotationAxis(&n, P(D3DXVECTOR3(0, 0, 1)), D3DX_PI / 2);
    CHECK(NearM(m, n));
    D3DXMATRIX ypr, ry, rx, rz, prod;
    D3DXMatrixRotationYawPitchRoll(&ypr, 0.3f, 0.2f, 0.1f);
    D3DXMatrixRotationZ(&rz, 0.1f);
    D3DXMatrixRotationX(&rx, 0.2f);
    D3DXMatrixRotationY(&ry, 0.3f);
    D3DXMatrixMultiply(&prod, &rz, &rx);
    D3DXMatrixMultiply(&prod, &prod, &ry);
    CHECK(NearM(ypr, prod));

    // Scaling, determinant, inverse, transpose.
    D3DXMatrixScaling(&m, 2, 3, 4);
    CHECK(Near(D3DXMatrixfDeterminant(&m), 24.0f));
    float det = 0;
    CHECK(D3DXMatrixInverse(&inv, &det, &m) == &inv && Near(det, 24.0f));
    D3DXMatrixMultiply(&prod, &m, &inv);
    CHECK(NearM(prod, id));
    D3DXMatrixRotationYawPitchRoll(&m, 0.5f, -0.3f, 1.1f);
    D3DXMatrixTranslation(&n, 4, -5, 6);
    D3DXMatrixMultiply(&m, &m, &n);
    D3DXMatrixInverse(&inv, nullptr, &m);
    D3DXMatrixMultiply(&prod, &m, &inv);
    CHECK(NearM(prod, id));
    D3DXMatrixTranspose(&n, &m);
    CHECK(n._12 == m._21 && n._43 == m._34);
    D3DXMATRIX singular;
    D3DXMatrixScaling(&singular, 1, 0, 1);
    CHECK(D3DXMatrixInverse(&inv, nullptr, &singular) == nullptr);

    // Operators.
    D3DXMATRIX a(1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4);
    D3DXMATRIX b = a * 2.0f;
    CHECK(b._33 == 6 && b(3, 3) == 8);
    b += a;
    CHECK(b._22 == 6);
    CHECK((a * id) == a);

    // Cameras and projections.
    D3DXMatrixLookAtLH(&m, P(D3DXVECTOR3(0, 0, -5)), P(D3DXVECTOR3(0, 0, 0)), P(D3DXVECTOR3(0, 1, 0)));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 0)), &m);
    CHECK(NearV(r, 0, 0, 5));
    D3DXMatrixLookAtRH(&m, P(D3DXVECTOR3(0, 0, 5)), P(D3DXVECTOR3(0, 0, 0)), P(D3DXVECTOR3(0, 1, 0)));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 0)), &m);
    CHECK(NearV(r, 0, 0, -5));
    D3DXMatrixPerspectiveFovLH(&m, D3DX_PI / 2, 1.0f, 1.0f, 11.0f);
    CHECK(Near(m._11, 1.0f) && Near(m._22, 1.0f) && Near(m._34, 1.0f));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 1)), &m);
    CHECK(Near(r.z, 0.0f));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 11)), &m);
    CHECK(Near(r.z, 1.0f));
    D3DXMatrixOrthoLH(&m, 4, 2, 1, 11);
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(2, 1, 11)), &m);
    CHECK(NearV(r, 1, 1, 1));
    D3DXMatrixOrthoOffCenterLH(&m, 0, 4, 0, 2, 1, 11);
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(4, 2, 1)), &m);
    CHECK(NearV(r, 1, 1, 0));
    D3DXMatrixPerspectiveOffCenterLH(&m, -1, 1, -1, 1, 1, 11);
    D3DXMatrixPerspectiveFovLH(&n, D3DX_PI / 2, 1.0f, 1.0f, 11.0f);
    CHECK(NearM(m, n));

    // Reflection and affine transformation.
    D3DXMatrixReflect(&m, P(D3DXPLANE(0, 1, 0, 0)));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(1, 2, 3)), &m);
    CHECK(NearV(r, 1, -2, 3));
    D3DXMatrixAffineTransformation(&m, 2.0f, nullptr, nullptr, P(D3DXVECTOR3(1, 2, 3)));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(1, 1, 1)), &m);
    CHECK(NearV(r, 3, 4, 5));
    D3DXMatrixTransformation(&m, nullptr, nullptr, P(D3DXVECTOR3(2, 2, 2)), nullptr, nullptr, P(D3DXVECTOR3(1, 2, 3)));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(1, 1, 1)), &m);
    CHECK(NearV(r, 3, 4, 5));

    // Projection to the screen and back.
    D3DVIEWPORT8 vp = {0, 0, 640, 480, 0.0f, 1.0f};
    D3DXMATRIX proj, view, world;
    D3DXMatrixPerspectiveFovLH(&proj, D3DX_PI / 3, 640.0f / 480.0f, 1.0f, 100.0f);
    D3DXMatrixLookAtLH(&view, P(D3DXVECTOR3(3, 2, -8)), P(D3DXVECTOR3(0, 0, 0)), P(D3DXVECTOR3(0, 1, 0)));
    D3DXMatrixTranslation(&world, 1, 0, 1);
    D3DXVECTOR3 world_pt(0.5f, 0.25f, -0.5f), screen, back;
    D3DXVec3Project(&screen, &world_pt, &vp, &proj, &view, &world);
    D3DXVec3Unproject(&back, &screen, &vp, &proj, &view, &world);
    CHECK(NearV(back, 0.5f, 0.25f, -0.5f, 1e-3f));
    CHECK(screen.x > 0 && screen.x < 640 && screen.y > 0 && screen.y < 480 && screen.z > 0 && screen.z < 1);
}

static void TestQuaternions()
{
    D3DXQUATERNION q, r, id;
    D3DXQuaternionIdentity(&id);
    CHECK(D3DXQuaternionIsIdentity(&id));
    D3DXQuaternionRotationAxis(&q, P(D3DXVECTOR3(0, 1, 0)), D3DX_PI / 2);
    CHECK(Near(q.y, sinf(D3DX_PI / 4)) && Near(q.w, cosf(D3DX_PI / 4)) && Near(q.x, 0) && Near(q.z, 0));
    D3DXMATRIX m, m2;
    D3DXMatrixRotationQuaternion(&m, &q);
    D3DXMatrixRotationY(&m2, D3DX_PI / 2);
    CHECK(NearM(m, m2));
    D3DXQuaternionRotationMatrix(&r, &m2);
    CHECK(Near(r.y, q.y) && Near(r.w, q.w));

    D3DXQUATERNION inv, prod;
    D3DXQuaternionInverse(&inv, &q);
    D3DXQuaternionMultiply(&prod, &q, &inv);
    CHECK(Near(prod.x, 0) && Near(prod.y, 0) && Near(prod.z, 0) && Near(prod.w, 1));
    D3DXQuaternionConjugate(&inv, &q);
    CHECK(Near(inv.y, -q.y) && Near(inv.w, q.w));
    D3DXQuaternionNormalize(&r, P(D3DXQUATERNION(0, 2, 0, 2)));
    CHECK(Near(D3DXQuaternionLength(&r), 1.0f));

    D3DXQuaternionSlerp(&r, &id, &q, 0.5f);
    D3DXQUATERNION half;
    D3DXQuaternionRotationAxis(&half, P(D3DXVECTOR3(0, 1, 0)), D3DX_PI / 4);
    CHECK(Near(r.y, half.y) && Near(r.w, half.w));
    D3DXQuaternionSlerp(&r, &id, &q, 0.0f);
    CHECK(Near(r.w, 1.0f));
    D3DXQuaternionSlerp(&r, &id, &q, 1.0f);
    CHECK(Near(r.y, q.y));

    D3DXVECTOR3 axis;
    float angle = 0;
    D3DXQuaternionToAxisAngle(&q, &axis, &angle);
    D3DXVec3Normalize(&axis, &axis); // the axis is returned with the length of sin(angle / 2)
    CHECK(NearV(axis, 0, 1, 0) && Near(angle, D3DX_PI / 2));
    D3DXQuaternionRotationYawPitchRoll(&r, 0.3f, 0.2f, 0.1f);
    D3DXMATRIX ypr;
    D3DXMatrixRotationYawPitchRoll(&ypr, 0.3f, 0.2f, 0.1f);
    D3DXMatrixRotationQuaternion(&m, &r);
    CHECK(NearM(m, ypr));

    D3DXQuaternionLn(&r, &id);
    CHECK(Near(r.x, 0) && Near(r.y, 0) && Near(r.z, 0) && Near(r.w, 0));
    D3DXQuaternionExp(&r, P(D3DXQUATERNION(0, 0, 0, 0)));
    CHECK(Near(r.w, 1.0f));
    D3DXQuaternionBaryCentric(&r, &id, &q, &half, 0.0f, 0.0f);
    CHECK(Near(r.w, 1.0f));
}

static void TestPlanesAndColors()
{
    D3DXPLANE p;
    D3DXPlaneFromPointNormal(&p, P(D3DXVECTOR3(0, 1, 0)), P(D3DXVECTOR3(0, 1, 0)));
    CHECK(Near(p.a, 0) && Near(p.b, 1) && Near(p.c, 0) && Near(p.d, -1));
    CHECK(Near(D3DXPlaneDotCoord(&p, P(D3DXVECTOR3(0, 3, 0))), 2.0f));
    CHECK(Near(D3DXPlaneDotNormal(&p, P(D3DXVECTOR3(5, 3, 0))), 3.0f));
    D3DXVECTOR3 hit;
    D3DXPlaneIntersectLine(&hit, &p, P(D3DXVECTOR3(0, 0, 0)), P(D3DXVECTOR3(0, 2, 0)));
    CHECK(NearV(hit, 0, 1, 0));
    D3DXPlaneFromPoints(&p, P(D3DXVECTOR3(0, 0, 0)), P(D3DXVECTOR3(0, 0, 1)), P(D3DXVECTOR3(1, 0, 0)));
    CHECK(Near(fabsf(p.b), 1.0f) && Near(p.d, 0.0f));
    D3DXPlaneNormalize(&p, P(D3DXPLANE(0, 2, 0, -4)));
    CHECK(Near(p.b, 1.0f) && Near(p.d, -2.0f));

    D3DXCOLOR c(0xFF804020);
    CHECK(Near(c.a, 1.0f) && Near(c.r, 128.0f / 255) && Near(c.g, 64.0f / 255) && Near(c.b, 32.0f / 255));
    CHECK((DWORD)c == 0xFF804020);
    D3DXCOLOR n;
    D3DXColorNegative(&n, &c);
    CHECK(Near(n.r, 1 - c.r) && Near(n.a, 0.0f));
    D3DXCOLOR l;
    D3DXColorLerp(&l, P(D3DXCOLOR(0, 0, 0, 0)), P(D3DXCOLOR(1, 1, 1, 1)), 0.25f);
    CHECK(Near(l.r, 0.25f) && Near(l.a, 0.25f));
    D3DXColorScale(&l, P(D3DXCOLOR(1, 0.5f, 0.25f, 1)), 2.0f);
    CHECK(Near(l.g, 1.0f));
    D3DXColorModulate(&l, P(D3DXCOLOR(0.5f, 0.5f, 0.5f, 1)), P(D3DXCOLOR(0.5f, 1, 0, 1)));
    CHECK(Near(l.r, 0.25f) && Near(l.b, 0.0f));
    D3DXColorAdjustSaturation(&l, P(D3DXCOLOR(1, 0, 0, 1)), 0.0f);
    CHECK(Near(l.r, l.g) && Near(l.g, l.b));
    D3DXColorAdjustContrast(&l, P(D3DXCOLOR(0.5f, 0.5f, 0.5f, 1)), 3.0f);
    CHECK(Near(l.r, 0.5f));

    CHECK(Near(D3DXFresnelTerm(1.0f, 1.5f), 0.04f));        // ((n - 1) / (n + 1))^2 at normal incidence
    CHECK(D3DXFresnelTerm(0.1f, 1.5f) > 0.5f);               // reflectivity grows towards grazing angles
    CHECK(Near(D3DXToRadian(180.0f), D3DX_PI) && Near(D3DXToDegree(D3DX_PI), 180.0f));
}

static void TestMatrixStack()
{
    ID3DXMatrixStack *st = nullptr;
    CHECK(SUCCEEDED(D3DXCreateMatrixStack(0, &st)) && st);
    if (!st) return;
    CHECK(D3DXMatrixIsIdentity(st->GetTop()));
    // Right multiplication: the last operation is applied last (to a row vector).
    CHECK(SUCCEEDED(st->Translate(1, 0, 0)));
    CHECK(SUCCEEDED(st->Scale(2, 2, 2)));
    D3DXVECTOR3 r;
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(1, 0, 0)), st->GetTop());
    CHECK(NearV(r, 4, 0, 0));
    CHECK(SUCCEEDED(st->Push()));
    CHECK(SUCCEEDED(st->TranslateLocal(0, 5, 0))); // applied first, in the local frame
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 0)), st->GetTop());
    CHECK(NearV(r, 2, 10, 0));
    CHECK(SUCCEEDED(st->Pop()));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 0)), st->GetTop());
    CHECK(NearV(r, 2, 0, 0));
    CHECK(SUCCEEDED(st->LoadIdentity()) && D3DXMatrixIsIdentity(st->GetTop()));
    CHECK(SUCCEEDED(st->RotateAxis(P(D3DXVECTOR3(0, 1, 0)), D3DX_PI / 2)));
    D3DXVec3TransformCoord(&r, P(D3DXVECTOR3(0, 0, 1)), st->GetTop());
    CHECK(NearV(r, 1, 0, 0));
    D3DXMATRIX t;
    D3DXMatrixTranslation(&t, 0, 0, 3);
    CHECK(SUCCEEDED(st->LoadMatrix(&t)) && st->GetTop()->_43 == 3);
    CHECK(SUCCEEDED(st->Pop())); // the last matrix stays
    CHECK(st->GetTop() != nullptr);
    CHECK(FAILED(st->LoadMatrix(nullptr)));
    CHECK(st->Release() == 0);
}

int main()
{
    TestVectors();
    TestMatrices();
    TestQuaternions();
    TestPlanesAndColors();
    TestMatrixStack();
    printf("%d checks, %d failed\n", g_checks, g_fail);
    printf(g_fail ? "TEST_RESULT: FAIL\n" : "TEST_RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
