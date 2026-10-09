/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * dxwebgl2/dxwebgl2.h - public C API of dxWebGL2.
 *
 * dxWebGL2 implements Direct3D on WebGL2 for Emscripten builds. The Direct3D 8 interfaces themselves
 * are used through the regular <d3d8.h> / <d3dx8.h> headers and Direct3DCreate8(); the functions below
 * only let the host application steer the parts that have no Direct3D equivalent (which canvas to
 * render to, how frames are presented, what the device reports). All of them are optional and, unless
 * noted, must be called before IDirect3D8::CreateDevice().
 *
 * The library originated in the Command & Conquer Generals Zero Hour web port
 * (https://github.com/scr2em/Generals-Zero-Hours-Web).
 */
#ifndef DXWEBGL2_DXWEBGL2_H
#define DXWEBGL2_DXWEBGL2_H

#define DXWEBGL2_VERSION_MAJOR 0
#define DXWEBGL2_VERSION_MINOR 1
#define DXWEBGL2_VERSION_PATCH 0
#define DXWEBGL2_VERSION_STRING "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

/// Version of the library as "major.minor.patch".
const char *dxwebgl2_GetVersionString(void);

/// Selects the canvas the WebGL2 context is created on, as a CSS selector
/// ("#canvas" by default). When the device renders from a pthread, the canvas
/// has to be an OffscreenCanvas transferred to that thread
/// (-sOFFSCREENCANVAS_SUPPORT -sOFFSCREENCANVASES_TO_PTHREAD).
void dxwebgl2_SetCanvas(const char *selector);

/// Size in pixels of the "desktop" that the display mode enumeration reports
/// (IDirect3D8::GetAdapterDisplayMode). The default is the browser screen size
/// when it can be queried, 1280x720 otherwise.
void dxwebgl2_SetDesktopSize(unsigned width, unsigned height);

/// Vertex/pixel shader versions reported by IDirect3DDevice8::GetDeviceCaps(),
/// as (major << 8) | minor, e.g. 0x0101 for 1.1 and 0x0104 for ps 1.4.
/// 0 reports no programmable shader support, which keeps applications on their
/// fixed-function code paths. This is the default.
void dxwebgl2_SetShaderModel(unsigned vertexShaderVersion, unsigned pixelShaderVersion);

/// How finished frames are handed to the browser by IDirect3DDevice8::Present().
enum dxwebgl2_PresentMode {
    /// The context is created with explicitSwapControl and Present() calls
    /// emscripten_webgl_commit_frame(). This is the default: it is what an
    /// OffscreenCanvas rendered from a thread that never yields to its event
    /// loop (-sPROXY_TO_PTHREAD) needs. When the browser/build cannot do
    /// explicit swapping (no -sOFFSCREENCANVAS_SUPPORT) the device falls back
    /// to DXWEBGL2_PRESENT_IMPLICIT automatically.
    DXWEBGL2_PRESENT_EXPLICIT = 1,
    /// The browser presents the canvas when the calling task returns to the
    /// event loop (requestAnimationFrame / emscripten_set_main_loop style).
    DXWEBGL2_PRESENT_IMPLICIT = 0,
};
void dxwebgl2_SetPresentMode(int mode);

/// Optional callbacks into the host application, so that the library does not
/// depend on it at link level.
typedef struct dxwebgl2_PlatformHooks {
    /// Called after CreateDevice() and Reset(), once the canvas drawing buffer
    /// has been resized to the back buffer size.
    void (*OnClientSize)(unsigned width, unsigned height);
    /// Called at the end of every Present().
    ///
    /// Finding (tests/runner/run_test.mjs --frames=N): a pthread that renders to an
    /// OffscreenCanvas from a loop that never returns to its event loop does
    /// not get its frames shown by Chromium, with or without explicitSwapControl
    /// (emscripten_webgl_commit_frame() is a no-op in current browsers; the
    /// canvas only updates when the thread yields). Yielding once per frame from
    /// this hook fixes it: link the application with -sJSPI (or -sASYNCIFY) and call
    /// emscripten_sleep(0) here.
    void (*OnFramePresented)(void);
} dxwebgl2_PlatformHooks;
void dxwebgl2_SetPlatformHooks(const dxwebgl2_PlatformHooks *hooks);

/// Where the WebGL calls are executed when the device is used from a pthread:
/// 0 (default) = on the calling thread (needs the canvas transferred as an
/// OffscreenCanvas), 1 = proxied to the main thread (-sOFFSCREEN_FRAMEBUFFER;
/// every GL call is forwarded, slow but works with a thread that never yields).
void dxwebgl2_SetContextProxy(int mode);

/// Enables extra GL error checking, a log of unsupported D3D features and, every 120th frame, a
/// log of the draw calls and of what the back buffer holds. Bit 0 is that switch; the higher bits
/// force GL state off for every draw to find which state hides a picture: bit 1 culling,
/// bit 2 blending, bit 3 the depth test (so 1 | 2 = 3 enables the log and disables culling).
void dxwebgl2_SetDebug(int flags);

/// Releases the memory of the system-memory shadow copies of managed textures
/// after they were uploaded, when enabled (default: disabled). Saves memory at
/// the price of a GPU read-back when such a level is locked again.
void dxwebgl2_SetReleaseTextureShadows(int enable);

/// Makes the device ignore WEBGL_compressed_texture_s3tc so DXT textures are
/// decoded on the CPU (for testing that path).
void dxwebgl2_SetDisableS3TC(int disable);

/// Counters of the last completed frame (valid after the first Present()).
typedef struct dxwebgl2_Stats {
    unsigned frames;                ///< Present() calls so far
    unsigned draws, drawsIndexed, drawsUserPointer, primitives;
    unsigned glCalls, glDraw, glState, glBind, glUniform, glUpload, glAttrib, glOther, glSync;
    unsigned uploadBytes;           ///< bytes handed to glBufferData/SubData and glTex(Sub)Image
    unsigned setRenderState, setTextureStageState, setTexture, setTransform, setShaderConstant, locks;
    unsigned programSwitches, pipelineChanges, uniformUploads, programsCreated;
    unsigned contextLosses, contextRestores;
} dxwebgl2_Stats;
void dxwebgl2_GetStats(dxwebgl2_Stats *out);

/// Turns on the diagnostics report: every `seconds` seconds the library prints, to the console,
/// the per-frame draw/WebGL call counts and a table of every unsupported or approximated Direct3D 8
/// feature the application hit (render state values, texture formats, shader opcodes, failed creations,
/// context losses). 0 switches it off (default; nothing is counted or formatted while off).
/// -dxwebgl2-report[=seconds] on the command line (the page URL's ?arg=) does the same.
void dxwebgl2_SetReport(int seconds);
/// Prints the report now (when enabled).
void dxwebgl2_PrintReport(void);

/// Test aid: after `frames` presented frames the WebGL context is dropped with WEBGL_lose_context
/// and restored `restoreAfterFrames` frames later, which exercises the application's device-lost path
/// (-dxwebgl2-loseafter=N[,M] on the command line). 0 disables.
void dxwebgl2_LoseContextAfterFrames(int frames, int restoreAfterFrames);
/// Test aid: drops the WebGL context now (WEBGL_lose_context.loseContext()); restoreContext() is
/// called after the library notices the loss and `restoreAfterFrames` Present()s went by.
void dxwebgl2_LoseContextNow(int restoreAfterFrames);

/// Test aid: pretends that optional WebGL extensions are missing, to exercise the fallbacks. Bits:
/// 1 = WEBGL_provoking_vertex, 2 = WEBGL_draw_instanced_base_vertex_base_instance,
/// 4 = WEBGL_polygon_mode, 8 = multisampling. Call before CreateDevice().
void dxwebgl2_SetFeatureOverrides(int disabledMask);

/// Test aid: number of times the feature hits whose description contains `substring` were recorded
/// (the diagnostics of dxwebgl2_SetReport; recording only happens while the report is on).
unsigned dxwebgl2_GetHitCount(const char *substring);

/// Parses the -dxwebgl2-* options out of an argument vector (the library also reads the C runtime's
/// __argv by itself when a device is created). Options: -dxwebgl2-report[=s], -dxwebgl2-shaders
/// (advertise vs.1.1/ps.1.4 so applications take their shader paths), -dxwebgl2-loseafter=N[,M].
void dxwebgl2_ParseArguments(int argc, char **argv);

/// Resolves the entry points that an application looks up with GetProcAddress() on
/// "D3D8.DLL" ("Direct3DCreate8"). Returns null for unknown names. A
/// Win32 compatibility layer's LoadLibrary("D3D8.DLL") can be backed by this.
void *dxwebgl2_LookupProc(const char *name);

/// Info about the GL implementation that backs the device (valid after
/// CreateDevice()). Strings are owned by the library.
const char *dxwebgl2_GetRendererString(void);

#ifdef __cplusplus
}
#endif

#endif /* DXWEBGL2_DXWEBGL2_H */
