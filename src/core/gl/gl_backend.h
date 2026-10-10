/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * The WebGL2 backend.
 *
 * Implements core/backend.h on WebGL2 (through Emscripten's OpenGL ES 3 emulation). Every WebGL call crosses from
 * WebAssembly into JavaScript, so the backend keeps a cache of what it applied and issues only the differences: the
 * pipeline state is diffed field by field, buffer / texture / sampler / program bindings are cached, uniform groups
 * are uploaded only when their version changed since the program last received them, vertex attribute setups live
 * in cached vertex array objects.
 *
 *   gl_backend.cpp    context, capabilities, back buffer, presentation, context loss
 *   gl_resources.cpp  buffers, textures, surfaces, samplers, read-back
 *   gl_program.cpp    GLSL program compilation and linking
 *   gl_draw.cpp       render targets, clears, pipeline state, uniforms, texture and vertex bindings, draws
 *
 * Objects of a context that was lost are not deleted (their names died with it): every GL object remembers the
 * generation of the context it was created in, and the generation changes when the context is lost and restored.
 */
#pragma once

#include "core/gl/gl.h"
#include "core/backend.h"
#include "core/texture_storage.h"
#include "core/buffer_storage.h"

namespace dxwebgl2 {

enum
{
    GL_UPLOAD_UNIT = MAX_STAGES, ///< texture unit used for resource uploads (stages use 0..7)
};

struct GLBuffer final : GpuBuffer
{
    GLuint name = 0;
    GLenum target = GL_ARRAY_BUFFER;
    uint32_t generation = 0;
};

struct GLTexture final : GpuTexture
{
    GLuint name = 0;
    GLenum target = GL_TEXTURE_2D;
    GLint baseLevel = 0;         ///< GL_TEXTURE_BASE_LEVEL as last set
    const FormatInfo *info = nullptr;
    bool decode = false;         ///< DXT decoded to RGBA8 on upload
    uint32_t generation = 0;
    GLenum FaceTarget(uint32_t face) const { return target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target; }
};

struct GLSurface final : GpuSurface
{
    GLuint renderbuffer = 0;
    uint32_t generation = 0;
};

struct GLSampler final : GpuSampler
{
    GLuint name = 0;
};

class GLProgram final : public Program
{
public:
    GLuint id = 0;
    GLint loc[U_COUNT];
    uint32_t generation = 0;
};

/// Vertex array objects are cached by layout, buffers and strides (not by the base vertex, see BindAttributes).
enum { VAO_KEY_STREAMS = 4, VAO_CACHE_LIMIT = 384 };
struct VaoKey
{
    uint32_t layout;
    uint32_t pointerBase;
    uint32_t buf[VAO_KEY_STREAMS];
    uint16_t stride[VAO_KEY_STREAMS];
    uint8_t stream[VAO_KEY_STREAMS];
};
struct VaoEntry
{
    VaoKey key;
    GLuint vao = 0;
    GLuint element = 0xFFFFFFFF; ///< element array buffer bound in this vertex array
    uint32_t enabled = 0;
    uint64_t lastUse = 0;
};

class WebGL2Backend final : public Backend, private UniformSink
{
public:
    WebGL2Backend() : Backend(BackendKind::WebGL2) {}
    ~WebGL2Backend() override = default;

    //-- Backend (gl_backend.cpp) ---------------------------------------------------------
    bool Initialize(uint32_t width, uint32_t height) override;
    void Shutdown() override;
    std::string Description() const override { return "WebGL2: " + m_caps.renderer; }
    void CanvasSize(int &width, int &height) override;
    void InvalidateState() override;
    void InvalidateVertexInput() override { m_attribSig = 0; }
    bool CreateBackBuffer(uint32_t width, uint32_t height, int samples) override;
    void DestroyBackBuffer() override;
    void SetGammaRamp(const uint16_t *red, const uint16_t *green, const uint16_t *blue, bool active) override;
    void Present() override;
    bool ReadBackBuffer(const RECT &rc, uint8_t *rgbaOut) override;
    void DebugDescribeBackBuffer() override;
    bool IsContextLost() override;
    bool HasContextEvent() const override { return m_ctxEvent != 0; }
    void ClearContextEvent() override { m_ctxEvent = 0; }
    void OnContextLost() override;
    bool RestoreContext() override;
    bool TestLoseContext() override;
    bool TestRestoreContext() override;

    //-- Backend (gl_resources.cpp) --------------------------------------------------------
    GpuBuffer *CreateBuffer(BufferKind kind, uint32_t size) override;
    void DestroyBuffer(GpuBuffer *buffer) override;
    void WriteBuffer(GpuBuffer *buffer, uint32_t offset, uint32_t size, const void *data, bool initial) override;
    GpuTexture *CreateTexture(const TextureDesc &desc) override;
    void DestroyTexture(GpuTexture *texture) override;
    void WriteTexture(GpuTexture *texture, const TextureRegion &region, const uint8_t *data, uint32_t rowPitch) override;
    bool ReadTexture(GpuTexture *texture, uint32_t face, uint32_t level, const RECT &rc, uint8_t *rgbaOut) override;
    GpuSurface *CreateSurface(const SurfaceDesc &desc) override;
    void DestroySurface(GpuSurface *surface) override;
    bool ReadSurface(GpuSurface *surface, const RECT &rc, uint8_t *rgbaOut) override;
    GpuSampler *GetSampler(const SamplerDesc &desc) override;

    //-- Backend (gl_program.cpp) ----------------------------------------------------------
    Program *CreateProgram(const ProgramKey &key, const VertexShaderObject *vs, const PixelShaderObject *ps) override;
    void DestroyProgram(Program *program) override;

    //-- Backend (gl_draw.cpp) -------------------------------------------------------------
    void SetRenderTargets(const RenderTargetRef &color, const RenderTargetRef &depth) override;
    void Clear(const ClearCall &call) override;
    bool Draw(const DrawCall &call) override;

private:
    // gl_backend.cpp
    bool CreateContext(uint32_t width, uint32_t height);
    void InstallContextListeners();
    void EnableExtensions();
    void QueryCaps();
    void CreateBaseObjects();
    void DestroyBaseObjects(bool contextAlive);
    void ResetCaches();
    void CreatePresentProgram();
    void PresentToCanvas();
    void ResolveBackBuffer();

    // gl_resources.cpp: the state cache shared by the resources
    void BindForUpload(GLenum target, GLuint tex);
    void BindBuffer(GLenum target, GLuint buf)
    {
        GLuint &cached = BufferBindingSlot(target);
        if (cached != buf) { glBindBuffer(target, buf); cached = buf; }
    }
    /// Where the cache keeps what is bound to `target` (the element array binding belongs to the bound vertex array).
    GLuint &BufferBindingSlot(GLenum target)
    {
        return target == GL_ARRAY_BUFFER ? m_boundArrayBuffer : (m_curVao ? m_curVao->element : m_boundElementBuffer);
    }
    /// Must be called before deleting a GL texture / buffer so cached bindings are dropped.
    void ForgetTexture(GLuint tex);
    void ForgetBuffer(GLuint buf);
    bool ReadTextureLevel(GLuint tex, GLenum target, GLenum faceTarget, int level, const RECT &rc, uint8_t *rgbaOut);
    bool ReadRenderbuffer(GLuint rb, const RECT &rc, uint8_t *rgbaOut);
    bool IsFresh(uint32_t generation) const { return generation == m_generation; }

    // gl_program.cpp
    GLProgram *CompileProgram(const ProgramKey &key, const std::string &vs, const std::string &fs);

    // gl_draw.cpp
    void ApplyRenderTargets();
    void ApplyPipeline(const PipelineState &want);
    void UploadUniforms(GLProgram *p, const uint32_t *versions, UniformSource &source);
    void BindTextures(const ProgramKey &key, const TextureBinding *bindings);
    bool BindAttributes(const DrawCall &c, UINT pointerBase, GLuint userBuffer);
    void SetAttributePointers(const VertexLayout *layout, const GLuint *bufs, const UINT *strides, UINT pointerBase,
                              uint32_t oldEnabled, uint32_t *newEnabled);
    void EvictVao();
    void DeleteVao(uint64_t hash, VaoEntry *e);
    void ClearVaoCache(bool contextAlive);
    size_t UploadStream(const void *data, size_t size, bool index, size_t align);
    void DebugStageCheck(const char *stage, const DrawCall *c);

    // UniformSink: writes into the program being drawn with
    void Vec4(UniformId u, const float *v, int count) override { glUniform4fv(m_uniformLoc[u], count, v); }
    void Vec2(UniformId u, const float *v, int count) override { glUniform2fv(m_uniformLoc[u], count, v); }
    void Float(UniformId u, const float *v, int count) override
    {
        if (count == 1) glUniform1f(m_uniformLoc[u], v[0]);
        else glUniform1fv(m_uniformLoc[u], count, v);
    }
    void Mat4(UniformId u, const float *m, int count) override { glUniformMatrix4fv(m_uniformLoc[u], count, GL_FALSE, m); }

    //-- Context ---------------------------------------------------------------------------
    int m_glContext = 0;
    volatile int m_ctxEvent = 0;        ///< set by the canvas event listeners: 1 = lost, 2 = restored
    uint32_t m_generation = 1;          ///< changes when the context is lost and when it is restored
    bool m_lost = false;                ///< the loss was reported (OnContextLost) and RestoreContext() did not run yet
    bool m_explicitSwap = false;

    //-- Back buffer -------------------------------------------------------------------------
    uint32_t m_bbWidth = 0, m_bbHeight = 0;
    int m_samples = 0;                  ///< multisampling of the back buffer (0 = off)
    GLuint m_bbColor = 0;               ///< the back buffer texture (resolve target when multisampled)
    GLuint m_bbMsRb = 0;                ///< multisampled colour renderbuffer (when m_samples)
    GLuint m_resolveRead = 0, m_resolveDraw = 0;
    bool m_bbDirty = false;

    //-- Present -----------------------------------------------------------------------------
    GLuint m_presentProgram = 0, m_presentVao = 0, m_gammaTex = 0;
    GLint m_presentGammaLoc = -1;
    uint8_t m_gammaLut[256 * 4] = {};
    bool m_gammaActive = false;
    bool m_gammaDirty = false;
    float m_presentGammaValue = -1.0f;

    //-- Render targets ------------------------------------------------------------------------
    RenderTargetRef m_color, m_depth;
    bool m_targetsDirty = true;         ///< render target attachments must be re-established before the next draw
    GLuint m_fbo = 0;
    uint64_t m_fboColorKey = 0, m_fboDepthKey = 0; ///< what m_fbo currently has attached
    GLuint m_boundFbo = 0xFFFFFFFF;                ///< framebuffer bound for drawing
    struct SubDepth { GLuint rb; uint32_t w, h; int samples; bool stencil; };
    std::vector<SubDepth> m_subDepth;   ///< depth buffers standing in for ones whose size / sample count mismatches the target
    GLuint m_scratchFbo = 0;

    //-- State cache ---------------------------------------------------------------------------
    GLuint m_boundArrayBuffer = 0xFFFFFFFF;
    GLuint m_boundElementBuffer = 0xFFFFFFFF;
    GLuint m_boundTex[MAX_STAGES + 1][3];
    int m_activeUnit = -1;
    GLuint m_boundSampler[MAX_STAGES];
    GLuint m_boundProgram = 0xFFFFFFFF;
    PipelineState m_applied;
    bool m_pipelineValid = false;
    float m_clearColor[4] = {};
    float m_clearDepth = 0;
    GLint m_clearStencil = 0;
    bool m_clearColorValid = false, m_clearDepthValid = false, m_clearStencilValid = false;
    const GLint *m_uniformLoc = nullptr; ///< locations of the program the uniform sink writes to

    //-- Objects of the backend ------------------------------------------------------------------
    GLuint m_whiteTex[3] = {};
    GLuint m_whiteSampler = 0;
    std::unordered_map<std::string, GLSampler *> m_samplers;

    //-- Vertex input --------------------------------------------------------------------------
    GLuint m_vao = 0;               ///< scratch vertex array (uncached draws)
    std::unordered_multimap<uint64_t, VaoEntry *> m_vaoCache;
    VaoEntry *m_curVao = nullptr;   ///< bound cached vertex array, null when the scratch one is bound
    std::unordered_map<uint64_t, uint32_t> m_vaoSeen; ///< keys with a base vertex seen once (promoted on the second sighting)
    uint64_t m_vaoClock = 0;
    uint32_t m_enabledAttribs = 0;  ///< arrays enabled in the scratch vertex array
    uint64_t m_attribSig = 0;       ///< signature of the scratch vertex array setup; reset whenever a buffer is deleted
    GLuint m_streamVB = 0, m_streamIB = 0; ///< streaming buffers of draws from user memory
    size_t m_streamVBSize = 0, m_streamIBSize = 0, m_streamVBPos = 0, m_streamIBPos = 0;

    //-- Scratch ---------------------------------------------------------------------------------
    std::vector<uint8_t> m_uploadScratch;
    std::vector<uint32_t> m_triScratch, m_lineScratch;
    unsigned m_drawCounter = 0;
};

} // namespace dxwebgl2
