#ifdef _WIN32
// Direct3D 12 backend.
//
// What it draws:
//   * every mesh the game submits (procedural city, cars, people), flat-shaded
//     with a directional sun, sky/ground ambient and distance fog;
//   * the ground as a procedural road grid (asphalt, lane markings, pavements,
//     beach and sea) worked out per pixel from the world position;
//   * a 2D overlay (menus, HUD, minimap) on top, using a font atlas rendered
//     with GDI at start-up.
//
// Structure: 2 frames in flight, one fence value per frame, one persistently
// mapped upload ring per frame for constants and overlay vertices, and a
// mesh cache that uploads each MeshData once (keyed by its address; the
// ResourceManager owns meshes for the whole run).
#include "renderer/d3d12/d3d12_renderer.h"
#include "core/logger.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace gc6 {
namespace {
constexpr UINT kFrames = 2;
constexpr UINT64 kRingBytes = 8ull * 1024 * 1024; // per frame
constexpr float kCanvasW = 1280.0f, kCanvasH = 720.0f;
// Font atlas: printable ASCII (32..127) in a 16 x 6 grid of 32 x 64 cells,
// glyphs rendered at a 48 px em. A glyph drawn at "size" px advances 0.55 x size.
constexpr int kCellW = 32, kCellH = 64, kAtlasW = 16 * kCellW, kAtlasH = 6 * kCellH;
constexpr float kEm = 48.0f;

const char* kShaderSource = R"HLSL(
cbuffer Frame : register(b0) {
    float4x4 viewProj;
    float4 camPos;      // xyz camera, w fog density
    float4 sunDir;      // xyz towards the sun, w intensity
    float4 sunColour;   // rgb, w night factor (0 day .. 1 night)
    float4 skyColour;   // rgb horizon / fog colour
};
cbuffer Draw : register(b1) {
    float4x4 model;
    float4 colour;      // rgb albedo, w emissive strength
};
Texture2D fontTex : register(t0);
SamplerState samp : register(s0);

struct V3 { float4 pos : SV_Position; float3 world : TEXCOORD0; };
V3 VSMain(float3 pos : POSITION) {
    V3 o;
    float4 w = mul(model, float4(pos, 1.0));
    o.world = w.xyz;
    o.pos = mul(viewProj, w);
    return o;
}
float band(float v, float lo, float hi) { return step(lo, v) * step(v, hi); }
// Procedural city ground: 14 m roads every 40 m, dashed centre lines,
// pavements, grass inside the blocks, beach and sea along the south edge.
float3 groundColour(float2 p) {
    if (p.y < -196.0) return float3(0.10, 0.28, 0.42);           // sea
    if (p.y < -160.0) return float3(0.74, 0.67, 0.49);           // beach
    float s = 40.0;
    float2 m = abs(p) % s;                                         // 0..40 within a block
    float2 d = min(m, s - m);                                      // distance to the road centre
    bool roadX = d.x < 7.0, roadZ = d.y < 7.0;
    if (roadX || roadZ) {
        float3 c = float3(0.085, 0.09, 0.10);
        bool junction = roadX && roadZ;
        if (!junction) {
            float along = roadX ? p.y : p.x;
            float across = roadX ? d.x : d.y;
            float dash = step(frac(along / 6.0), 0.5);
            c = lerp(c, float3(0.85, 0.72, 0.18), band(across, 0.0, 0.12) * dash); // centre line
            c = lerp(c, float3(0.80, 0.80, 0.78), band(across, 6.3, 6.5));        // edge line
        }
        return c;
    }
    if (d.x < 9.5 || d.y < 9.5) return float3(0.42, 0.42, 0.40);   // pavement
    return float3(0.20, 0.30, 0.15);                               // grass
}
float4 PSMain(V3 i) : SV_Target {
    float3 n = normalize(cross(ddx(i.world), ddy(i.world)));
    float3 toCam = camPos.xyz - i.world;
    if (dot(n, toCam) < 0.0) n = -n;
    float3 albedo = colour.rgb;
    if (i.world.y < 0.05 && n.y > 0.9) albedo = groundColour(i.world.xz);
    float3 L = normalize(float3(sunDir.x, max(sunDir.y, 0.45), sunDir.z));
    float diffuse = saturate(dot(n, L)) * sunDir.w;
    float3 ambient = lerp(float3(0.20, 0.19, 0.17), skyColour.rgb, n.y * 0.5 + 0.5) * 0.55;
    float3 c = albedo * (ambient + sunColour.rgb * diffuse) + albedo * colour.w;
    float dist = length(toCam);
    float fog = 1.0 - exp(-dist * camPos.w);
    c = lerp(c, skyColour.rgb, saturate(fog));
    return float4(pow(saturate(c), 1.0 / 1.25), 1.0);
}

struct V2 { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
V2 VSOverlay(float2 pos : POSITION, float2 uv : TEXCOORD0, float4 col : COLOR0) {
    V2 o;
    o.pos = float4(pos, 0.0, 1.0);
    o.uv = uv;
    o.col = col;
    return o;
}
float4 PSOverlay(V2 i) : SV_Target {
    if (i.uv.x < 0.0) return i.col;
    float a = fontTex.Sample(samp, i.uv).r;
    return float4(i.col.rgb, i.col.a * a);
}
)HLSL";

struct FrameConstants {
    float viewProj[16];
    float camPos[4];
    float sunDir[4];
    float sunColour[4];
    float skyColour[4];
};
struct DrawConstants {
    float model[16];
    float colour[4];
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
void transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
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
} // namespace

struct D3D12Renderer::Impl {
    HWND hwnd = nullptr;
    int width = 1280, height = 720;
    UINT frameIndex = 0;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12Resource> backBuffers[kFrames];
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;
    UINT rtvStride = 0;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12CommandAllocator> allocators[kFrames];
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceCounter = 0;
    UINT64 frameFence[kFrames] = {};
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> psoScene, psoSceneWire, psoOverlay;
    ComPtr<ID3D12Resource> fontTexture;
    // Upload ring: kFrames slices of kRingBytes, persistently mapped.
    ComPtr<ID3D12Resource> ring;
    UINT8* ringCpu = nullptr;
    UINT64 ringOffset = 0;
    bool ringFullWarned = false;
    std::unordered_map<const MeshData*, GpuMesh> meshes;
    std::vector<OverlayVertex> overlay;
    Camera camera;
    Vec3 sunDir{0, 1, 0}, sunColour{1, 1, 1};
    float sunIntensity = 1.0f;
    bool wireframe = false;
    bool frameOpen = false;
    size_t draws = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv(UINT i) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(i) * rtvStride;
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
    GpuMesh* mesh(const MeshData& m) {
        auto it = meshes.find(&m);
        if (it != meshes.end() && it->second.sourceVertices == m.positions.size() &&
            it->second.sourceIndices == m.indices.size())
            return &it->second;
        GpuMesh g;
        UINT64 vbBytes = m.positions.size() * sizeof(float) * 3;
        UINT64 ibBytes = m.indices.size() * sizeof(uint32_t);
        g.vb = makeUploadBuffer(device.Get(), vbBytes);
        g.ib = makeUploadBuffer(device.Get(), ibBytes);
        if (!g.vb || !g.ib) return nullptr;
        void* p = nullptr;
        D3D12_RANGE none{0, 0};
        g.vb->Map(0, &none, &p);
        std::memcpy(p, m.positions.data(), vbBytes);
        g.vb->Unmap(0, nullptr);
        g.ib->Map(0, &none, &p);
        std::memcpy(p, m.indices.data(), ibBytes);
        g.ib->Unmap(0, nullptr);
        g.vbv = {g.vb->GetGPUVirtualAddress(), static_cast<UINT>(vbBytes), sizeof(float) * 3};
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
    Vec3 sky() const {
        float day = clampf((sunIntensity - 0.25f) / 0.75f, 0.0f, 1.0f);
        Vec3 dayCol{0.62f, 0.74f, 0.88f}, nightCol{0.035f, 0.045f, 0.09f};
        return nightCol + (dayCol - nightCol) * day;
    }
    bool createFontAtlas();
    bool createPipelines();
};

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
    range.NumDescriptors = 1;
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
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 3;
    rs.pParameters = params;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &sampler;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        Logger::error("D3D12: root signature serialisation failed");
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSig))))
        return false;

    ComPtr<ID3DBlob> vs, ps, vsO, psO;
    if (!compile("VSMain", "vs_5_0", vs) || !compile("PSMain", "ps_5_0", ps) ||
        !compile("VSOverlay", "vs_5_0", vsO) || !compile("PSOverlay", "ps_5_0", psO))
        return false;

    D3D12_INPUT_ELEMENT_DESC sceneLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {sceneLayout, 1};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // procedural meshes mix windings
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.SampleDesc.Count = 1;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoScene)))) return false;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoSceneWire)))) return false;

    D3D12_INPUT_ELEMENT_DESC overlayLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    pd.VS = {vsO->GetBufferPointer(), vsO->GetBufferSize()};
    pd.PS = {psO->GetBufferPointer(), psO->GetBufferSize()};
    pd.InputLayout = {overlayLayout, 3};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    auto& bl = pd.BlendState.RenderTarget[0];
    bl.BlendEnable = TRUE;
    bl.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    bl.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    bl.BlendOp = D3D12_BLEND_OP_ADD;
    bl.SrcBlendAlpha = D3D12_BLEND_ONE;
    bl.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    bl.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return SUCCEEDED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoOverlay)));
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

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.BufferCount = kFrames;
    sd.Width = static_cast<UINT>(d.width);
    sd.Height = static_cast<UINT>(d.height);
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
    hd.NumDescriptors = kFrames;
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
    hd.NumDescriptors = 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.dsvHeap)))) return false;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.srvHeap)))) return false;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC dd{};
    dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dd.Width = static_cast<UINT64>(d.width);
    dd.Height = static_cast<UINT>(d.height);
    dd.DepthOrArraySize = 1; dd.MipLevels = 1;
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.SampleDesc.Count = 1;
    dd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.0f;
    if (FAILED(d.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd,
                                                 D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                                                 IID_PPV_ARGS(&d.depth))))
        return false;
    d.device->CreateDepthStencilView(d.depth.Get(), nullptr, d.dsvHeap->GetCPUDescriptorHandleForHeapStart());

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
    Logger::info("D3D12 renderer initialised (" + std::to_string(d.width) + "x" + std::to_string(d.height) + ")");
    return true;
}

void D3D12Renderer::beginFrame() {
    auto& d = *impl_;
    if (d.frameOpen) return; // the game and the app may both start the frame
    d.frameOpen = true;
    d.draws = 0;
    d.overlay.clear();
    d.ringOffset = 0;
    if (d.fence->GetCompletedValue() < d.frameFence[d.frameIndex]) {
        d.fence->SetEventOnCompletion(d.frameFence[d.frameIndex], d.fenceEvent);
        WaitForSingleObject(d.fenceEvent, INFINITE);
    }
    d.allocators[d.frameIndex]->Reset();
    d.cmd->Reset(d.allocators[d.frameIndex].Get(), d.wireframe ? d.psoSceneWire.Get() : d.psoScene.Get());
    transition(d.cmd.Get(), d.backBuffers[d.frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
    Vec3 sky = d.sky();
    // Same curve the pixel shader applies, so distant fog meets the clear colour.
    const float clear[4] = {std::pow(sky.x, 0.8f), std::pow(sky.y, 0.8f), std::pow(sky.z, 0.8f), 1.0f};
    auto rtv = d.rtv(d.frameIndex);
    auto dsv = d.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    d.cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    d.cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    d.cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp{0, 0, static_cast<float>(d.width), static_cast<float>(d.height), 0, 1};
    D3D12_RECT sc{0, 0, d.width, d.height};
    d.cmd->RSSetViewports(1, &vp);
    d.cmd->RSSetScissorRects(1, &sc);
    d.cmd->SetGraphicsRootSignature(d.rootSig.Get());
    ID3D12DescriptorHeap* heaps[] = {d.srvHeap.Get()};
    d.cmd->SetDescriptorHeaps(1, heaps);
    d.cmd->SetGraphicsRootDescriptorTable(2, d.srvHeap->GetGPUDescriptorHandleForHeapStart());
    d.cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Frame constants. Depth range 0..1 (Direct3D), right-handed like the game.
    FrameConstants fc{};
    const Camera& c = d.camera;
    Mat4 proj = glm::perspectiveRH_ZO(glm::radians(c.fov), static_cast<float>(d.width) / d.height,
                                      std::max(c.nearPlane, 0.1f), c.farPlane);
    Mat4 vpm = proj * c.view();
    std::memcpy(fc.viewProj, &vpm[0][0], sizeof(fc.viewProj));
    float night = 1.0f - clampf((d.sunIntensity - 0.25f) / 0.75f, 0.0f, 1.0f);
    fc.camPos[0] = c.position.x; fc.camPos[1] = c.position.y; fc.camPos[2] = c.position.z;
    fc.camPos[3] = 0.0022f + night * 0.002f;
    fc.sunDir[0] = d.sunDir.x; fc.sunDir[1] = d.sunDir.y; fc.sunDir[2] = d.sunDir.z;
    fc.sunDir[3] = d.sunIntensity;
    fc.sunColour[0] = d.sunColour.x; fc.sunColour[1] = d.sunColour.y; fc.sunColour[2] = d.sunColour.z;
    fc.sunColour[3] = night;
    fc.skyColour[0] = sky.x; fc.skyColour[1] = sky.y; fc.skyColour[2] = sky.z; fc.skyColour[3] = 1;
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    if (d.alloc(sizeof(fc), 256, cpu, gpu)) {
        std::memcpy(cpu, &fc, sizeof(fc));
        d.cmd->SetGraphicsRootConstantBufferView(0, gpu);
    }
}

void D3D12Renderer::drawMesh(const MeshData& mesh, const Mat4& model, const Vec3& colour) {
    auto& d = *impl_;
    if (!d.frameOpen || mesh.positions.empty() || mesh.indices.empty()) return;
    GpuMesh* g = d.mesh(mesh);
    if (!g) return;
    DrawConstants dc{};
    std::memcpy(dc.model, &model[0][0], sizeof(dc.model));
    // Colours above 1 (lamps, head/tail lights) glow instead of clipping.
    float peak = std::max(colour.x, std::max(colour.y, colour.z));
    float emissive = peak > 1.0f ? peak - 1.0f : 0.0f;
    Vec3 albedo = peak > 1.0f ? colour / peak : colour;
    dc.colour[0] = albedo.x; dc.colour[1] = albedo.y; dc.colour[2] = albedo.z; dc.colour[3] = emissive;
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    if (!d.alloc(sizeof(dc), 256, cpu, gpu)) return;
    std::memcpy(cpu, &dc, sizeof(dc));
    d.cmd->SetGraphicsRootConstantBufferView(1, gpu);
    d.cmd->IASetVertexBuffers(0, 1, &g->vbv);
    d.cmd->IASetIndexBuffer(&g->ibv);
    d.cmd->DrawIndexedInstanced(g->indexCount, 1, 0, 0, 0);
    ++d.draws;
}

void D3D12Renderer::drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) {
    Vec3 c = mat.albedo;
    float glow = std::max(mat.emissive.x, std::max(mat.emissive.y, mat.emissive.z));
    if (glow > 0.0f) c = c * (1.0f + glow);
    drawMesh(mesh, model, c);
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
void D3D12Renderer::setLights(const LightRig&) {}
void D3D12Renderer::setPostSettings(const PostSettings&) {}
void D3D12Renderer::setWireframe(bool on) { impl_->wireframe = on; }

void D3D12Renderer::present() {
    auto& d = *impl_;
    if (!d.frameOpen) beginFrame(); // menus with no 3D pass still get a frame
    if (!d.overlay.empty()) {
        UINT64 bytes = d.overlay.size() * sizeof(OverlayVertex);
        void* cpu = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        if (d.alloc(bytes, 16, cpu, gpu)) {
            std::memcpy(cpu, d.overlay.data(), bytes);
            D3D12_VERTEX_BUFFER_VIEW vbv{gpu, static_cast<UINT>(bytes), sizeof(OverlayVertex)};
            d.cmd->SetPipelineState(d.psoOverlay.Get());
            d.cmd->IASetVertexBuffers(0, 1, &vbv);
            d.cmd->DrawInstanced(static_cast<UINT>(d.overlay.size()), 1, 0, 0);
        }
    }
    transition(d.cmd.Get(), d.backBuffers[d.frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PRESENT);
    d.cmd->Close();
    ID3D12CommandList* lists[] = {d.cmd.Get()};
    d.queue->ExecuteCommandLists(1, lists);
    d.swapChain->Present(1, 0); // vsync
    d.frameFence[d.frameIndex] = ++d.fenceCounter;
    d.queue->Signal(d.fence.Get(), d.fenceCounter);
    d.frameIndex = d.swapChain->GetCurrentBackBufferIndex();
    d.frameOpen = false;
}

void D3D12Renderer::resize(int w, int h) { (void)w; (void)h; } // fixed-size window
size_t D3D12Renderer::drawsThisFrame() const { return impl_->draws; }

std::unique_ptr<IRenderer> createD3D12Renderer(void* h, int w, int hh) {
    return std::make_unique<D3D12Renderer>(h, w, hh);
}
} // namespace gc6
#endif // _WIN32
