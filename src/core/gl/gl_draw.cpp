/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WebGL2 backend: render targets, clears and draws (pipeline state, uniforms, textures, vertex attributes).
 */
#include "core/gl/gl_backend.h"

namespace dxwebgl2 {

//------------------------------------------------------------------------------
// Enum mapping
//------------------------------------------------------------------------------
static GLenum ToGL(CompareFunc f)
{
    switch (f)
    {
    case CompareFunc::Never: return GL_NEVER;
    case CompareFunc::Less: return GL_LESS;
    case CompareFunc::Equal: return GL_EQUAL;
    case CompareFunc::LessEqual: return GL_LEQUAL;
    case CompareFunc::Greater: return GL_GREATER;
    case CompareFunc::NotEqual: return GL_NOTEQUAL;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

static GLenum ToGL(BlendFactor b)
{
    switch (b)
    {
    case BlendFactor::Zero: return GL_ZERO;
    case BlendFactor::One: return GL_ONE;
    case BlendFactor::SrcColor: return GL_SRC_COLOR;
    case BlendFactor::InvSrcColor: return GL_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return GL_DST_ALPHA;
    case BlendFactor::InvDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DstColor: return GL_DST_COLOR;
    case BlendFactor::InvDstColor: return GL_ONE_MINUS_DST_COLOR;
    default: return GL_SRC_ALPHA_SATURATE;
    }
}

static GLenum ToGL(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Subtract: return GL_FUNC_SUBTRACT;
    case BlendOp::RevSubtract: return GL_FUNC_REVERSE_SUBTRACT;
    case BlendOp::Min: return GL_MIN;
    case BlendOp::Max: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

static GLenum ToGL(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Zero: return GL_ZERO;
    case StencilOp::Replace: return GL_REPLACE;
    case StencilOp::IncrSat: return GL_INCR;
    case StencilOp::DecrSat: return GL_DECR;
    case StencilOp::Invert: return GL_INVERT;
    case StencilOp::IncrWrap: return GL_INCR_WRAP;
    case StencilOp::DecrWrap: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

static GLenum ToGL(Topology t)
{
    switch (t)
    {
    case Topology::PointList: return GL_POINTS;
    case Topology::LineList: return GL_LINES;
    case Topology::LineStrip: return GL_LINE_STRIP;
    case Topology::TriangleList: return GL_TRIANGLES;
    case Topology::TriangleStrip: return GL_TRIANGLE_STRIP;
    default: return GL_TRIANGLE_FAN;
    }
}

/// Debug aid: reports the first GL error after a stage of the draw setup.
void WebGL2Backend::DebugStageCheck(const char *stage, const DrawCall *c)
{
    GLenum e = glGetError();
    for (GLenum x = e; x != GL_NO_ERROR; x = glGetError()) Log("  (drain 0x%x)", x);
    if (e != GL_NO_ERROR)
    {
        const RenderTargetRef &rt = m_color;
        Log("GL error 0x%x after %s (draw #%u, fvf 0x%x, rt %ux%u)", e, stage, m_drawCounter, c ? (unsigned)c->debugTag : 0u,
            rt.width, rt.height);
    }
}

#define GL_STAGE_CHECK(stage, call) \
    do { if (GetConfig().debug) DebugStageCheck(stage, call); } while (0)

//------------------------------------------------------------------------------
// Render targets
//------------------------------------------------------------------------------
void WebGL2Backend::SetRenderTargets(const RenderTargetRef &color, const RenderTargetRef &depth)
{
    m_color = color;
    m_depth = depth;
    m_targetsDirty = true;
}

void WebGL2Backend::ApplyRenderTargets()
{
    if (!m_targetsDirty) return;
    if (m_boundFbo != m_fbo) { glBindFramebuffer(GL_FRAMEBUFFER, m_fbo); m_boundFbo = m_fbo; }

    // Colour attachment, unless the cached key says it is attached already.
    const RenderTargetRef &rt = m_color;
    int rtSamples = 0;
    switch (rt.kind)
    {
    case RenderTargetRef::Kind::TextureLevel:
    {
        rt.texture->EnsureGpu();
        GLTexture *t = static_cast<GLTexture *>(rt.texture->Gpu());
        const GLuint name = t ? t->name : 0;
        const uint64_t key = (1ull << 60) | ((uint64_t)name << 24) | ((uint64_t)rt.face << 16) | (uint64_t)rt.level;
        const GLenum faceTarget = rt.texture->Kind() == TextureKind::Cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + rt.face : GL_TEXTURE_2D;
        if (key != m_fboColorKey)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faceTarget, name, rt.level);
        m_fboColorKey = key;
        break;
    }
    case RenderTargetRef::Kind::Surface:
    {
        GLSurface *s = static_cast<GLSurface *>(rt.surface->Gpu());
        const GLuint rb = s ? s->renderbuffer : 0;
        const uint64_t key = (2ull << 60) | rb;
        if (key != m_fboColorKey) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
        m_fboColorKey = key;
        rtSamples = rt.surface->Desc().samples;
        break;
    }
    case RenderTargetRef::Kind::BackBuffer:
        if (m_samples)
        {
            const uint64_t key = (3ull << 60) | m_bbMsRb;
            if (key != m_fboColorKey) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_bbMsRb);
            m_fboColorKey = key;
        }
        else
        {
            const uint64_t key = (4ull << 60) | m_bbColor;
            if (key != m_fboColorKey) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bbColor, 0);
            m_fboColorKey = key;
        }
        rtSamples = m_samples;
        break;
    default:
        break;
    }

    // Depth attachment.
    const RenderTargetRef &ds = m_depth;
    const bool hasRT = rt.kind != RenderTargetRef::Kind::None;
    const bool hasDS = ds.kind == RenderTargetRef::Kind::Surface;
    uint64_t depthKey = m_fboDepthKey;
    const FormatInfo *di = hasDS ? GetFormatInfo(ds.format) : nullptr;
    const bool stencil = di && di->stencil;
    if (hasDS && hasRT && (ds.surface->Desc().samples != rtSamples || ds.width != rt.width || ds.height != rt.height))
    {
        // Direct3D 8 accepts a depth buffer that is larger than the render target (applications render
        // reflections into a small texture with the back buffer's depth buffer) and refuses one
        // whose multisampling differs (applications then disable their render-to-texture effects). WebGL
        // attaches neither (FRAMEBUFFER_INCOMPLETE_DIMENSIONS / _MULTISAMPLE), so the target gets a
        // private depth buffer of its own sample count and size; callers clear it before use.
        GLuint rb = 0;
        for (SubDepth &d : m_subDepth)
            if (d.w == rt.width && d.h == rt.height && d.samples == rtSamples && d.stencil == stencil) { rb = d.rb; break; }
        if (!rb)
        {
            glGenRenderbuffers(1, &rb);
            glBindRenderbuffer(GL_RENDERBUFFER, rb);
            const GLenum fmt = stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
            if (rtSamples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, rtSamples, fmt, rt.width, rt.height);
            else glRenderbufferStorage(GL_RENDERBUFFER, fmt, rt.width, rt.height);
            m_subDepth.push_back({rb, rt.width, rt.height, rtSamples, stencil});
            DXW_HIT(Approximated, "depth buffer substituted for a size/multisample mismatch (%ux%u, %d samples)", rt.width, rt.height, rtSamples);
        }
        depthKey = (7ull << 60) | rb;
        if (depthKey != m_fboDepthKey)
        {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
            if (stencil) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
            else glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        }
    }
    else if (hasDS)
    {
        // The renderbuffer is DEPTH24_STENCIL8 for stencil formats and a pure
        // depth format otherwise; attach to the matching point and detach the other.
        GLSurface *s = static_cast<GLSurface *>(ds.surface->Gpu());
        const GLuint rb = s ? s->renderbuffer : 0;
        depthKey = ((stencil ? 5ull : 6ull) << 60) | rb;
        if (depthKey != m_fboDepthKey)
        {
            if (stencil)
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
            else
            {
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
            }
        }
    }
    else
    {
        depthKey = 0;
        if (m_fboDepthKey != 0)
        {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        }
    }
    m_fboDepthKey = depthKey;
    if (GetConfig().debug)
    {
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) Log("framebuffer incomplete: 0x%x", st);
    }
    m_targetsDirty = false;
}

//------------------------------------------------------------------------------
// Clear
//------------------------------------------------------------------------------
void WebGL2Backend::Clear(const ClearCall &c)
{
    ApplyRenderTargets();
    if (m_samples && m_color.kind == RenderTargetRef::Kind::BackBuffer) m_bbDirty = true;

    // The masks a clear needs are set through the pipeline cache so that the next draw only re-issues
    // what really differs.
    const bool known = m_pipelineValid;
    GLbitfield bits = 0;
    if (c.color)
    {
        if (!known || m_applied.colorMask != 0xF) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        m_applied.colorMask = 0xF;
        if (c.rgba[0] != m_clearColor[0] || c.rgba[1] != m_clearColor[1] || c.rgba[2] != m_clearColor[2] ||
            c.rgba[3] != m_clearColor[3] || !m_clearColorValid)
        {
            glClearColor(c.rgba[0], c.rgba[1], c.rgba[2], c.rgba[3]);
            memcpy(m_clearColor, c.rgba, sizeof m_clearColor);
            m_clearColorValid = true;
        }
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if (c.depth)
    {
        if (!known || !m_applied.depthMask) glDepthMask(GL_TRUE);
        m_applied.depthMask = true;
        if (c.z != m_clearDepth || !m_clearDepthValid) { glClearDepthf(c.z); m_clearDepth = c.z; m_clearDepthValid = true; }
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    if (c.stencil)
    {
        if (!known || m_applied.stencilWriteMask != 0xFF) glStencilMask(0xFF);
        m_applied.stencilWriteMask = 0xFF;
        if ((GLint)c.stencilValue != m_clearStencil || !m_clearStencilValid)
        {
            glClearStencil((GLint)c.stencilValue);
            m_clearStencil = (GLint)c.stencilValue;
            m_clearStencilValid = true;
        }
        bits |= GL_STENCIL_BUFFER_BIT;
    }
    if (bits)
    {
        // The scissor test restricts a clear to the rectangles (the stencil test does not affect it). It
        // stays enabled afterwards; the next draw switches it off when needed (PipelineState::scissor).
        if (!known || !m_applied.scissor) glEnable(GL_SCISSOR_TEST);
        m_applied.scissor = true;
        for (uint32_t i = 0; i < c.rectCount; ++i)
        {
            const ClearRect &r = c.rects[i];
            const GLint sc[4] = {r.x, r.y, r.width, r.height};
            if (memcmp(sc, m_applied.sc, sizeof sc) != 0 || !known) { glScissor(r.x, r.y, r.width, r.height); memcpy(m_applied.sc, sc, sizeof sc); }
            glClear(bits);
        }
        // Anything that was not known (m_pipelineValid false) is applied in full by the next draw.
    }
}

//------------------------------------------------------------------------------
// Pipeline state
//------------------------------------------------------------------------------
void WebGL2Backend::ApplyPipeline(const PipelineState &want)
{
    PipelineState &have = m_applied;
    const bool all = !m_pipelineValid;
    auto enable = [](GLenum cap, bool on) { if (on) glEnable(cap); else glDisable(cap); };

    if (all || want.blend != have.blend) enable(GL_BLEND, want.blend);
    if (all || want.srcBlend != have.srcBlend || want.dstBlend != have.dstBlend) glBlendFunc(ToGL(want.srcBlend), ToGL(want.dstBlend));
    if (all || want.blendOp != have.blendOp) glBlendEquation(ToGL(want.blendOp));
    if (all || want.depthTest != have.depthTest) enable(GL_DEPTH_TEST, want.depthTest);
    if (all || want.depthFunc != have.depthFunc) glDepthFunc(ToGL(want.depthFunc));
    if (all || want.depthMask != have.depthMask) glDepthMask(want.depthMask ? GL_TRUE : GL_FALSE);
    if (all || want.depthNear != have.depthNear || want.depthFar != have.depthFar) glDepthRangef(want.depthNear, want.depthFar);
    if (all || want.cull != have.cull) enable(GL_CULL_FACE, want.cull);
    if (all || want.frontFace != have.frontFace) glFrontFace(want.frontFace == FrontFace::CW ? GL_CW : GL_CCW);
    if (all) glCullFace(GL_BACK);
    if (all || want.stencilTest != have.stencilTest) enable(GL_STENCIL_TEST, want.stencilTest);
    if (all || want.stencilFunc != have.stencilFunc || want.stencilRef != have.stencilRef || want.stencilMask != have.stencilMask)
        glStencilFunc(ToGL(want.stencilFunc), want.stencilRef, want.stencilMask);
    if (all || want.stencilFail != have.stencilFail || want.stencilZFail != have.stencilZFail || want.stencilPass != have.stencilPass)
        glStencilOp(ToGL(want.stencilFail), ToGL(want.stencilZFail), ToGL(want.stencilPass));
    if (all || want.stencilWriteMask != have.stencilWriteMask) glStencilMask(want.stencilWriteMask);
    if (all || want.colorMask != have.colorMask)
        glColorMask((want.colorMask & 1) != 0, (want.colorMask & 2) != 0, (want.colorMask & 4) != 0, (want.colorMask & 8) != 0);
    if (all || want.polyOffset != have.polyOffset) enable(GL_POLYGON_OFFSET_FILL, want.polyOffset);
    if (all || want.polyFactor != have.polyFactor || want.polyUnits != have.polyUnits) glPolygonOffset(want.polyFactor, want.polyUnits);
    if (all || memcmp(want.vp, have.vp, sizeof want.vp) != 0) glViewport(want.vp[0], want.vp[1], want.vp[2], want.vp[3]);
    if (all || want.scissor != have.scissor) enable(GL_SCISSOR_TEST, want.scissor);

    if (all || memcmp(&want, &have, sizeof want) != 0) ++g_d3d.pipelineChanges;
    have = want;
    m_pipelineValid = true;
}

//------------------------------------------------------------------------------
// Uniforms
//------------------------------------------------------------------------------
void WebGL2Backend::UploadUniforms(GLProgram *p, const uint32_t *versions, UniformSource &source)
{
    // Only the groups some uniform of this program belongs to, and only when a state of the group
    // changed since this program last received it.
    uint32_t pending = 0;
    for (int g = 0; g < G_COUNT; ++g)
        if ((p->usedGroups & (1u << g)) && p->ver[g] != versions[g]) pending |= 1u << g;
    if (!pending) return;

    m_uniformLoc = p->loc;
    for (int g = 0; g < G_COUNT; ++g)
    {
        if (!(pending & (1u << g))) continue;
        p->ver[g] = versions[g];
        ++g_d3d.uniformUploads;
        ++g_d3d.groupUploads[g];
        source.WriteUniforms((UniformGroup)g, *p, *this);
    }
}

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
void WebGL2Backend::BindTextures(const ProgramKey &key, const TextureBinding *bindings)
{
    for (int s = 0; s < key.stageCount; ++s)
    {
        const StageKey &st = key.stage[s];
        if (!st.texType) continue;
        const TextureBinding &b = bindings[s];
        GLTexture *t = static_cast<GLTexture *>(b.texture);
        GLenum target = st.texType == 1 ? GL_TEXTURE_2D : (st.texType == 2 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_3D);
        const int slot = st.texType - 1;
        GLuint id = t ? t->name : 0;
        if (!id) id = m_whiteTex[slot];
        if (m_activeUnit != s) { glActiveTexture(GL_TEXTURE0 + s); m_activeUnit = s; }
        if (m_boundTex[s][slot] != id) { glBindTexture(target, id); m_boundTex[s][slot] = id; ++g_d3d.textureBinds; }
        if (t && id == t->name)
        {
            const GLuint sampler = b.sampler ? static_cast<GLSampler *>(b.sampler)->name : 0;
            if (m_boundSampler[s] != sampler) { glBindSampler(s, sampler); m_boundSampler[s] = sampler; }
            if (t->baseLevel != b.baseLevel)
            {
                glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, b.baseLevel);
                t->baseLevel = b.baseLevel;
            }
        }
        else
        {
            if (m_boundSampler[s] != m_whiteSampler)
            {
                if (!m_whiteSampler)
                {
                    glGenSamplers(1, &m_whiteSampler);
                    glSamplerParameteri(m_whiteSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glSamplerParameteri(m_whiteSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
                glBindSampler(s, m_whiteSampler);
                m_boundSampler[s] = m_whiteSampler;
            }
        }
    }
}

//------------------------------------------------------------------------------
// Vertex attributes
//------------------------------------------------------------------------------
static inline GLuint NameOf(GpuBuffer *b) { return b ? static_cast<GLBuffer *>(b)->name : 0; }

/// Sets up the vertex attributes of the layout and streams of the draw and makes the vertex array object
/// that holds them current. Layouts, buffers and strides that were drawn with before are served from a
/// cache of vertex array objects (one GL call to bind instead of one per attribute); the cache is keyed
/// without the base vertex because indexed draws pass it to the draw call
/// (WEBGL_draw_instanced_base_vertex_base_instance) and streamed data is placed at multiples of the
/// vertex size. `pointerBase` is a vertex offset applied to the attribute pointers instead, for browsers
/// without that extension; such draws use the uncached scratch vertex array.
bool WebGL2Backend::BindAttributes(const DrawCall &c, UINT pointerBase, GLuint userBuffer)
{
    const VertexLayout *layout = c.layout;
    if (!layout->count) return false;
    const bool userData = c.userVertices != nullptr;

    GLuint bufs[MAX_STREAMS] = {};
    UINT strides[MAX_STREAMS] = {};
    VaoKey key;
    memset(&key, 0, sizeof key);
    key.layout = layout->id;
    key.pointerBase = pointerBase;
    int streamsUsed = 0;
    for (int i = 0; i < layout->count; ++i)
    {
        const int st = layout->elems[i].stream;
        if (bufs[st]) continue;
        if (userData && st == 0)
        {
            bufs[0] = userBuffer;
            strides[0] = c.userStride;
        }
        else
        {
            BufferStorage *vb = c.streams[st].buffer;
            if (!vb) return false;
            bufs[st] = NameOf(vb->Flush());
            strides[st] = c.streams[st].stride ? c.streams[st].stride : layout->stride[st];
        }
        if (streamsUsed < VAO_KEY_STREAMS)
        {
            key.buf[streamsUsed] = bufs[st];
            key.stride[streamsUsed] = (uint16_t)strides[st];
            key.stream[streamsUsed] = (uint8_t)st;
        }
        ++streamsUsed;
    }

    bool cacheable = streamsUsed <= VAO_KEY_STREAMS && !(userData && pointerBase != 0);
    VaoEntry *entry = nullptr;
    if (cacheable)
    {
        const uint64_t h = HashBytes(&key, sizeof key);
        auto range = m_vaoCache.equal_range(h);
        for (auto it = range.first; it != range.second; ++it)
            if (memcmp(&it->second->key, &key, sizeof key) == 0) { entry = it->second; break; }
        if (!entry && pointerBase != 0)
        {
            // A base vertex in the key (browsers without the base vertex extension): only geometry that
            // keeps coming back is worth a vertex array object; dynamic buffers move their data around.
            uint32_t &seen = m_vaoSeen[h];
            if (seen == 0) { seen = 1; cacheable = false; }
        }
        if (cacheable && !entry)
        {
            if (m_vaoCache.size() >= VAO_CACHE_LIMIT) EvictVao();
            entry = new VaoEntry();
            entry->key = key;
            glGenVertexArrays(1, &entry->vao);
            m_vaoCache.insert({h, entry});
            glBindVertexArray(entry->vao);
            m_curVao = entry;
            SetAttributePointers(layout, bufs, strides, pointerBase, 0, &entry->enabled);
            ++g_d3d.vaoCreated;
        }
        if (entry)
        {
            entry->lastUse = ++m_vaoClock;
            if (m_curVao != entry)
            {
                glBindVertexArray(entry->vao);
                m_curVao = entry;
            }
            return true;
        }
    }

    // Uncached: the scratch vertex array, reconfigured when anything it depends on changed.
    uint64_t sig = layout->id * 1000003ull + pointerBase;
    for (int i = 0; i < MAX_STREAMS; ++i)
        if (bufs[i]) { sig = sig * 31 + bufs[i]; sig = sig * 31 + strides[i]; }
    if (m_curVao)
    {
        glBindVertexArray(m_vao); // keeps the state it was last configured with
        m_curVao = nullptr;
    }
    if (sig == m_attribSig) return true;
    m_attribSig = sig;
    SetAttributePointers(layout, bufs, strides, pointerBase, m_enabledAttribs, &m_enabledAttribs);
    return true;
}

/// Points the attributes of `layout` at the buffers and enables exactly those arrays (the vertex array
/// object that is bound keeps the state). `oldEnabled` is the mask of arrays currently enabled in it.
void WebGL2Backend::SetAttributePointers(const VertexLayout *layout, const GLuint *bufs, const UINT *strides, UINT pointerBase,
                                         uint32_t oldEnabled, uint32_t *newEnabled)
{
    uint32_t enabled = 0;
    for (int i = 0; i < layout->count; ++i)
    {
        const VertexElement &e = layout->elems[i];
        const UINT stride = strides[e.stream];
        BindBuffer(GL_ARRAY_BUFFER, bufs[e.stream]);
        const size_t off = (size_t)e.offset + (size_t)pointerBase * stride;
        GLenum type = e.glType == 0 ? GL_FLOAT : (e.glType == 1 ? GL_UNSIGNED_BYTE : GL_SHORT);
        glVertexAttribPointer(e.reg, e.size, type, e.normalized ? GL_TRUE : GL_FALSE, stride, reinterpret_cast<const void *>(off));
        enabled |= 1u << e.reg;
    }
    const uint32_t changed = enabled ^ oldEnabled;
    if (changed)
        for (int i = 0; i < 16; ++i)
            if (changed & (1u << i))
            {
                if (enabled & (1u << i)) glEnableVertexAttribArray(i);
                else glDisableVertexAttribArray(i);
            }
    *newEnabled = enabled;
}

/// Deletes the least recently used vertex array object (and the ones nothing can use any more).
void WebGL2Backend::EvictVao()
{
    uint64_t oldest = ~0ull;
    VaoEntry *victim = nullptr;
    uint64_t victimHash = 0;
    for (auto &kv : m_vaoCache)
        if (kv.second != m_curVao && kv.second->lastUse < oldest) { oldest = kv.second->lastUse; victim = kv.second; victimHash = kv.first; }
    if (!victim) return;
    DeleteVao(victimHash, victim);
}

void WebGL2Backend::DeleteVao(uint64_t hash, VaoEntry *e)
{
    auto range = m_vaoCache.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second == e) { m_vaoCache.erase(it); break; }
    if (m_curVao == e) { glBindVertexArray(m_vao); m_curVao = nullptr; m_attribSig = 0; }
    if (e->vao) glDeleteVertexArrays(1, &e->vao);
    delete e;
}

/// Drops every cached vertex array object (`contextAlive` false: their names died with the context).
void WebGL2Backend::ClearVaoCache(bool contextAlive)
{
    for (auto &kv : m_vaoCache)
    {
        if (contextAlive && kv.second->vao) glDeleteVertexArrays(1, &kv.second->vao);
        delete kv.second;
    }
    m_vaoCache.clear();
    m_vaoSeen.clear();
    m_curVao = nullptr;
    m_attribSig = 0;
}

/// Appends data to the streaming buffer used for draws from user memory. Offsets are multiples of
/// `align` (the vertex size for vertex data, so that the offset is a whole number of vertices and can
/// be passed as a base vertex). The buffer is orphaned when it is full.
size_t WebGL2Backend::UploadStream(const void *data, size_t size, bool index, size_t align)
{
    GLuint &buf = index ? m_streamIB : m_streamVB;
    size_t &cap = index ? m_streamIBSize : m_streamVBSize;
    size_t &pos = index ? m_streamIBPos : m_streamVBPos;
    const GLenum target = index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    if (!buf) glGenBuffers(1, &buf);
    BindBuffer(target, buf);
    if (align < 4) align = 4;
    size = (size + 3) & ~size_t(3);
    size_t at = (pos + align - 1) / align * align;
    if (size > cap || at + size > cap)
    {
        if (size + align > cap) cap = Max<size_t>((size + align) * 2, 4u << 20);
        glBufferData(target, (GLsizeiptr)cap, nullptr, GL_STREAM_DRAW); // orphan
        at = 0;
    }
    glBufferSubData(target, (GLintptr)at, (GLsizeiptr)size, data);
    g_gl.uploadBytes += (uint32_t)size;
    pos = at + size;
    return at;
}

//------------------------------------------------------------------------------
// Draw
//------------------------------------------------------------------------------
bool WebGL2Backend::Draw(const DrawCall &c)
{
    ++m_drawCounter;
    GLProgram *prog = static_cast<GLProgram *>(c.program);
    GL_STAGE_CHECK("select program", &c);
    ApplyRenderTargets();
    GL_STAGE_CHECK("render targets", &c);
    if (m_color.kind == RenderTargetRef::Kind::BackBuffer) m_bbDirty = true;
    ApplyPipeline(*c.pipeline);
    GL_STAGE_CHECK("apply pipeline", &c);
    if (m_boundProgram != prog->id)
    {
        glUseProgram(prog->id);
        m_boundProgram = prog->id;
        ++g_d3d.programSwitches;
    }
    GL_STAGE_CHECK("pipeline/program", &c);
    UploadUniforms(prog, c.uniformVersions, *c.uniforms);
    GL_STAGE_CHECK("uniforms", &c);
    BindTextures(prog->key, c.textures);
    GL_STAGE_CHECK("textures", &c);
    if (GetConfig().debugForce)
    {
        if (GetConfig().debugForce & 1) glDisable(GL_CULL_FACE);
        if (GetConfig().debugForce & 2) glDisable(GL_BLEND);
        if (GetConfig().debugForce & 4) glDisable(GL_DEPTH_TEST);
        m_applied.Invalidate();
    }

    const GLenum mode = ToGL(c.topology);
    const bool indexed = c.indexed;
    const bool userData = c.userVertices != nullptr;
    const bool triangles = mode == GL_TRIANGLES || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN;
    const bool wire = triangles && c.fill != FillMode::Solid;
    // Flat shading takes the color of the triangle's first vertex in Direct3D and of the last one in
    // WebGL unless WEBGL_provoking_vertex switches the convention; otherwise the triangles are
    // reordered so that the first vertex comes last.
    const bool flatFallback = triangles && !wire && prog->key.flatShade && !m_caps.provokingVertex && mode != GL_TRIANGLE_FAN;
    const bool rebuild = wire || flatFallback;

    // Vertex data. User data is appended to the streaming buffer at a multiple of the vertex size, so
    // that its position is a vertex number ("vertexBase") like the base vertex of indexed draws.
    UINT vertexBase = 0;
    if (userData)
    {
        const size_t bytes = (size_t)c.userVertexCount * c.userStride;
        vertexBase = (UINT)(UploadStream(c.userVertices, bytes, false, c.userStride) / c.userStride);
    }
    else
        for (int i = 0; i < MAX_STREAMS; ++i)
            if (c.streams[i].buffer) c.streams[i].buffer->Flush();
    // Where the base vertex is applied: by the draw call (extension), or by the attribute pointers.
    const UINT totalBase = userData ? vertexBase : (indexed ? c.baseVertex : 0);
    const bool baseInDraw = indexed && m_caps.baseVertex && !rebuild;
    if (!BindAttributes(c, baseInDraw || !indexed || rebuild ? 0 : totalBase, m_streamVB)) return false;
    GL_STAGE_CHECK("attributes", &c);

    // Index data.
    GLenum indexType = GL_UNSIGNED_SHORT;
    size_t indexOffset = 0;
    GLuint ibuf = 0;
    const void *wireSrc = nullptr;
    if (indexed)
    {
        if (userData)
        {
            indexType = c.index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            const size_t bytes = (size_t)c.count * (c.index32 ? 4 : 2);
            indexOffset = UploadStream(c.userIndices, bytes, true, 4);
            GL_STAGE_CHECK("index upload", &c);
            ibuf = m_streamIB;
            wireSrc = c.userIndices;
        }
        else
        {
            BufferStorage *ib = c.indexBuffer;
            if (!ib) return false;
            indexType = c.index32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            ibuf = NameOf(ib->Flush());
            GL_STAGE_CHECK("index buffer flush", &c);
            indexOffset = (size_t)c.start * (c.index32 ? 4 : 2);
            wireSrc = ib->Data() + indexOffset;
        }
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf);
        GL_STAGE_CHECK("element buffer bind", &c);
    }

    if (rebuild)
    {
        // GL ES has no polygon mode: rebuild the primitive as lines/points (or rotate the triangles).
        // The indices of the rebuilt list include the base vertex, so the attribute pointers stay put.
        const IndexFetch fetch = {indexed ? wireSrc : nullptr, indexType == GL_UNSIGNED_INT, totalBase, userData ? vertexBase : c.start};
        ExpandTriangles(c.topology, c.primitiveCount, fetch, flatFallback, m_triScratch);
        const std::vector<uint32_t> *out = &m_triScratch;
        GLenum outMode = GL_TRIANGLES;
        if (wire && c.fill == FillMode::Wireframe)
        {
            outMode = GL_LINES;
            TrianglesToLines(m_triScratch, m_lineScratch);
            out = &m_lineScratch;
        }
        else if (wire)
            outMode = GL_POINTS;
        size_t off = UploadStream(out->data(), out->size() * 4, true, 4);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_streamIB);
        glDrawElements(outMode, (GLsizei)out->size(), GL_UNSIGNED_INT, reinterpret_cast<const void *>(off));
        if (g_diagOn)
        {
            if (wire) DXW_HIT(Approximated, "fill mode %s rebuilt from the triangle list on the CPU", c.fill == FillMode::Wireframe ? "WIREFRAME" : "POINT");
            else DXW_HIT(Approximated, "flat shading by reordering triangles (no WEBGL_provoking_vertex)");
        }
        return true;
    }
    if (g_diagOn && triangles && prog->key.flatShade && mode == GL_TRIANGLE_FAN)
        DXW_HIT(Approximated, "flat shaded triangle fan uses the wrong provoking vertex");

    if (indexed)
    {
        if (baseInDraw && totalBase)
            glDrawElementsInstancedBaseVertexBaseInstanceWEBGL(mode, c.count, indexType, reinterpret_cast<const void *>(indexOffset), 1, (GLint)totalBase, 0);
        else
            glDrawElements(mode, c.count, indexType, reinterpret_cast<const void *>(indexOffset));
        GL_STAGE_CHECK("glDrawElements", &c);
    }
    else
        glDrawArrays(mode, userData ? vertexBase : c.start, c.count);

    if (GetConfig().debug)
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) Log("GL error 0x%x after draw #%u (fvf 0x%x, indexed %d)", err, m_drawCounter, (unsigned)c.debugTag, (int)indexed);
    }
    return true;
}

} // namespace dxwebgl2
