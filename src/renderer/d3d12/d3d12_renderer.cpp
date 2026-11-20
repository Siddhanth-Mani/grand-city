#ifdef _WIN32
// Direct3D 12 backend.
//
// Frame structure:
//   1. sun shadow pass: every shadow caster into a 2048^2 depth map from an
//      orthographic sun camera that follows the player (snapped to texels so
//      shadows do not shimmer);
//   2. main pass into a 4x MSAA target: a procedural sky (gradient, sun, moon,
//      clouds, stars) and then the scene, lit with GGX + sky reflections +
//      PCF shadows + up to 16 point lights, ACES tone mapped (shaders in
//      d3d12_shaders.h);
//   3. MSAA resolve into the swap-chain buffer, then the 2D overlay (menus,
//      HUD, minimap) with a font atlas rendered with GDI at start-up.
//
// Draw calls are recorded into a list during the frame and replayed in
// present(), so the same draws feed both the shadow and the main pass.
// 2 frames in flight, one persistently mapped upload ring per frame for
// constants and overlay vertices, and a mesh cache that uploads each
// MeshData once (keyed by its address; the ResourceManager owns meshes for
// the whole run).
#include "renderer/d3d12/d3d12_renderer.h"
#include "renderer/d3d12/d3d12_shaders.h"
#include "core/logger.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace gc6 {
namespace {
constexpr UINT kFrames = 2;
constexpr UINT64 kRingBytes = 8ull * 1024 * 1024; // per frame
constexpr float kCanvasW = 1280.0f, kCanvasH = 720.0f;
constexpr UINT kShadowSize = 2048;
constexpr float kShadowHalf = 70.0f;   // metres covered either side of the focus
constexpr DXGI_FORMAT kColourFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// Font atlas: printable ASCII (32..127) in a 16 x 6 grid of 32 x 64 cells,
// glyphs rendered at a 48 px em. A glyph drawn at "size" px advances 0.55 x size.
constexpr int kCellW = 32, kCellH = 64, kAtlasW = 16 * kCellW, kAtlasH = 6 * kCellH;
constexpr float kEm = 48.0f;

struct FrameConstants {
    float viewProj[16];
    float shadowVP[16];
    float invViewProj[16];
    float camPos[4];
    float sunDir[4];
    float sunColour[4];
    float skyZenith[4];
    float skyHorizon[4];
    float misc[4];
    float lightPos[16][4];
    float lightCol[16][4];
};
struct DrawConstants {
    float model[16];
    float colour[4];
    float params[4];
};
struct OverlayVertex {
    float x, y, u, v;
    float r, g, b, a;
};
struct GpuMesh {
    ComPtr<ID3D12Resource> vb, ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    UINT indexCount = 0;
    size_t sourceVertices = 0, sourceIndices = 0;
};
struct DrawItem {
    GpuMesh* mesh = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS constants = 0;
    bool castsShadow = true;
};

ComPtr<ID3D12Resource> makeUploadBuffer(ID3D12Device* device, UINT64 bytes) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&res))))
        return nullptr;
    return res;
}
ComPtr<ID3D12Resource> makeTexture(ID3D12Device* device, DXGI_FORMAT format, UINT w, UINT h, UINT samples,
                                   D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                                   const D3D12_CLEAR_VALUE* clear) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w; td.Height = h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = format;
    td.SampleDesc.Count = samples;
    td.Flags = flags;
    ComPtr<ID3D12Resource> res;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, state, clear,
                                               IID_PPV_ARGS(&res))))
        return nullptr;
    return res;
}
void transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}
bool compile(const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "gc6_shaders", nullptr, nullptr,
                            entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errors);
    if (FAILED(hr)) {
        std::string msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
        Logger::error(std::string("D3D12: shader ") + entry + " failed: " + msg);
        return false;
    }
    return true;
}
Vec3 mix3(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
} // namespace

struct D3D12Renderer::Impl {
    HWND hwnd = nullptr;
    int width = 1280, height = 720;
    UINT frameIndex = 0;
    UINT samples = 4;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12Resource> backBuffers[kFrames];
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;
    UINT rtvStride = 0, dsvStride = 0, srvStride = 0;
    ComPtr<ID3D12Resource> sceneColour, sceneDepth, shadowMap;
    D3D12_RESOURCE_STATES shadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    ComPtr<ID3D12CommandAllocator> allocators[kFrames];
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceCounter = 0;
    UINT64 frameFence[kFrames] = {};
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> psoScene, psoSceneWire, psoSky, psoShadow, psoOverlay;
    ComPtr<ID3D12Resource> fontTexture;
    // Upload ring: kFrames slices of kRingBytes, persistently mapped.
    ComPtr<ID3D12Resource> ring;
    UINT8* ringCpu = nullptr;
    UINT64 ringOffset = 0;
    bool ringFullWarned = false;
    std::unordered_map<const MeshData*, GpuMesh> meshes;
    std::vector<DrawItem> items;
    std::vector<OverlayVertex> overlay;
    Camera camera;
    Vec3 sunDir{0, 1, 0}, sunColour{1, 1, 1};
    float sunIntensity = 1.0f;
    std::vector<DynamicLight> lights;
    PostSettings post;
    bool wireframe = false;
    bool frameOpen = false;
    size_t draws = 0;
    LARGE_INTEGER startTime{}, frequency{};
    std::string screenshotPath;   // non-empty: save the next frame
    ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readbackFootprint{};
    void saveReadback();

    D3D12_CPU_DESCRIPTOR_HANDLE rtv(UINT i) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(i) * rtvStride;
        return h;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv(UINT i) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(i) * dsvStride;
        return h;
    }
    void waitIdle() {
        if (!queue || !fence) return;
        UINT64 v = ++fenceCounter;
        queue->Signal(fence.Get(), v);
        if (fence->GetCompletedValue() < v) {
            fence->SetEventOnCompletion(v, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }
    // Returns a CPU pointer + GPU address for `bytes` in this frame's slice.
    bool alloc(UINT64 bytes, UINT64 align, void*& cpu, D3D12_GPU_VIRTUAL_ADDRESS& gpu) {
        UINT64 base = static_cast<UINT64>(frameIndex) * kRingBytes;
        UINT64 off = (ringOffset + align - 1) & ~(align - 1);
        if (off + bytes > kRingBytes) {
            if (!ringFullWarned) { Logger::warn("D3D12: upload ring full, skipping draws this frame"); ringFullWarned = true; }
            return false;
        }
        ringOffset = off + bytes;
        cpu = ringCpu + base + off;
        gpu = ring->GetGPUVirtualAddress() + base + off;
        return true;
    }
    // Vertex format: position + normal (6 floats). Meshes without normals
    // upload zeros; the pixel shader then falls back to a face normal.
    GpuMesh* mesh(const MeshData& m) {
        auto it = meshes.find(&m);
        if (it != meshes.end() && it->second.sourceVertices == m.positions.size() &&
            it->second.sourceIndices == m.indices.size())
            return &it->second;
        GpuMesh g;
        std::vector<float> verts(m.positions.size() * 6, 0.0f);
        bool hasNormals = m.normals.size() == m.positions.size();
        for (size_t i = 0; i < m.positions.size(); ++i) {
            verts[i * 6 + 0] = m.positions[i].x;
            verts[i * 6 + 1] = m.positions[i].y;
            verts[i * 6 + 2] = m.positions[i].z;
            if (hasNormals) {
                verts[i * 6 + 3] = m.normals[i].x;
                verts[i * 6 + 4] = m.normals[i].y;
                verts[i * 6 + 5] = m.normals[i].z;
            }
        }
        UINT64 vbBytes = verts.size() * sizeof(float);
        UINT64 ibBytes = m.indices.size() * sizeof(uint32_t);
        g.vb = makeUploadBuffer(device.Get(), vbBytes);
        g.ib = makeUploadBuffer(device.Get(), ibBytes);
        if (!g.vb || !g.ib) return nullptr;
        void* p = nullptr;
        D3D12_RANGE none{0, 0};
        g.vb->Map(0, &none, &p);
        std::memcpy(p, verts.data(), vbBytes);
        g.vb->Unmap(0, nullptr);
        g.ib->Map(0, &none, &p);
        std::memcpy(p, m.indices.data(), ibBytes);
        g.ib->Unmap(0, nullptr);
        g.vbv = {g.vb->GetGPUVirtualAddress(), static_cast<UINT>(vbBytes), sizeof(float) * 6};
        g.ibv = {g.ib->GetGPUVirtualAddress(), static_cast<UINT>(ibBytes), DXGI_FORMAT_R32_UINT};
        g.indexCount = static_cast<UINT>(m.indices.size());
        g.sourceVertices = m.positions.size();
        g.sourceIndices = m.indices.size();
        // A mesh whose size changed is replaced; the old buffers may still be
        // in flight, so wait for the GPU before releasing them.
        if (it != meshes.end()) waitIdle();
        auto& slot = meshes[&m];
        slot = std::move(g);
        return &slot;
    }
    float nightFactor() const { return clampf(1.0f - (sunIntensity - 0.15f) / 0.6f, 0.0f, 1.0f); }
    void skyColours(Vec3& zenith, Vec3& horizon) const {
        float night = nightFactor();
        float elev = normalize(sunDir).y;
        float dusk = (1.0f - night) * clampf(1.0f - elev / 0.35f, 0.0f, 1.0f);
        Vec3 zen = mix3(Vec3(0.10f, 0.25f, 0.62f), Vec3(0.16f, 0.20f, 0.42f), dusk);
        Vec3 hor = mix3(Vec3(0.50f, 0.64f, 0.82f), Vec3(0.95f, 0.50f, 0.25f), dusk);
        zen = mix3(zen, Vec3(0.003f, 0.006f, 0.018f), night);
        hor = mix3(hor, Vec3(0.020f, 0.030f, 0.055f), night);
        // Rain: an overcast, grey sky.
        float wet = clampf(post.wetness, 0.0f, 1.0f) * 0.75f;
        float lz = (zen.x + zen.y + zen.z) / 3.0f, lh = (hor.x + hor.y + hor.z) / 3.0f;
        zenith = mix3(zen, Vec3(lz, lz, lz * 1.05f) * 1.1f, wet);
        horizon = mix3(hor, Vec3(lh, lh, lh * 1.05f), wet);
    }
    void buildFrameConstants(FrameConstants& fc) const;
    void submit(const MeshData& mesh, const Mat4& model, const float colour[4], const float params[4],
                bool castsShadow);
    bool createFontAtlas();
    bool createPipelines();
    bool createTargets();
};

void D3D12Renderer::Impl::buildFrameConstants(FrameConstants& fc) const {
    std::memset(&fc, 0, sizeof(fc));
    const Camera& c = camera;
    float aspect = static_cast<float>(width) / static_cast<float>(height);
    Mat4 proj = glm::perspectiveRH_ZO(glm::radians(c.fov), aspect, std::max(c.nearPlane, 0.1f), c.farPlane);
    Mat4 vpm = proj * c.view();
    Mat4 inv = glm::inverse(vpm);
    std::memcpy(fc.viewProj, &vpm[0][0], sizeof(fc.viewProj));
    std::memcpy(fc.invViewProj, &inv[0][0], sizeof(fc.invViewProj));

    // Sun shadow camera: orthographic, centred just ahead of the camera
    // target, snapped to whole shadow-map texels.
    Vec3 L = normalize(sunDir);
    if (L.y < 0.18f) { L.y = 0.18f; L = normalize(L); }
    Vec3 look = c.target - c.position;
    look.y = 0.0f;
    Vec3 focus = c.target + (length(look) > 0.01f ? normalize(look) * 18.0f : Vec3(0.0f));
    focus.y = 0.0f;
    Vec3 up = std::fabs(L.y) > 0.98f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
    Mat4 lview = glm::lookAtRH(focus + L * 220.0f, focus, up);
    Mat4 lproj = glm::orthoRH_ZO(-kShadowHalf, kShadowHalf, -kShadowHalf, kShadowHalf, 1.0f, 480.0f);
    Vec4 origin = lproj * lview * Vec4(0, 0, 0, 1);
    float texels = kShadowSize * 0.5f;
    float sx = origin.x * texels, sy = origin.y * texels;
    lproj[3][0] += (std::round(sx) - sx) / texels;
    lproj[3][1] += (std::round(sy) - sy) / texels;
    Mat4 svp = lproj * lview;
    std::memcpy(fc.shadowVP, &svp[0][0], sizeof(fc.shadowVP));

    float night = nightFactor();
    Vec3 zen, hor;
    skyColours(zen, hor);
    fc.camPos[0] = c.position.x; fc.camPos[1] = c.position.y; fc.camPos[2] = c.position.z;
    fc.camPos[3] = 0.0018f + night * 0.0012f + post.wetness * 0.004f;
    Vec3 sd = normalize(sunDir);
    if (sd.y < 0.04f) { sd.y = 0.04f; sd = normalize(sd); }
    fc.sunDir[0] = sd.x; fc.sunDir[1] = sd.y; fc.sunDir[2] = sd.z;
    fc.sunDir[3] = sunIntensity * (1.0f - post.wetness * 0.6f);
    fc.sunColour[0] = sunColour.x; fc.sunColour[1] = sunColour.y; fc.sunColour[2] = sunColour.z;
    fc.sunColour[3] = night;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float seconds = frequency.QuadPart
                        ? static_cast<float>(static_cast<double>(now.QuadPart - startTime.QuadPart) /
                                             static_cast<double>(frequency.QuadPart))
                        : 0.0f;
    fc.skyZenith[0] = zen.x; fc.skyZenith[1] = zen.y; fc.skyZenith[2] = zen.z;
    fc.skyZenith[3] = std::fmod(seconds, 3600.0f);
    float exposure = (post.exposure > 0.05f ? post.exposure : 1.1f) * (1.0f + night * 0.5f);
    fc.skyHorizon[0] = hor.x; fc.skyHorizon[1] = hor.y; fc.skyHorizon[2] = hor.z;
    fc.skyHorizon[3] = exposure;
    size_t n = std::min<size_t>(lights.size(), 16);
    fc.misc[0] = static_cast<float>(n);
    fc.misc[1] = 1.0f / static_cast<float>(kShadowSize);
    fc.misc[2] = sunIntensity > 0.05f ? 1.0f : 0.0f;
    fc.misc[3] = clampf(post.wetness, 0.0f, 1.0f);
    for (size_t i = 0; i < n; ++i) {
        const DynamicLight& l = lights[i];
        fc.lightPos[i][0] = l.position.x; fc.lightPos[i][1] = l.position.y;
        fc.lightPos[i][2] = l.position.z; fc.lightPos[i][3] = l.radius;
        fc.lightCol[i][0] = l.colour.x * l.intensity;
        fc.lightCol[i][1] = l.colour.y * l.intensity;
        fc.lightCol[i][2] = l.colour.z * l.intensity;
        fc.lightCol[i][3] = (!l.isHeadlight && !l.isPolice) ? 1.0f : 0.0f;
    }
}

D3D12Renderer::D3D12Renderer(void* hwnd, int w, int h) : impl_(std::make_unique<Impl>()) {
    impl_->hwnd = static_cast<HWND>(hwnd);
    impl_->width = w;
    impl_->height = h;
}
D3D12Renderer::~D3D12Renderer() {
    impl_->waitIdle();
    if (impl_->fenceEvent) CloseHandle(impl_->fenceEvent);
}

bool D3D12Renderer::Impl::createPipelines() {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 2;  // t0 font atlas, t1 shadow map
    range.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &range;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1] = samplers[0];
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ShaderRegister = 1;
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 3;
    rs.pParameters = params;
    rs.NumStaticSamplers = 2;
    rs.pStaticSamplers = samplers;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        Logger::error("D3D12: root signature serialisation failed");
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSig))))
        return false;

    ComPtr<ID3DBlob> vs, ps, vsSh, vsSky, psSky, vsO, psO;
    if (!compile("VSMain", "vs_5_0", vs) || !compile("PSMain", "ps_5_0", ps) ||
        !compile("VSShadow", "vs_5_0", vsSh) || !compile("VSSky", "vs_5_0", vsSky) ||
        !compile("PSSky", "ps_5_0", psSky) ||
        !compile("VSOverlay", "vs_5_0", vsO) || !compile("PSOverlay", "ps_5_0", psO))
        return false;

    D3D12_INPUT_ELEMENT_DESC sceneLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {sceneLayout, 2};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // procedural meshes mix windings
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.RasterizerState.MultisampleEnable = samples > 1;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kColourFormat;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.SampleDesc.Count = samples;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoScene)))) return false;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoSceneWire)))) return false;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

    // Sky: a full-screen triangle behind everything (no depth test or write).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC sky = pd;
    sky.VS = {vsSky->GetBufferPointer(), vsSky->GetBufferSize()};
    sky.PS = {psSky->GetBufferPointer(), psSky->GetBufferSize()};
    sky.InputLayout = {nullptr, 0};
    sky.DepthStencilState.DepthEnable = FALSE;
    sky.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device->CreateGraphicsPipelineState(&sky, IID_PPV_ARGS(&psoSky)))) return false;

    // Shadow casters: depth only, single sample, slope-scaled bias.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC sh = pd;
    sh.VS = {vsSh->GetBufferPointer(), vsSh->GetBufferSize()};
    sh.PS = {nullptr, 0};
    sh.NumRenderTargets = 0;
    sh.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    sh.SampleDesc.Count = 1;
    sh.RasterizerState.MultisampleEnable = FALSE;
    sh.RasterizerState.SlopeScaledDepthBias = 1.5f;
    sh.RasterizerState.DepthBiasClamp = 0.01f;
    if (FAILED(device->CreateGraphicsPipelineState(&sh, IID_PPV_ARGS(&psoShadow)))) return false;

    D3D12_INPUT_ELEMENT_DESC overlayLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC od = pd;
    od.VS = {vsO->GetBufferPointer(), vsO->GetBufferSize()};
    od.PS = {psO->GetBufferPointer(), psO->GetBufferSize()};
    od.InputLayout = {overlayLayout, 3};
    od.SampleDesc.Count = 1;
    od.RasterizerState.MultisampleEnable = FALSE;
    od.DSVFormat = DXGI_FORMAT_UNKNOWN;
    od.DepthStencilState.DepthEnable = FALSE;
    od.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    auto& bl = od.BlendState.RenderTarget[0];
    bl.BlendEnable = TRUE;
    bl.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    bl.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    bl.BlendOp = D3D12_BLEND_OP_ADD;
    bl.SrcBlendAlpha = D3D12_BLEND_ONE;
    bl.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    bl.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&od, IID_PPV_ARGS(&psoOverlay)));
}

// Off-screen targets: the (multisampled) scene colour + depth and the sun
// shadow map. RTV slot kFrames is the scene colour; DSV 0 scene, 1 shadow.
bool D3D12Renderer::Impl::createTargets() {
    D3D12_CLEAR_VALUE cc{};
    cc.Format = kColourFormat;
    D3D12_CLEAR_VALUE dc{};
    dc.Format = DXGI_FORMAT_D32_FLOAT;
    dc.DepthStencil.Depth = 1.0f;
    UINT w = static_cast<UINT>(width), h = static_cast<UINT>(height);
    sceneColour = makeTexture(device.Get(), kColourFormat, w, h, samples, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                              D3D12_RESOURCE_STATE_RENDER_TARGET, &cc);
    sceneDepth = makeTexture(device.Get(), DXGI_FORMAT_D32_FLOAT, w, h, samples,
                             D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc);
    shadowMap = makeTexture(device.Get(), DXGI_FORMAT_R32_TYPELESS, kShadowSize, kShadowSize, 1,
                            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dc);
    if (!sceneColour || !sceneDepth || !shadowMap) return false;
    shadowState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    device->CreateRenderTargetView(sceneColour.Get(), nullptr, rtv(kFrames));
    device->CreateDepthStencilView(sceneDepth.Get(), nullptr, dsv(0));
    D3D12_DEPTH_STENCIL_VIEW_DESC sdv{};
    sdv.Format = DXGI_FORMAT_D32_FLOAT;
    sdv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(shadowMap.Get(), &sdv, dsv(1));
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE slot = srvHeap->GetCPUDescriptorHandleForHeapStart();
    slot.ptr += srvStride;
    device->CreateShaderResourceView(shadowMap.Get(), &sv, slot);
    return true;
}

// Renders printable ASCII with GDI into a DIB, then uploads the coverage as
// an R8 texture. Consolas ships with every Windows install since Vista.
bool D3D12Renderer::Impl::createFontAtlas() {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kAtlasW;
    bi.bmiHeader.biHeight = -kAtlasH; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp) return false;
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, static_cast<size_t>(kAtlasW) * kAtlasH * 4);
    HFONT font = CreateFontW(-static_cast<int>(kEm), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    SIZE adv{};
    GetTextExtentPoint32W(dc, L"M", 1, &adv);
    for (int c = 32; c < 128; ++c) {
        int cell = c - 32;
        int x = (cell % 16) * kCellW + (kCellW - adv.cx) / 2;
        int y = (cell / 16) * kCellH + 4;
        wchar_t ch = static_cast<wchar_t>(c == 127 ? '-' : c);
        TextOutW(dc, x, y, &ch, 1);
    }
    GdiFlush();
    std::vector<UINT8> coverage(static_cast<size_t>(kAtlasW) * kAtlasH);
    const UINT8* px = static_cast<const UINT8*>(bits);
    for (size_t i = 0; i < coverage.size(); ++i) coverage[i] = px[i * 4 + 1];
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = kAtlasW; td.Height = kAtlasH; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&fontTexture))))
        return false;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, &fp, &rows, &rowBytes, &total);
    auto staging = makeUploadBuffer(device.Get(), total);
    if (!staging) return false;
    UINT8* dst = nullptr;
    D3D12_RANGE none{0, 0};
    staging->Map(0, &none, reinterpret_cast<void**>(&dst));
    for (UINT r = 0; r < rows; ++r)
        std::memcpy(dst + fp.Offset + static_cast<size_t>(r) * fp.Footprint.RowPitch,
                    coverage.data() + static_cast<size_t>(r) * kAtlasW, kAtlasW);
    staging->Unmap(0, nullptr);
    allocators[0]->Reset();
    cmd->Reset(allocators[0].Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = staging.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = fp;
    to.pResource = fontTexture.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.SubresourceIndex = 0;
    cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    transition(cmd.Get(), fontTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->Close();
    ID3D12CommandList* lists[] = {cmd.Get()};
    queue->ExecuteCommandLists(1, lists);
    waitIdle(); // staging buffer must outlive the copy

    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(fontTexture.Get(), &sv, srvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

bool D3D12Renderer::init() {
    auto& d = *impl_;
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif
    QueryPerformanceFrequency(&d.frequency);
    QueryPerformanceCounter(&d.startTime);
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        Logger::error("D3D12: CreateDXGIFactory2 failed");
        return false;
    }
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d.device)))) {
        Logger::error("D3D12: no Direct3D 12 capable GPU");
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(d.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&d.queue)))) return false;

    // 4x MSAA when the GPU supports it for both targets, otherwise none.
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms{};
    ms.Format = kColourFormat;
    ms.SampleCount = 4;
    bool colourOk = SUCCEEDED(d.device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms, sizeof(ms))) &&
                    ms.NumQualityLevels > 0;
    ms.Format = DXGI_FORMAT_D32_FLOAT;
    ms.NumQualityLevels = 0;
    bool depthOk = SUCCEEDED(d.device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms, sizeof(ms))) &&
                   ms.NumQualityLevels > 0;
    d.samples = colourOk && depthOk ? 4 : 1;

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.BufferCount = kFrames;
    sd.Width = static_cast<UINT>(d.width);
    sd.Height = static_cast<UINT>(d.height);
    sd.Format = kColourFormat;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;
    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(factory->CreateSwapChainForHwnd(d.queue.Get(), d.hwnd, &sd, nullptr, nullptr, &sc1)))
        return false;
    factory->MakeWindowAssociation(d.hwnd, DXGI_MWA_NO_ALT_ENTER);
    sc1.As(&d.swapChain);
    d.frameIndex = d.swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = kFrames + 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.rtvHeap)))) return false;
    d.rtvStride = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT i = 0; i < kFrames; ++i) {
        d.swapChain->GetBuffer(i, IID_PPV_ARGS(&d.backBuffers[i]));
        d.device->CreateRenderTargetView(d.backBuffers[i].Get(), nullptr, d.rtv(i));
        if (FAILED(d.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&d.allocators[i]))))
            return false;
    }
    hd.NumDescriptors = 2;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.dsvHeap)))) return false;
    d.dsvStride = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    hd.NumDescriptors = 2;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.srvHeap)))) return false;
    d.srvStride = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!d.createTargets()) { Logger::error("D3D12: render target creation failed"); return false; }
    if (FAILED(d.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d.allocators[0].Get(),
                                           nullptr, IID_PPV_ARGS(&d.cmd))))
        return false;
    d.cmd->Close();
    if (FAILED(d.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d.fence)))) return false;
    d.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    d.ring = makeUploadBuffer(d.device.Get(), kRingBytes * kFrames);
    D3D12_RANGE none{0, 0};
    if (!d.ring || FAILED(d.ring->Map(0, &none, reinterpret_cast<void**>(&d.ringCpu)))) return false;
    if (!d.createPipelines()) { Logger::error("D3D12: pipeline creation failed"); return false; }
    if (!d.createFontAtlas()) { Logger::error("D3D12: font atlas creation failed"); return false; }
    Logger::info("D3D12 renderer initialised (" + std::to_string(d.width) + "x" + std::to_string(d.height) +
                 ", " + std::to_string(d.samples) + "x MSAA, " + std::to_string(kShadowSize) + " shadow map)");
    return true;
}

void D3D12Renderer::beginFrame() {
    auto& d = *impl_;
    if (d.frameOpen) return; // the game and the app may both start the frame
    d.frameOpen = true;
    d.draws = 0;
    d.items.clear();
    d.overlay.clear();
    d.ringOffset = 0;
    if (d.fence->GetCompletedValue() < d.frameFence[d.frameIndex]) {
        d.fence->SetEventOnCompletion(d.frameFence[d.frameIndex], d.fenceEvent);
        WaitForSingleObject(d.fenceEvent, INFINITE);
    }
}


void D3D12Renderer::drawMesh(const MeshData& mesh, const Mat4& model, const Vec3& colour) {
    // Colours above 1 (lamps, head/tail lights) glow instead of clipping.
    float peak = std::max(colour.x, std::max(colour.y, colour.z));
    float emissive = peak > 1.0f ? peak - 1.0f : 0.0f;
    Vec3 albedo = peak > 1.0f ? colour / peak : colour;
    const float c[4] = {albedo.x, albedo.y, albedo.z, emissive};
    const float p[4] = {0.0f, 0.6f, static_cast<float>(kShadeStandard), 0.0f};
    impl_->submit(mesh, model, c, p, true);
}

void D3D12Renderer::drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) {
    float glow = std::max(mat.emissive.x, std::max(mat.emissive.y, mat.emissive.z));
    const float c[4] = {mat.albedo.x, mat.albedo.y, mat.albedo.z, glow};
    const float p[4] = {mat.metalness, mat.roughness, static_cast<float>(mat.shading), mat.variant};
    bool casts = mat.shading != kShadeGround && mat.shading != kShadeEmissive;
    impl_->submit(mesh, model, c, p, casts);
}

void D3D12Renderer::Impl::submit(const MeshData& mesh, const Mat4& model, const float colour[4],
                                 const float params[4], bool castsShadow) {
    auto& d = *this;
    if (!d.frameOpen || mesh.positions.empty() || mesh.indices.empty()) return;
    GpuMesh* g = d.mesh(mesh);
    if (!g) return;
    DrawConstants dc{};
    std::memcpy(dc.model, &model[0][0], sizeof(dc.model));
    std::memcpy(dc.colour, colour, sizeof(dc.colour));
    std::memcpy(dc.params, params, sizeof(dc.params));
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    if (!d.alloc(sizeof(dc), 256, cpu, gpu)) return;
    std::memcpy(cpu, &dc, sizeof(dc));
    d.items.push_back({g, gpu, castsShadow});
    ++d.draws;
}

void D3D12Renderer::drawRect2D(float x, float y, float w, float h, const Vec4& col) {
    auto& o = impl_->overlay;
    auto nx = [](float px) { return px / kCanvasW * 2.0f - 1.0f; };
    auto ny = [](float py) { return 1.0f - py / kCanvasH * 2.0f; };
    OverlayVertex a{nx(x), ny(y), -1, -1, col.r, col.g, col.b, col.a};
    OverlayVertex b{nx(x + w), ny(y), -1, -1, col.r, col.g, col.b, col.a};
    OverlayVertex c{nx(x + w), ny(y + h), -1, -1, col.r, col.g, col.b, col.a};
    OverlayVertex e{nx(x), ny(y + h), -1, -1, col.r, col.g, col.b, col.a};
    o.insert(o.end(), {a, b, c, a, c, e});
}

void D3D12Renderer::drawText2D(float x, float y, float size, const std::string& text, const Vec4& col) {
    auto& o = impl_->overlay;
    auto nx = [](float px) { return px / kCanvasW * 2.0f - 1.0f; };
    auto ny = [](float py) { return 1.0f - py / kCanvasH * 2.0f; };
    const float advance = size * 0.55f;
    const float qw = size * kCellW / kEm, qh = size * kCellH / kEm;
    float penX = x, penY = y - size * 0.12f;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == '\n') { penX = x; penY += size * 1.35f; continue; }
        if ((ch & 0xC0) == 0x80) continue;      // UTF-8 continuation byte
        int code = ch >= 0x80 ? '-' : ch;        // dashes and other non-ASCII
        if (code < 32) code = ' ';
        if (code != ' ') {
            int cell = code - 32;
            float u0 = static_cast<float>((cell % 16) * kCellW) / kAtlasW;
            float v0 = static_cast<float>((cell / 16) * kCellH) / kAtlasH;
            float u1 = u0 + static_cast<float>(kCellW) / kAtlasW;
            float v1 = v0 + static_cast<float>(kCellH) / kAtlasH;
            float x0 = penX - (qw - advance) * 0.5f, y0 = penY;
            OverlayVertex a{nx(x0), ny(y0), u0, v0, col.r, col.g, col.b, col.a};
            OverlayVertex b{nx(x0 + qw), ny(y0), u1, v0, col.r, col.g, col.b, col.a};
            OverlayVertex c{nx(x0 + qw), ny(y0 + qh), u1, v1, col.r, col.g, col.b, col.a};
            OverlayVertex e{nx(x0), ny(y0 + qh), u0, v1, col.r, col.g, col.b, col.a};
            o.insert(o.end(), {a, b, c, a, c, e});
        }
        penX += advance;
    }
}


void D3D12Renderer::setCamera(const Camera& cam) { impl_->camera = cam; }
void D3D12Renderer::setSun(const Vec3& dir, const Vec3& colour, float intensity) {
    impl_->sunDir = dir;
    impl_->sunColour = colour;
    impl_->sunIntensity = intensity;
}
void D3D12Renderer::setLights(const LightRig& rig) {
    impl_->lights.assign(rig.lights.begin(),
                         rig.lights.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(rig.lights.size(), 16)));
}
void D3D12Renderer::setPostSettings(const PostSettings& s) { impl_->post = s; }
void D3D12Renderer::setWireframe(bool on) { impl_->wireframe = on; }

void D3D12Renderer::present() {
    auto& d = *impl_;
    if (!d.frameOpen) beginFrame(); // menus with no 3D pass still get a frame
    ID3D12GraphicsCommandList* c = d.cmd.Get();
    d.allocators[d.frameIndex]->Reset();
    c->Reset(d.allocators[d.frameIndex].Get(), d.psoShadow.Get());

    FrameConstants fc;
    d.buildFrameConstants(fc);
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameCb = 0;
    bool haveFrame = d.alloc(sizeof(fc), 256, cpu, frameCb);
    if (haveFrame) std::memcpy(cpu, &fc, sizeof(fc));

    c->SetGraphicsRootSignature(d.rootSig.Get());
    ID3D12DescriptorHeap* heaps[] = {d.srvHeap.Get()};
    c->SetDescriptorHeaps(1, heaps);
    c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (haveFrame) c->SetGraphicsRootConstantBufferView(0, frameCb);

    // 1. Sun shadow map.
    transition(c, d.shadowMap.Get(), d.shadowState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto shadowDsv = d.dsv(1);
    c->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    if (haveFrame && fc.misc[2] > 0.5f && !d.items.empty()) {
        c->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
        D3D12_VIEWPORT svp{0, 0, static_cast<float>(kShadowSize), static_cast<float>(kShadowSize), 0, 1};
        D3D12_RECT ssc{0, 0, static_cast<LONG>(kShadowSize), static_cast<LONG>(kShadowSize)};
        c->RSSetViewports(1, &svp);
        c->RSSetScissorRects(1, &ssc);
        for (const auto& it : d.items) {
            if (!it.castsShadow) continue;
            c->SetGraphicsRootConstantBufferView(1, it.constants);
            c->IASetVertexBuffers(0, 1, &it.mesh->vbv);
            c->IASetIndexBuffer(&it.mesh->ibv);
            c->DrawIndexedInstanced(it.mesh->indexCount, 1, 0, 0, 0);
        }
    }
    transition(c, d.shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    d.shadowState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // 2. Sky + scene into the multisampled target.
    auto sceneRtv = d.rtv(kFrames);
    auto sceneDsv = d.dsv(0);
    const float black[4] = {0, 0, 0, 1};
    c->ClearRenderTargetView(sceneRtv, black, 0, nullptr);
    c->ClearDepthStencilView(sceneDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    c->OMSetRenderTargets(1, &sceneRtv, FALSE, &sceneDsv);
    D3D12_VIEWPORT vp{0, 0, static_cast<float>(d.width), static_cast<float>(d.height), 0, 1};
    D3D12_RECT sc{0, 0, d.width, d.height};
    c->RSSetViewports(1, &vp);
    c->RSSetScissorRects(1, &sc);
    c->SetGraphicsRootDescriptorTable(2, d.srvHeap->GetGPUDescriptorHandleForHeapStart());
    if (haveFrame) {
        c->SetPipelineState(d.psoSky.Get());
        c->DrawInstanced(3, 1, 0, 0);
        c->SetPipelineState(d.wireframe ? d.psoSceneWire.Get() : d.psoScene.Get());
        for (const auto& it : d.items) {
            c->SetGraphicsRootConstantBufferView(1, it.constants);
            c->IASetVertexBuffers(0, 1, &it.mesh->vbv);
            c->IASetIndexBuffer(&it.mesh->ibv);
            c->DrawIndexedInstanced(it.mesh->indexCount, 1, 0, 0, 0);
        }
    }

    // 3. Resolve into the back buffer, then the 2D overlay on top.
    ID3D12Resource* back = d.backBuffers[d.frameIndex].Get();
    if (d.samples > 1) {
        transition(c, d.sceneColour.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        transition(c, back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        c->ResolveSubresource(back, 0, d.sceneColour.Get(), 0, kColourFormat);
        transition(c, back, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(c, d.sceneColour.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    } else {
        transition(c, d.sceneColour.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(c, back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        c->CopyResource(back, d.sceneColour.Get());
        transition(c, back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition(c, d.sceneColour.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    auto backRtv = d.rtv(d.frameIndex);
    c->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    if (!d.overlay.empty()) {
        UINT64 bytes = d.overlay.size() * sizeof(OverlayVertex);
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        if (d.alloc(bytes, 16, cpu, gpu)) {
            std::memcpy(cpu, d.overlay.data(), bytes);
            D3D12_VERTEX_BUFFER_VIEW vbv{gpu, static_cast<UINT>(bytes), sizeof(OverlayVertex)};
            c->SetPipelineState(d.psoOverlay.Get());
            c->IASetVertexBuffers(0, 1, &vbv);
            c->DrawInstanced(static_cast<UINT>(d.overlay.size()), 1, 0, 0);
        }
    }
    bool capture = !d.screenshotPath.empty();
    if (capture) {
        D3D12_RESOURCE_DESC bd = back->GetDesc();
        UINT rows = 0; UINT64 rowBytes = 0, total = 0;
        d.device->GetCopyableFootprints(&bd, 0, 1, 0, &d.readbackFootprint, &rows, &rowBytes, &total);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = total; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.readback.Reset();
        if (SUCCEEDED(d.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                        nullptr, IID_PPV_ARGS(&d.readback)))) {
            transition(c, back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
            src.pResource = back;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource = d.readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = d.readbackFootprint;
            c->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            transition(c, back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        } else {
            capture = false;
            d.screenshotPath.clear();
        }
    }
    transition(c, back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    c->Close();
    ID3D12CommandList* lists[] = {c};
    d.queue->ExecuteCommandLists(1, lists);
    if (capture) { d.waitIdle(); d.saveReadback(); }
    d.swapChain->Present(1, 0); // vsync
    d.frameFence[d.frameIndex] = ++d.fenceCounter;
    d.queue->Signal(d.fence.Get(), d.fenceCounter);
    d.frameIndex = d.swapChain->GetCurrentBackBufferIndex();
    d.frameOpen = false;
}

bool D3D12Renderer::requestScreenshot(const std::string& path) {
    impl_->screenshotPath = path;
    return true;
}
// Writes the read-back RGBA frame as a bottom-up 24-bit BMP.
void D3D12Renderer::Impl::saveReadback() {
    std::string path = screenshotPath;
    screenshotPath.clear();
    UINT8* data = nullptr;
    D3D12_RANGE all{0, static_cast<SIZE_T>(readbackFootprint.Footprint.RowPitch) * readbackFootprint.Footprint.Height};
    if (FAILED(readback->Map(0, &all, reinterpret_cast<void**>(&data)))) return;
    int w = static_cast<int>(readbackFootprint.Footprint.Width), h = static_cast<int>(readbackFootprint.Footprint.Height);
    int rowOut = (w * 3 + 3) & ~3;
    std::vector<UINT8> px(static_cast<size_t>(rowOut) * h, 0);
    for (int y = 0; y < h; ++y) {
        const UINT8* src = data + readbackFootprint.Offset + static_cast<size_t>(y) * readbackFootprint.Footprint.RowPitch;
        UINT8* dst = px.data() + static_cast<size_t>(h - 1 - y) * rowOut;
        for (int x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 0];
        }
    }
    D3D12_RANGE none{0, 0};
    readback->Unmap(0, &none);
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1; ih.biBitCount = 24;
    ih.biCompression = BI_RGB; ih.biSizeImage = static_cast<DWORD>(px.size());
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) { Logger::error("Screenshot: cannot write " + path); return; }
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    Logger::info("Screenshot saved: " + path);
}

void D3D12Renderer::resize(int w, int h) { (void)w; (void)h; } // fixed-size window
size_t D3D12Renderer::drawsThisFrame() const { return impl_->draws; }

std::unique_ptr<IRenderer> createD3D12Renderer(void* h, int w, int hh) {
    return std::make_unique<D3D12Renderer>(h, w, hh);
}
} // namespace gc6
#endif // _WIN32
