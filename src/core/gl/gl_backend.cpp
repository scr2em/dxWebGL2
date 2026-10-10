/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WebGL2 backend: context creation and capabilities, back buffer, presentation and context loss.
 */
#include "core/gl/gl_backend.h"

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

// Canvas event listeners for context loss. The default action of webglcontextlost has to be
// prevented or the browser never restores the context. The flag is a plain int in the backend
// object: 1 = lost, 2 = restored (the listeners run on the thread that owns the context).
EM_JS(void, dxwebgl2_install_context_listeners, (int ctx, int flagPtr), {
    var c = GL.contexts[ctx];
    if (!c || !c.GLctx) return;
    var gl = c.GLctx;
    var canvas = gl.canvas;
    if (!canvas || !canvas.addEventListener) return;
    Module['dxwebgl2LoseExt'] = gl.getExtension('WEBGL_lose_context');
    canvas.addEventListener('webglcontextlost', function(e) {
        e.preventDefault();
        Atomics.store(HEAP32, flagPtr >> 2, 1);
    }, false);
    canvas.addEventListener('webglcontextrestored', function(e) {
        Atomics.store(HEAP32, flagPtr >> 2, 2);
    }, false);
});

// WEBGL_lose_context.loseContext(): the test aid behind dxwebgl2_LoseContextNow().
EM_JS(int, dxwebgl2_lose_context, (), {
    var ext = Module['dxwebgl2LoseExt'];
    if (!ext) return 0;
    ext.loseContext();
    return 1;
});

EM_JS(int, dxwebgl2_restore_context, (), {
    var ext = Module['dxwebgl2LoseExt'];
    if (!ext) return 0;
    try { ext.restoreContext(); } catch (e) { return 0; }
    return 1;
});

// D3D flat shading takes the colour of a triangle's first vertex, WebGL's default is the last.
EM_JS(int, dxwebgl2_provoking_first, (int ctx), {
    var c = GL.contexts[ctx];
    if (!c || !c.GLctx) return 0;
    var ext = c.GLctx.getExtension('WEBGL_provoking_vertex');
    if (!ext) return 0;
    ext.provokingVertexWEBGL(0x8E4D /* FIRST_VERTEX_CONVENTION_WEBGL */);
    return 1;
});

namespace dxwebgl2 {

#ifndef GL_UNMASKED_RENDERER_WEBGL
#define GL_UNMASKED_RENDERER_WEBGL 0x9246
#define GL_UNMASKED_VENDOR_WEBGL 0x9245
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

Backend *CreateWebGL2Backend() { return new WebGL2Backend(); }

//------------------------------------------------------------------------------
// Context
//------------------------------------------------------------------------------
bool WebGL2Backend::Initialize(uint32_t width, uint32_t height)
{
    if (!CreateContext(width, height)) return false;
    QueryCaps();
    ResetCaches();
    CreateBaseObjects();
    CreatePresentProgram();
    return true;
}

bool WebGL2Backend::CreateContext(uint32_t width, uint32_t height)
{
    const Config &cfg = GetConfig();
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.alpha = false;
    attr.depth = false;
    attr.stencil = false;
    attr.antialias = false;
    attr.premultipliedAlpha = false;
    attr.preserveDrawingBuffer = false;
    attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    attr.failIfMajorPerformanceCaveat = false;
    attr.majorVersion = 2;
    attr.minorVersion = 0;
    attr.enableExtensionsByDefault = true;
    attr.explicitSwapControl = cfg.presentMode == DXWEBGL2_PRESENT_EXPLICIT;
    if (cfg.contextProxy) attr.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_ALWAYS;

    // The drawing buffer has the size of the back buffer.
    if (width && height)
        emscripten_set_canvas_element_size(cfg.canvas.c_str(), (int)width, (int)height);

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(cfg.canvas.c_str(), &attr);
    if (ctx <= 0 && attr.explicitSwapControl)
    {
        // Explicit swap control needs OffscreenCanvas support in the build and the browser.
        Log("explicit swap control is not available (error %d), using implicit presentation", (int)ctx);
        attr.explicitSwapControl = false;
        ctx = emscripten_webgl_create_context(cfg.canvas.c_str(), &attr);
    }
    m_explicitSwap = attr.explicitSwapControl;
    if (ctx <= 0)
    {
        Log("could not create a WebGL2 context on '%s' (error %d)", cfg.canvas.c_str(), (int)ctx);
        DXW_HIT(Failed, "WebGL2 context creation failed (error %d)", (int)ctx);
        return false;
    }
    if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS)
    {
        Log("could not make the WebGL2 context current");
        emscripten_webgl_destroy_context(ctx);
        return false;
    }
    m_glContext = (int)ctx;
    InstallContextListeners();
    return true;
}

void WebGL2Backend::InstallContextListeners()
{
    m_ctxEvent = 0;
    dxwebgl2_install_context_listeners(m_glContext, (int)(intptr_t)&m_ctxEvent);
}

/// Extensions have to be requested again on a restored context.
void WebGL2Backend::EnableExtensions()
{
    emscripten_webgl_enable_extension(m_glContext, "WEBGL_compressed_texture_s3tc");
    emscripten_webgl_enable_extension(m_glContext, "EXT_texture_filter_anisotropic");
    emscripten_webgl_enable_extension(m_glContext, "WEBGL_debug_renderer_info");
    const int off = GetConfig().featureOverrides;
    m_caps.provokingVertex = !(off & 1) && dxwebgl2_provoking_first(m_glContext) != 0;
    m_caps.baseVertex = !(off & 2) && emscripten_webgl_enable_WEBGL_draw_instanced_base_vertex_base_instance(m_glContext);
}

void WebGL2Backend::QueryCaps()
{
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_caps.maxTextureSize);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &m_caps.maxCubeSize);
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &m_caps.max3DSize);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &m_caps.maxVertexAttribs);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &m_caps.maxVertexUniformVectors);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &m_caps.maxTextureUnits);
    glGetIntegerv(GL_MAX_SAMPLES, &m_caps.maxSamples);
    if (GetConfig().featureOverrides & 8) m_caps.maxSamples = 1;
    GLfloat range[2] = {1, 1};
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, range);
    m_caps.maxPointSize = range[1];

    // Extensions are queried through the Emscripten helper (works on workers too).
    EnableExtensions();
    m_caps.s3tc = emscripten_webgl_enable_extension(m_glContext, "WEBGL_compressed_texture_s3tc") != 0;
    if (GetConfig().disableS3TC) m_caps.s3tc = false;
    m_caps.anisotropic = emscripten_webgl_enable_extension(m_glContext, "EXT_texture_filter_anisotropic") != 0;
    if (m_caps.anisotropic)
    {
        GLfloat a = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &a);
        m_caps.maxAnisotropy = a;
    }
    const char *v = (const char *)glGetString(GL_VERSION);
    m_caps.version = v ? v : "";
    const char *r = (const char *)glGetString(GL_RENDERER);
    const char *ven = (const char *)glGetString(GL_VENDOR);
    if (emscripten_webgl_enable_extension(m_glContext, "WEBGL_debug_renderer_info"))
    {
        const char *ur = (const char *)glGetString(GL_UNMASKED_RENDERER_WEBGL);
        const char *uv = (const char *)glGetString(GL_UNMASKED_VENDOR_WEBGL);
        if (ur && *ur) r = ur;
        if (uv && *uv) ven = uv;
    }
    m_caps.renderer = r ? r : "WebGL2";
    m_caps.vendor = ven ? ven : "WebGL";
    DiagSetGpu(m_caps.vendor + " / " + m_caps.renderer + " (" + m_caps.version + "), S3TC " + (m_caps.s3tc ? "yes" : "no") +
               ", max texture " + std::to_string(m_caps.maxTextureSize) + ", MSAA up to " + std::to_string(m_caps.maxSamples) +
               "x, provoking vertex ext " + (m_caps.provokingVertex ? "yes" : "no") + ", base vertex ext " + (m_caps.baseVertex ? "yes" : "no"));
    Log("WebGL2 renderer: %s / %s (%s), S3TC %s, max texture %d, anisotropy %.0f, max samples %d, provoking vertex %s, base vertex %s", m_caps.vendor.c_str(),
        m_caps.renderer.c_str(), m_caps.version.c_str(), m_caps.s3tc ? "yes" : "no (decoded on the CPU)",
        m_caps.maxTextureSize, m_caps.maxAnisotropy, m_caps.maxSamples, m_caps.provokingVertex ? "yes" : "no",
        m_caps.baseVertex ? "yes" : "no");
}

/// The objects every draw depends on; recreated after the context was lost.
void WebGL2Backend::CreateBaseObjects()
{
    glGenFramebuffers(1, &m_fbo);
    glGenFramebuffers(1, &m_scratchFbo);
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // Neutral defaults for all attributes (0,0,0,1) so that unbound inputs read what D3D expects.
    for (int i = 0; i < 16 && i < m_caps.maxVertexAttribs; ++i) glVertexAttrib4f(i, 0, 0, 0, 1);

    // 1x1 white textures bound for stages without a texture.
    const uint8_t white[4] = {255, 255, 255, 255};
    const uint8_t white6[6][4] = {{255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
                                  {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}};
    glGenTextures(3, m_whiteTex);
    glActiveTexture(GL_TEXTURE0 + GL_UPLOAD_UNIT);
    glBindTexture(GL_TEXTURE_2D, m_whiteTex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_whiteTex[1]);
    for (int f = 0; f < 6; ++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white6[f]);
    glBindTexture(GL_TEXTURE_3D, m_whiteTex[2]);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    m_activeUnit = GL_UPLOAD_UNIT;
    for (int i = 0; i < 3; ++i) m_boundTex[GL_UPLOAD_UNIT][i] = m_whiteTex[i];
}

/// Clears every cached piece of GL state knowledge (fresh or restored context).
void WebGL2Backend::ResetCaches()
{
    memset(m_boundTex, 0, sizeof m_boundTex);
    memset(m_boundSampler, 0, sizeof m_boundSampler);
    m_activeUnit = -1;
    m_applied.Invalidate();
    m_pipelineValid = false;
    m_boundProgram = 0xFFFFFFFF;
    m_boundArrayBuffer = 0xFFFFFFFF;
    m_boundElementBuffer = 0xFFFFFFFF;
    m_enabledAttribs = 0;
    m_attribSig = 0;
    m_curVao = nullptr;
    m_boundFbo = 0xFFFFFFFF;
    m_fboColorKey = m_fboDepthKey = 0;
    m_targetsDirty = true;
    m_whiteSampler = 0;
    m_presentGammaValue = -1.0f;
    m_clearColorValid = m_clearDepthValid = m_clearStencilValid = false;
}

void WebGL2Backend::InvalidateState()
{
    m_pipelineValid = false;
    m_targetsDirty = true;
    m_attribSig = 0;
}

void WebGL2Backend::DestroyBaseObjects(bool contextAlive)
{
    ClearVaoCache(contextAlive);
    if (contextAlive)
    {
        for (auto &s : m_samplers) glDeleteSamplers(1, &s.second->name);
        if (m_whiteSampler) glDeleteSamplers(1, &m_whiteSampler);
        glDeleteFramebuffers(1, &m_fbo);
        glDeleteFramebuffers(1, &m_scratchFbo);
        glDeleteVertexArrays(1, &m_vao);
        glDeleteTextures(3, m_whiteTex);
        if (m_streamVB) glDeleteBuffers(1, &m_streamVB);
        if (m_streamIB) glDeleteBuffers(1, &m_streamIB);
        if (m_presentProgram) glDeleteProgram(m_presentProgram);
        if (m_presentVao) glDeleteVertexArrays(1, &m_presentVao);
        if (m_gammaTex) glDeleteTextures(1, &m_gammaTex);
        for (SubDepth &d : m_subDepth) glDeleteRenderbuffers(1, &d.rb);
        if (m_resolveRead) glDeleteFramebuffers(1, &m_resolveRead);
        if (m_resolveDraw) glDeleteFramebuffers(1, &m_resolveDraw);
    }
    for (auto &s : m_samplers) delete s.second;
    m_samplers.clear();
    m_whiteSampler = 0;
    m_fbo = m_scratchFbo = m_vao = 0;
    memset(m_whiteTex, 0, sizeof m_whiteTex);
    m_streamVB = m_streamIB = 0;
    m_streamVBSize = m_streamIBSize = m_streamVBPos = m_streamIBPos = 0;
    m_presentProgram = 0;
    m_presentVao = 0;
    m_gammaTex = 0;
    m_subDepth.clear();
    m_resolveRead = m_resolveDraw = 0;
}

void WebGL2Backend::Shutdown()
{
    if (!m_glContext) return;
    DestroyBaseObjects(true);
    emscripten_webgl_make_context_current(0);
    emscripten_webgl_destroy_context(m_glContext);
    m_glContext = 0;
}

void WebGL2Backend::CanvasSize(int &width, int &height)
{
    width = height = 0;
    emscripten_get_canvas_element_size(GetConfig().canvas.c_str(), &width, &height);
}

//------------------------------------------------------------------------------
// Back buffer
//------------------------------------------------------------------------------
bool WebGL2Backend::CreateBackBuffer(uint32_t w, uint32_t h, int samples)
{
    m_bbWidth = w;
    m_bbHeight = h;
    m_samples = samples;
    emscripten_set_canvas_element_size(GetConfig().canvas.c_str(), (int)w, (int)h);

    glGenTextures(1, &m_bbColor);
    BindForUpload(GL_TEXTURE_2D, m_bbColor);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    // Sampled by the present pass without a sampler object.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (m_samples)
    {
        glGenRenderbuffers(1, &m_bbMsRb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_bbMsRb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_RGBA8, w, h);
    }
    m_bbDirty = false;
    m_targetsDirty = true;
    return true;
}

void WebGL2Backend::DestroyBackBuffer()
{
    if (m_bbColor)
    {
        ForgetTexture(m_bbColor);
        glDeleteTextures(1, &m_bbColor);
        m_bbColor = 0;
    }
    if (m_bbMsRb)
    {
        glDeleteRenderbuffers(1, &m_bbMsRb);
        m_bbMsRb = 0;
    }
    m_samples = 0;
    m_targetsDirty = true;
}

/// Copies the multisampled back buffer into the texture that Present() and the read-backs use.
void WebGL2Backend::ResolveBackBuffer()
{
    if (!m_samples || !m_bbDirty || !m_bbMsRb || m_lost) return;
    if (!m_resolveRead) glGenFramebuffers(1, &m_resolveRead);
    if (!m_resolveDraw) glGenFramebuffers(1, &m_resolveDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_resolveRead);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_bbMsRb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveDraw);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bbColor, 0);
    const GLint w = (GLint)m_bbWidth, h = (GLint)m_bbHeight;
    // The blit is affected by the scissor test and the colour mask.
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_applied.colorMask = 0xF;
    m_applied.scissor = false;
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    m_boundFbo = 0xFFFFFFFF; // the draw binding changed
    m_targetsDirty = true;
    m_bbDirty = false;
}

bool WebGL2Backend::ReadBackBuffer(const RECT &rc, uint8_t *rgbaOut)
{
    return ReadTextureLevel(m_bbColor, GL_TEXTURE_2D, GL_TEXTURE_2D, 0, rc, rgbaOut);
}

//------------------------------------------------------------------------------
// Present
//------------------------------------------------------------------------------
void WebGL2Backend::CreatePresentProgram()
{
    static const char *vs =
        "#version 300 es\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "    uv = vec2(p.x, 1.0 - p.y);\n"
        "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    static const char *fs =
        "#version 300 es\nprecision highp float;\n"
        "in vec2 uv;\nuniform sampler2D s;\nuniform sampler2D g;\nuniform float doGamma;\n"
        "out vec4 o;\n"
        "void main() {\n"
        "    vec3 c = texture(s, uv).rgb;\n"
        "    if (doGamma > 0.5) {\n"
        "        c = vec3(texture(g, vec2(c.r * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).r,\n"
        "                 texture(g, vec2(c.g * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).g,\n"
        "                 texture(g, vec2(c.b * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).b);\n"
        "    }\n"
        "    o = vec4(c, 1.0);\n"
        "}\n";
    ProgramKey key = {};
    GLProgram *p = CompileProgram(key, vs, fs);
    m_presentProgram = p->id;
    delete p; // keep the GL program, drop the wrapper
    glUseProgram(m_presentProgram);
    glUniform1i(glGetUniformLocation(m_presentProgram, "s"), 0);
    glUniform1i(glGetUniformLocation(m_presentProgram, "g"), 1);
    m_presentGammaLoc = glGetUniformLocation(m_presentProgram, "doGamma");
    m_boundProgram = 0xFFFFFFFF;
    glGenVertexArrays(1, &m_presentVao);
}

void WebGL2Backend::SetGammaRamp(const uint16_t *red, const uint16_t *green, const uint16_t *blue, bool active)
{
    for (int i = 0; i < 256; ++i)
    {
        m_gammaLut[i * 4 + 0] = (uint8_t)(red[i] >> 8);
        m_gammaLut[i * 4 + 1] = (uint8_t)(green[i] >> 8);
        m_gammaLut[i * 4 + 2] = (uint8_t)(blue[i] >> 8);
        m_gammaLut[i * 4 + 3] = 255;
    }
    m_gammaActive = active;
    m_gammaDirty = true;
}

void WebGL2Backend::PresentToCanvas()
{
    ResolveBackBuffer();
    // Gamma LUT texture.
    if (m_gammaActive && m_gammaDirty)
    {
        if (!m_gammaTex) glGenTextures(1, &m_gammaTex);
        BindForUpload(GL_TEXTURE_2D, m_gammaTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_gammaLut);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gammaDirty = false;
    }

    // State the present pass needs, issued only where the cached pipeline state differs.
    if (m_boundFbo != 0) { glBindFramebuffer(GL_FRAMEBUFFER, 0); m_boundFbo = 0; }
    m_targetsDirty = true;
    const bool known = m_pipelineValid;
    auto off = [&](GLenum cap, bool &state) { if (!known || state) glDisable(cap); state = false; };
    off(GL_BLEND, m_applied.blend);
    off(GL_DEPTH_TEST, m_applied.depthTest);
    off(GL_CULL_FACE, m_applied.cull);
    off(GL_STENCIL_TEST, m_applied.stencilTest);
    off(GL_SCISSOR_TEST, m_applied.scissor);
    off(GL_POLYGON_OFFSET_FILL, m_applied.polyOffset);
    if (!known || m_applied.colorMask != 0xF) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_applied.colorMask = 0xF;
    if (!known || !m_applied.depthMask) glDepthMask(GL_TRUE);
    m_applied.depthMask = true;
    const GLint vp[4] = {0, 0, (GLint)m_bbWidth, (GLint)m_bbHeight};
    if (!known || memcmp(m_applied.vp, vp, sizeof vp) != 0) glViewport(vp[0], vp[1], vp[2], vp[3]);
    memcpy(m_applied.vp, vp, sizeof vp);

    glBindVertexArray(m_presentVao);
    if (m_boundProgram != m_presentProgram) { glUseProgram(m_presentProgram); m_boundProgram = m_presentProgram; }
    const float gamma = m_gammaActive ? 1.0f : 0.0f;
    if (gamma != m_presentGammaValue) { glUniform1f(m_presentGammaLoc, gamma); m_presentGammaValue = gamma; }
    if (m_activeUnit != 0) { glActiveTexture(GL_TEXTURE0); m_activeUnit = 0; }
    if (m_boundTex[0][0] != m_bbColor) { glBindTexture(GL_TEXTURE_2D, m_bbColor); m_boundTex[0][0] = m_bbColor; }
    if (m_boundSampler[0] != 0) { glBindSampler(0, 0); m_boundSampler[0] = 0; }
    if (m_gammaActive)
    {
        glActiveTexture(GL_TEXTURE1);
        m_activeUnit = 1;
        if (m_boundTex[1][0] != m_gammaTex) { glBindTexture(GL_TEXTURE_2D, m_gammaTex); m_boundTex[1][0] = m_gammaTex; }
        if (m_boundSampler[1] != 0) { glBindSampler(1, 0); m_boundSampler[1] = 0; }
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Back to the vertex array of the draw path; its program binding and attribute state are unchanged.
    glBindVertexArray(m_vao);
    m_curVao = nullptr;
}

void WebGL2Backend::Present()
{
    PresentToCanvas();
    if (m_vaoSeen.size() > 16384) m_vaoSeen.clear();
    if (m_explicitSwap)
        emscripten_webgl_commit_frame();
}

void WebGL2Backend::DebugDescribeBackBuffer()
{
    ResolveBackBuffer();
    // Sample the finished frame: a few pixels of the back buffer, as RGBA.
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bbColor, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        const UINT w = m_bbWidth, h = m_bbHeight;
        std::vector<uint8_t> px((size_t)w * h * 4);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        size_t lit = 0;
        UINT minX = w, minY = h, maxX = 0, maxY = 0;
        const uint8_t *first = nullptr;
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x)
            {
                const uint8_t *p = &px[((size_t)y * w + x) * 4];
                if (p[0] | p[1] | p[2])
                {
                    if (!first) first = p;
                    ++lit;
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        if (lit)
            Log("back buffer %ux%u: %u non-black pixels in (%u,%u)-(%u,%u), first is %u,%u,%u,%u", w, h, (unsigned)lit, minX, minY, maxX, maxY, first[0], first[1], first[2], first[3]);
        else
            Log("back buffer %ux%u: all black", w, h);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    m_boundFbo = 0;
    m_targetsDirty = true;
}

//------------------------------------------------------------------------------
// Context loss
//------------------------------------------------------------------------------
bool WebGL2Backend::IsContextLost()
{
    return emscripten_is_webgl_context_lost(m_glContext);
}

void WebGL2Backend::OnContextLost()
{
    // Every GL name of the old context is dead: nothing created before this point may be deleted any more.
    ++m_generation;
    m_lost = true;
    Backend::OnContextLost();
}

bool WebGL2Backend::RestoreContext()
{
    if (emscripten_webgl_make_context_current(m_glContext) != EMSCRIPTEN_RESULT_SUCCESS)
    {
        Log("could not make the restored WebGL2 context current");
        return false;
    }
    // Objects created while the context was lost have no valid names either.
    ++m_generation;
    m_lost = false;
    DestroyBaseObjects(false);
    m_bbColor = 0;
    m_bbMsRb = 0;
    QueryCaps();
    ResetCaches();
    CreateBaseObjects();
    CreatePresentProgram();
    m_gammaDirty = true;
    NotifyContextRestored();
    return true;
}

bool WebGL2Backend::TestLoseContext() { return dxwebgl2_lose_context() != 0; }

bool WebGL2Backend::TestRestoreContext() { return dxwebgl2_restore_context() != 0; }

} // namespace dxwebgl2
