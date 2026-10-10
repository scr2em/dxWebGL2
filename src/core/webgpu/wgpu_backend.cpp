/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WebGPU backend: device creation, capabilities, back buffer, present pass, command submission, read-back and device
 * loss.
 *
 * requestAdapter, requestDevice and mapAsync are asynchronous while Direct3D's CreateDevice and Lock are not: the
 * calling thread waits in wgpuInstanceWaitAny, which suspends it with JSPI (or ASYNCIFY). The backend is therefore only
 * offered when the build can suspend (emscripten_has_asyncify()); see docs/WEBGPU.md, section 5.
 */
#include "core/webgpu/wgpu_backend.h"

#include <emscripten.h>
#include <emscripten/html5.h>

namespace dxwebgl2 {

Backend *CreateWebGPUBackend()
{
    WebGPUBackend *b = new WebGPUBackend();
    if (!b->Acquire())
    {
        delete b;
        return nullptr;
    }
    return b;
}

namespace {

std::string ToString(WGPUStringView s)
{
    if (!s.data) return std::string();
    if (s.length == WGPU_STRLEN) return std::string(s.data);
    return std::string(s.data, s.length);
}

inline WGPUStringView Label(const char *s) { return WGPUStringView{s, WGPU_STRLEN}; }

// The device callbacks may arrive after the backend is gone (a destroyed device reports its loss asynchronously):
// they find the backend through this slot, which the destructor clears.
WebGPUBackend *g_liveBackend = nullptr;

void DeviceLostCallback(WGPUDevice const *, WGPUDeviceLostReason reason, WGPUStringView message, void *ud, void *)
{
    if (g_liveBackend && g_liveBackend == ud) g_liveBackend->OnDeviceLost((int)reason, ToString(message).c_str());
}

void UncapturedErrorCallback(WGPUDevice const *, WGPUErrorType type, WGPUStringView message, void *ud, void *)
{
    if (g_liveBackend && g_liveBackend == ud) g_liveBackend->OnUncapturedError((int)type, ToString(message).c_str());
}

struct AdapterRequest { WGPUAdapter adapter = nullptr; std::string message; };
struct DeviceRequest { WGPUDevice device = nullptr; std::string message; };
struct MapRequest { bool ok = false; std::string message; };

const char *kPresentShader = R"(
@group(0) @binding(0) var bb: texture_2d<f32>;
@group(0) @binding(1) var lut: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}
@fragment fn fs_plain(@builtin(position) p: vec4f) -> @location(0) vec4f {
    return vec4f(textureLoad(bb, vec2i(p.xy), 0).rgb, 1.0);
}
@fragment fn fs_gamma(@builtin(position) p: vec4f) -> @location(0) vec4f {
    let c = textureLoad(bb, vec2i(p.xy), 0).rgb;
    let i = vec3i(floor(c * 255.0 + 0.5));
    return vec4f(textureLoad(lut, vec2i(i.r, 0), 0).r, textureLoad(lut, vec2i(i.g, 0), 0).g,
                 textureLoad(lut, vec2i(i.b, 0), 0).b, 1.0);
}
)";

} // namespace

WebGPUBackend::~WebGPUBackend()
{
    if (g_liveBackend == this) g_liveBackend = nullptr;
}

//------------------------------------------------------------------------------
// Device
//------------------------------------------------------------------------------
bool WebGPUBackend::WaitFuture(WGPUFuture future)
{
    WGPUFutureWaitInfo info = {future, false};
    const WGPUWaitStatus st = wgpuInstanceWaitAny(m_instance, 1, &info, UINT64_MAX);
    return st == WGPUWaitStatus_Success && info.completed;
}

bool WebGPUBackend::Acquire()
{
    if (m_device) return true;
    if (!emscripten_has_asyncify())
    {
        Log("WebGPU is not available: the application is not linked with -sJSPI (or -sASYNCIFY), which creating the "
            "device and reading back from the GPU need");
        DXW_HIT(Unsupported, "WebGPU backend needs a build that can suspend (-sJSPI)");
        return false;
    }
    if (!m_instance)
    {
        static const WGPUInstanceFeatureName features[] = {WGPUInstanceFeatureName_TimedWaitAny};
        WGPUInstanceDescriptor id = WGPU_INSTANCE_DESCRIPTOR_INIT;
        id.requiredFeatureCount = 1;
        id.requiredFeatures = features;
        m_instance = wgpuCreateInstance(&id);
        if (!m_instance)
        {
            Log("WebGPU is not available: no instance (navigator.gpu is missing)");
            return false;
        }
    }
    return RequestDevice();
}

bool WebGPUBackend::RequestDevice()
{
    if (m_adapter) { wgpuAdapterRelease(m_adapter); m_adapter = nullptr; }
    AdapterRequest ar;
    WGPURequestAdapterOptions opt = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    opt.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo aci = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    aci.mode = WGPUCallbackMode_WaitAnyOnly;
    aci.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *ud, void *) {
        AdapterRequest *r = static_cast<AdapterRequest *>(ud);
        if (status == WGPURequestAdapterStatus_Success) r->adapter = adapter;
        else r->message = ToString(message);
    };
    aci.userdata1 = &ar;
    if (!WaitFuture(wgpuInstanceRequestAdapter(m_instance, &opt, aci)) || !ar.adapter)
    {
        Log("WebGPU is not available: no adapter (%s)", ar.message.empty() ? "navigator.gpu.requestAdapter returned null" : ar.message.c_str());
        return false;
    }
    m_adapter = ar.adapter;

    // DXT through texture-compression-bc when the adapter has it.
    std::vector<WGPUFeatureName> features;
    if (wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_TextureCompressionBC)) features.push_back(WGPUFeatureName_TextureCompressionBC);

    DeviceRequest dr;
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.label = Label("dxWebGL2");
    dd.requiredFeatureCount = features.size();
    dd.requiredFeatures = features.data();
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback = DeviceLostCallback;
    dd.deviceLostCallbackInfo.userdata1 = this;
    dd.uncapturedErrorCallbackInfo.callback = UncapturedErrorCallback;
    dd.uncapturedErrorCallbackInfo.userdata1 = this;
    WGPURequestDeviceCallbackInfo dci = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dci.mode = WGPUCallbackMode_WaitAnyOnly;
    dci.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void *ud, void *) {
        DeviceRequest *r = static_cast<DeviceRequest *>(ud);
        if (status == WGPURequestDeviceStatus_Success) r->device = device;
        else r->message = ToString(message);
    };
    dci.userdata1 = &dr;
    g_liveBackend = this;
    if (!WaitFuture(wgpuAdapterRequestDevice(m_adapter, &dd, dci)) || !dr.device)
    {
        Log("WebGPU is not available: requestDevice failed (%s)", dr.message.c_str());
        return false;
    }
    m_device = dr.device;
    m_queue = wgpuDeviceGetQueue(m_device);
    m_bc = wgpuDeviceHasFeature(m_device, WGPUFeatureName_TextureCompressionBC);
    m_lost = m_lostByTest = m_restorable = false;
    return true;
}

void WebGPUBackend::ReleaseDevice()
{
    if (m_surface) { wgpuSurfaceRelease(m_surface); m_surface = nullptr; }
    if (m_queue) { wgpuQueueRelease(m_queue); m_queue = nullptr; }
    if (m_device) { wgpuDeviceRelease(m_device); m_device = nullptr; }
    if (m_adapter) { wgpuAdapterRelease(m_adapter); m_adapter = nullptr; }
}

void WebGPUBackend::QueryCaps()
{
    WGPULimits lim = WGPU_LIMITS_INIT;
    wgpuDeviceGetLimits(m_device, &lim);
    m_caps.maxTextureSize = (int)lim.maxTextureDimension2D;
    m_caps.maxCubeSize = (int)lim.maxTextureDimension2D;
    m_caps.max3DSize = (int)lim.maxTextureDimension3D;
    m_caps.maxVertexAttribs = (int)Min<uint32_t>(lim.maxVertexAttributes, 16);
    m_caps.maxVertexUniformVectors = 256;
    m_caps.maxTextureUnits = (int)lim.maxSampledTexturesPerShaderStage;
    m_caps.maxAnisotropy = 16.0f;
    m_caps.anisotropic = true;
    // Points are drawn as quads of any size; the limit is that of the WebGL2 implementations (SwiftShader, desktop GPUs).
    m_caps.maxPointSize = 1024.0f;
    // Multisampling: WebGPU has 1 and 4 samples (every request of 2 or more samples is rendered with 4).
    m_caps.maxSamples = (GetConfig().featureOverrides & 8) ? 1 : 4;
    m_caps.uniformAlignment = (int)lim.minUniformBufferOffsetAlignment;
    m_caps.s3tc = m_bc && !GetConfig().disableS3TC;
    m_caps.provokingVertex = true;  // flat interpolation takes the first vertex, like Direct3D
    m_caps.baseVertex = true;

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    std::string vendor = "WebGPU", device = "";
    if (wgpuAdapterGetInfo(m_adapter, &info) == WGPUStatus_Success)
    {
        const std::string v = ToString(info.vendor), a = ToString(info.architecture), d = ToString(info.device),
                          desc = ToString(info.description);
        if (!v.empty()) vendor = v;
        device = !desc.empty() ? desc : (!d.empty() ? d : a);
        if (device.empty()) device = a;
        if (!a.empty() && device != a) device += " (" + a + ")";
        wgpuAdapterInfoFreeMembers(info);
    }
    m_caps.vendor = vendor;
    m_caps.renderer = device.empty() ? vendor : vendor + " " + device;
    m_caps.version = "WebGPU";
    DiagSetGpu("WebGPU " + m_caps.renderer + ", texture-compression-bc " + (m_bc ? "yes" : "no") + ", max texture " +
               std::to_string(m_caps.maxTextureSize));
    Log("WebGPU adapter: %s, texture-compression-bc %s%s, max texture %d", m_caps.renderer.c_str(), m_bc ? "yes" : "no",
        (m_bc && !m_caps.s3tc) ? " (disabled: DXT decoded on the CPU)" : (m_bc ? "" : " (DXT decoded on the CPU)"),
        m_caps.maxTextureSize);
}

bool WebGPUBackend::Initialize(uint32_t width, uint32_t height)
{
    if (!Acquire()) return false;
    if (width && height) emscripten_set_canvas_element_size(GetConfig().canvas.c_str(), (int)width, (int)height);
    // The canvas gets a webgpu context.
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector src = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    src.selector = Label(GetConfig().canvas.c_str());
    WGPUSurfaceDescriptor sd = WGPU_SURFACE_DESCRIPTOR_INIT;
    sd.nextInChain = &src.chain;
    m_surface = wgpuInstanceCreateSurface(m_instance, &sd);
    if (!m_surface)
    {
        Log("could not create a WebGPU context on '%s'", GetConfig().canvas.c_str());
        DXW_HIT(Failed, "WebGPU canvas context creation failed");
        return false;
    }
    WGPUSurfaceCapabilities sc = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(m_surface, m_adapter, &sc) == WGPUStatus_Success && sc.formatCount)
        m_canvasFormat = sc.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(sc);
    QueryCaps();
    CreateBaseObjects();
    return true;
}

bool WebGPUBackend::ConfigureSurface()
{
    if (!m_surface || !m_bbWidth) return false;
    WGPUSurfaceConfiguration c = WGPU_SURFACE_CONFIGURATION_INIT;
    c.device = m_device;
    c.format = m_canvasFormat;
    c.usage = WGPUTextureUsage_RenderAttachment;
    c.width = m_bbWidth;
    c.height = m_bbHeight;
    c.alphaMode = WGPUCompositeAlphaMode_Opaque;
    c.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(m_surface, &c);
    return true;
}

/// Objects every draw depends on; created again on a new device.
void WebGPUBackend::CreateBaseObjects()
{
    // Uniform blocks: bind group 0, one buffer binding with a dynamic offset, seen by both stages.
    WGPUBindGroupLayoutEntry ue = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    ue.binding = 0;
    ue.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    ue.buffer.type = WGPUBufferBindingType_Uniform;
    ue.buffer.hasDynamicOffset = true;
    WGPUBindGroupLayoutDescriptor ud = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    ud.entryCount = 1;
    ud.entries = &ue;
    m_uniformLayout = wgpuDeviceCreateBindGroupLayout(m_device, &ud);
    m_uniformCap = 0;
    m_streamCap = 0;
    m_uniformUsed = m_streamUsed = 0;
    GrowRing(m_uniformBuffer, m_uniformCap, 256 * 1024, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "uniform ring");
    GrowRing(m_streamBuffer, m_streamCap, 1024 * 1024, WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, "stream ring");

    // 1x1 white textures for stages without a texture (2D, cube, volume).
    static const uint8_t white[6 * 4] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                         255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    for (int k = 0; k < 3; ++k)
    {
        WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = k == 2 ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
        td.size = {1, 1, k == 1 ? 6u : 1u};
        td.format = WGPUTextureFormat_RGBA8Unorm;
        m_whiteTex[k] = wgpuDeviceCreateTexture(m_device, &td);
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = m_whiteTex[k];
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D size = {1, 1, td.size.depthOrArrayLayers};
        wgpuQueueWriteTexture(m_queue, &dst, white, 4 * size.depthOrArrayLayers, &layout, &size);
        WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        vd.dimension = k == 0 ? WGPUTextureViewDimension_2D : (k == 1 ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_3D);
        m_whiteView[k] = wgpuTextureCreateView(m_whiteTex[k], &vd);
    }
    WGPUSamplerDescriptor wsd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    m_whiteSampler = wgpuDeviceCreateSampler(m_device, &wsd);
    m_pointSampler = m_whiteSampler;
    CreatePresentPipeline();
}

void WebGPUBackend::DestroyBaseObjects()
{
    auto release = [](auto &obj, auto fn) { if (obj) { fn(obj); obj = nullptr; } };
    for (auto &kv : m_pipelines)
        for (auto &e : kv.second) wgpuRenderPipelineRelease(e.second);
    m_pipelines.clear();
    m_lastPipeline = nullptr;
    for (auto &kv : m_clearPipelines) wgpuRenderPipelineRelease(kv.second);
    m_clearPipelines.clear();
    for (auto &kv : m_bindGroups)
        for (auto &e : kv.second) wgpuBindGroupRelease(e.second);
    m_bindGroups.clear();
    for (auto &kv : m_texLayouts) wgpuBindGroupLayoutRelease(kv.second);
    m_texLayouts.clear();
    for (auto &kv : m_samplers) { wgpuSamplerRelease(kv.second->sampler); delete kv.second; }
    m_samplers.clear();
    for (SubDepth &d : m_subDepth) { wgpuTextureViewRelease(d.view); wgpuTextureDestroy(d.tex); wgpuTextureRelease(d.tex); }
    m_subDepth.clear();
    for (int k = 0; k < 3; ++k)
    {
        release(m_whiteView[k], wgpuTextureViewRelease);
        release(m_whiteTex[k], wgpuTextureRelease);
    }
    release(m_whiteSampler, wgpuSamplerRelease);
    m_pointSampler = nullptr;
    release(m_uniformGroup, wgpuBindGroupRelease);
    release(m_uniformBuffer, wgpuBufferRelease);
    release(m_streamBuffer, wgpuBufferRelease);
    release(m_uniformLayout, wgpuBindGroupLayoutRelease);
    release(m_presentGroup, wgpuBindGroupRelease);
    release(m_presentLayout, wgpuBindGroupLayoutRelease);
    release(m_presentPipeline, wgpuRenderPipelineRelease);
    release(m_presentGammaPipeline, wgpuRenderPipelineRelease);
    release(m_gammaView, wgpuTextureViewRelease);
    release(m_gammaTex, wgpuTextureRelease);
    release(m_clearModule, wgpuShaderModuleRelease);
    release(m_clearPipelineLayout, wgpuPipelineLayoutRelease);
    if (m_pass) { wgpuRenderPassEncoderRelease(m_pass); m_pass = nullptr; }
    if (m_encoder) { wgpuCommandEncoderRelease(m_encoder); m_encoder = nullptr; }
    m_recorded = false;
    m_uniformCap = m_streamCap = 0;
    m_uniformUsed = m_streamUsed = 0;
    m_targetsDirty = true;
    m_pendColor = m_pendDepth = m_pendStencil = false;
}

/// Grows a staging ring's GPU buffer to hold at least `need` bytes (only between submits: nothing may refer to it).
void WebGPUBackend::GrowRing(WGPUBuffer &buffer, uint32_t &capacity, uint32_t need, WGPUBufferUsage usage, const char *label)
{
    if (buffer && need <= capacity) return;
    uint32_t cap = Max<uint32_t>(capacity, 64 * 1024);
    while (cap < need) cap *= 2;
    if (buffer) wgpuBufferRelease(buffer);
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label = Label(label);
    bd.usage = usage;
    bd.size = cap;
    buffer = wgpuDeviceCreateBuffer(m_device, &bd);
    capacity = cap;
    if (&buffer == &m_uniformBuffer)
    {
        // The bind group of the uniform blocks refers to the buffer.
        if (m_uniformGroup) wgpuBindGroupRelease(m_uniformGroup);
        WGPUBindGroupEntry e = WGPU_BIND_GROUP_ENTRY_INIT;
        e.binding = 0;
        e.buffer = m_uniformBuffer;
        e.offset = 0;
        e.size = WgslMaxUniformBlockSize();
        WGPUBindGroupDescriptor gd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        gd.layout = m_uniformLayout;
        gd.entryCount = 1;
        gd.entries = &e;
        m_uniformGroup = wgpuDeviceCreateBindGroup(m_device, &gd);
        m_curGroup0 = nullptr;
    }
}

void WebGPUBackend::Shutdown()
{
    if (!m_device) return;
    if (!m_lost) Submit();
    DestroyBackBuffer();
    DestroyBaseObjects();
    if (m_surface) wgpuSurfaceUnconfigure(m_surface);
    g_liveBackend = nullptr;
    wgpuDeviceDestroy(m_device);
    ReleaseDevice();
    if (m_instance) { wgpuInstanceRelease(m_instance); m_instance = nullptr; }
}

void WebGPUBackend::CanvasSize(int &width, int &height)
{
    width = height = 0;
    emscripten_get_canvas_element_size(GetConfig().canvas.c_str(), &width, &height);
}

void WebGPUBackend::InvalidateState()
{
    // Nothing is applied piecemeal: only the redundant-call filter of the open pass is reset.
    if (m_pass) ResetPassCache();
}

//------------------------------------------------------------------------------
// Commands
//------------------------------------------------------------------------------
WGPUCommandEncoder WebGPUBackend::Encoder()
{
    if (!m_encoder) m_encoder = wgpuDeviceCreateCommandEncoder(m_device, nullptr);
    return m_encoder;
}

void WebGPUBackend::Submit()
{
    EndPass();
    if (!m_device) return;
    // The staging rings go up first: queue writes take effect before the commands submitted after them.
    if (m_uniformUsed) wgpuQueueWriteBuffer(m_queue, m_uniformBuffer, 0, m_uniformStaging.data(), m_uniformUsed);
    if (m_streamUsed) wgpuQueueWriteBuffer(m_queue, m_streamBuffer, 0, m_streamStaging.data(), m_streamUsed);
    g_gl.uploadBytes += m_uniformUsed + m_streamUsed;
    if (m_encoder)
    {
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(m_encoder, nullptr);
        wgpuQueueSubmit(m_queue, 1, &cb);
        wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(m_encoder);
        m_encoder = nullptr;
    }
    m_recorded = false;
    m_uniformUsed = m_streamUsed = 0;
    ++m_serial;
    ++m_epoch;
}

//------------------------------------------------------------------------------
// Back buffer
//------------------------------------------------------------------------------
bool WebGPUBackend::CreateBackBuffer(uint32_t w, uint32_t h, int samples)
{
    m_bbWidth = w;
    m_bbHeight = h;
    m_samples = samples;
    emscripten_set_canvas_element_size(GetConfig().canvas.c_str(), (int)w, (int)h);
    ConfigureSurface();
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.label = Label("back buffer");
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    td.size = {w, h, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm;
    m_bb = wgpuDeviceCreateTexture(m_device, &td);
    m_bbView = wgpuTextureCreateView(m_bb, nullptr);
    if (m_samples)
    {
        td.label = Label("back buffer (multisampled)");
        td.usage = WGPUTextureUsage_RenderAttachment;
        td.sampleCount = 4;
        m_bbMs = wgpuDeviceCreateTexture(m_device, &td);
        m_bbMsView = wgpuTextureCreateView(m_bbMs, nullptr);
    }
    // The present pass reads the back buffer and the gamma table.
    if (m_presentGroup) { wgpuBindGroupRelease(m_presentGroup); m_presentGroup = nullptr; }
    WGPUBindGroupEntry e[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    e[0].binding = 0;
    e[0].textureView = m_bbView;
    e[1].binding = 1;
    e[1].textureView = m_gammaView;
    WGPUBindGroupDescriptor gd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    gd.layout = m_presentLayout;
    gd.entryCount = 2;
    gd.entries = e;
    m_presentGroup = wgpuDeviceCreateBindGroup(m_device, &gd);
    m_targetsDirty = true;
    return true;
}

void WebGPUBackend::DestroyBackBuffer()
{
    if (m_color.kind == RenderTargetRef::Kind::BackBuffer) EndPass();
    m_targetsDirty = true;
    if (m_presentGroup) { wgpuBindGroupRelease(m_presentGroup); m_presentGroup = nullptr; }
    if (m_bbView) { wgpuTextureViewRelease(m_bbView); m_bbView = nullptr; }
    if (m_bb) { wgpuTextureDestroy(m_bb); wgpuTextureRelease(m_bb); m_bb = nullptr; }
    if (m_bbMsView) { wgpuTextureViewRelease(m_bbMsView); m_bbMsView = nullptr; }
    if (m_bbMs) { wgpuTextureDestroy(m_bbMs); wgpuTextureRelease(m_bbMs); m_bbMs = nullptr; }
    m_samples = 0;
}

bool WebGPUBackend::ReadBackBuffer(const RECT &rc, uint8_t *rgbaOut)
{
    if (!m_bb || m_lost) return false;
    return ReadTextureRect(m_bb, WGPUTextureFormat_RGBA8Unorm, 0, 0, rc, rgbaOut);
}

/// Copies a rectangle of a texture level (or cube face) through a mappable buffer and waits for it.
bool WebGPUBackend::ReadTextureRect(WGPUTexture texture, WGPUTextureFormat format, uint32_t layer, uint32_t level,
                                    const RECT &rc, uint8_t *rgbaOut)
{
    if (format != WGPUTextureFormat_RGBA8Unorm && format != WGPUTextureFormat_BGRA8Unorm) return false;
    const uint32_t w = (uint32_t)(rc.right - rc.left), h = (uint32_t)(rc.bottom - rc.top);
    if (!w || !h) return true;
    if (g_diagOn) DiagHit(Hit::Perf, "GPU read-back of a texture or the back buffer (mapAsync)");
    Submit();
    const uint32_t bytesPerRow = (w * 4 + 255) & ~255u;
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label = Label("read-back");
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    bd.size = (uint64_t)bytesPerRow * h;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(m_device, &bd);
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = texture;
    src.mipLevel = level;
    src.origin = {(uint32_t)rc.left, (uint32_t)rc.top, layer};
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = buf;
    dst.layout.bytesPerRow = bytesPerRow;
    dst.layout.rowsPerImage = h;
    WGPUExtent3D size = {w, h, 1};
    wgpuCommandEncoderCopyTextureToBuffer(Encoder(), &src, &dst, &size);
    Submit();

    MapRequest mr;
    WGPUBufferMapCallbackInfo mci = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mci.mode = WGPUCallbackMode_WaitAnyOnly;
    mci.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void *ud, void *) {
        MapRequest *r = static_cast<MapRequest *>(ud);
        r->ok = status == WGPUMapAsyncStatus_Success;
        if (!r->ok) r->message = ToString(message);
    };
    mci.userdata1 = &mr;
    ++g_gl.sync;
    bool ok = WaitFuture(wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, (size_t)bd.size, mci)) && mr.ok;
    if (ok)
    {
        const uint8_t *p = static_cast<const uint8_t *>(wgpuBufferGetConstMappedRange(buf, 0, (size_t)bd.size));
        ok = p != nullptr;
        if (ok)
        {
            const bool swap = format == WGPUTextureFormat_BGRA8Unorm;
            for (uint32_t y = 0; y < h; ++y)
            {
                const uint8_t *s = p + (size_t)y * bytesPerRow;
                uint8_t *d = rgbaOut + (size_t)y * w * 4;
                if (!swap) memcpy(d, s, (size_t)w * 4);
                else
                    for (uint32_t x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
            }
        }
        wgpuBufferUnmap(buf);
    }
    else
        Log("WebGPU read-back failed (%s)", mr.message.c_str());
    wgpuBufferDestroy(buf);
    wgpuBufferRelease(buf);
    return ok;
}

//------------------------------------------------------------------------------
// Present
//------------------------------------------------------------------------------
void WebGPUBackend::CreatePresentPipeline()
{
    WGPUBindGroupLayoutEntry le[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    for (int i = 0; i < 2; ++i)
    {
        le[i].binding = (uint32_t)i;
        le[i].visibility = WGPUShaderStage_Fragment;
        le[i].texture.sampleType = WGPUTextureSampleType_Float;
        le[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    WGPUBindGroupLayoutDescriptor ld = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    ld.entryCount = 2;
    ld.entries = le;
    m_presentLayout = wgpuDeviceCreateBindGroupLayout(m_device, &ld);
    WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &m_presentLayout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(m_device, &pld);
    WGPUShaderModule module = CompileModule(kPresentShader, "present");

    // Two pipelines: without and with the gamma ramp.
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = m_canvasFormat;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = module;
    fs.entryPoint = Label("fs_plain");
    fs.targetCount = 1;
    fs.targets = &target;
    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.label = Label("present");
    pd.layout = layout;
    pd.vertex.module = module;
    pd.vertex.entryPoint = Label("vs");
    pd.fragment = &fs;
    m_presentPipeline = wgpuDeviceCreateRenderPipeline(m_device, &pd);
    fs.entryPoint = Label("fs_gamma");
    pd.label = Label("present with gamma");
    m_presentGammaPipeline = wgpuDeviceCreateRenderPipeline(m_device, &pd);
    wgpuShaderModuleRelease(module);
    wgpuPipelineLayoutRelease(layout);

    // Gamma table: 256 x 1 RGBA.
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.label = Label("gamma ramp");
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.size = {256, 1, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm;
    m_gammaTex = wgpuDeviceCreateTexture(m_device, &td);
    m_gammaView = wgpuTextureCreateView(m_gammaTex, nullptr);
    m_gammaDirty = true;
}

void WebGPUBackend::SetGammaRamp(const uint16_t *red, const uint16_t *green, const uint16_t *blue, bool active)
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

void WebGPUBackend::Present()
{
    if (!m_device || m_lost) return;
    EndPass();
    if (m_gammaActive && m_gammaDirty && m_gammaTex)
    {
        // The table is only read by present passes, which are submitted right away: no recorded command uses it.
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = m_gammaTex;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = 256 * 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D size = {256, 1, 1};
        wgpuQueueWriteTexture(m_queue, &dst, m_gammaLut, sizeof m_gammaLut, &layout, &size);
        m_gammaDirty = false;
    }
    WGPUSurfaceTexture st = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(m_surface, &st);
    if (st.texture && m_presentGroup)
    {
        WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
        WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        ca.view = view;
        ca.loadOp = WGPULoadOp_Clear;
        ca.storeOp = WGPUStoreOp_Store;
        ca.clearValue = {0, 0, 0, 1};
        WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(Encoder(), &rp);
        wgpuRenderPassEncoderSetPipeline(pass, m_gammaActive ? m_presentGammaPipeline : m_presentPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_presentGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuTextureViewRelease(view);
    }
    if (st.texture) wgpuTextureRelease(st.texture);
    Submit();
}

void WebGPUBackend::DebugDescribeBackBuffer()
{
    if (!m_bb) return;
    const uint32_t w = m_bbWidth, h = m_bbHeight;
    std::vector<uint8_t> px((size_t)w * h * 4);
    RECT rc = {0, 0, (LONG)w, (LONG)h};
    if (!ReadBackBuffer(rc, px.data())) return;
    size_t lit = 0;
    uint32_t minX = w, minY = h, maxX = 0, maxY = 0;
    const uint8_t *first = nullptr;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint8_t *p = &px[((size_t)y * w + x) * 4];
            if (p[0] | p[1] | p[2])
            {
                if (!first) first = p;
                ++lit;
                minX = Min(minX, x); maxX = Max(maxX, x); minY = Min(minY, y); maxY = Max(maxY, y);
            }
        }
    if (lit)
        Log("back buffer %ux%u: %u non-black pixels in (%u,%u)-(%u,%u), first is %u,%u,%u,%u", w, h, (unsigned)lit, minX, minY, maxX, maxY,
            first[0], first[1], first[2], first[3]);
    else
        Log("back buffer %ux%u: all black", w, h);
}

//------------------------------------------------------------------------------
// Device loss
//
// GPUDevice.lost maps onto the Direct3D device loss the front end implements (D3DERR_DEVICELOST, then
// D3DERR_DEVICENOTRESET, then Reset()): a lost device is reported lost once, after which a new one can be requested
// (RestoreContext). The test aid destroys the device and lets the front end see it restorable only after
// TestRestoreContext(), like WEBGL_lose_context.
//------------------------------------------------------------------------------
void WebGPUBackend::OnDeviceLost(int reason, const char *message)
{
    if (m_lost) return;
    if (reason == WGPUDeviceLostReason_Destroyed && !g_liveBackend) return;
    Log("the WebGPU device was lost (reason %d: %s)", reason, message);
    m_lost = true;
    m_ctxEvent = 1;
}

void WebGPUBackend::OnUncapturedError(int type, const char *message)
{
    ++m_errorCount;
    if (m_errorCount <= 20)
        Log("WebGPU %s error: %s", type == WGPUErrorType_Validation ? "validation" : (type == WGPUErrorType_OutOfMemory ? "out of memory" : "internal"),
            message);
    else if (m_errorCount == 21)
        Log("WebGPU: further errors are not logged");
    DXW_HIT(Failed, "WebGPU error (type %d)", type);
}

bool WebGPUBackend::IsContextLost()
{
    return m_lost && !m_restorable;
}

void WebGPUBackend::OnContextLost()
{
    // Every object of the old device is dead; the GpuObjects drop their handles (releasing them is harmless).
    if (!m_lostByTest) m_restorable = true;
    if (m_pass) { wgpuRenderPassEncoderRelease(m_pass); m_pass = nullptr; }
    if (m_encoder) { wgpuCommandEncoderRelease(m_encoder); m_encoder = nullptr; }
    m_recorded = false;
    m_uniformUsed = m_streamUsed = 0;
    Backend::OnContextLost();
}

bool WebGPUBackend::RestoreContext()
{
    // A new device (the adapter of a lost one cannot create another).
    DestroyBackBuffer();
    DestroyBaseObjects();
    g_liveBackend = nullptr;
    if (m_device) wgpuDeviceDestroy(m_device);
    if (m_surface) { wgpuSurfaceRelease(m_surface); m_surface = nullptr; }
    if (m_queue) { wgpuQueueRelease(m_queue); m_queue = nullptr; }
    if (m_device) { wgpuDeviceRelease(m_device); m_device = nullptr; }
    if (!RequestDevice())
    {
        m_lost = true;
        return false;
    }
    if (!Initialize(0, 0)) return false;
    ++m_serial;
    ++m_epoch;
    NotifyContextRestored();
    return true;
}

bool WebGPUBackend::TestLoseContext()
{
    if (!m_device || m_lost) return false;
    m_lost = true;
    m_lostByTest = true;
    m_restorable = false;
    m_ctxEvent = 1;
    wgpuDeviceDestroy(m_device);
    return true;
}

bool WebGPUBackend::TestRestoreContext()
{
    if (!m_lost) return false;
    m_restorable = true;
    m_ctxEvent = 2;
    return true;
}

} // namespace dxwebgl2
