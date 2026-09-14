#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

struct TextureData {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct BackendData {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIFactory* factory = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* vertexConstantBuffer = nullptr;
    ID3D11PixelShader* pixelShaderRgba = nullptr;
    ID3D11PixelShader* pixelShaderAlpha = nullptr;
    ID3D11SamplerState* samplerLinear = nullptr;
    ID3D11SamplerState* samplerNearest = nullptr;
    ID3D11RasterizerState* rasterizerState = nullptr;
    ID3D11BlendState* blendState = nullptr;
    ID3D11DepthStencilState* depthStencilState = nullptr;
    int vertexBufferSize = 5000;
    int indexBufferSize = 10000;
};

struct ViewportData {
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;
};

struct VertexConstantBuffer {
    float mvp[4][4];
};

BackendData* GetBackendData() {
    if (!ImGui::GetCurrentContext())
        return nullptr;
    return static_cast<BackendData*>(ImGui::GetIO().BackendRendererUserData);
}

template <typename T>
void SafeRelease(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

void DestroyTexture(ImTextureData* tex) {
    if (!tex)
        return;
    if (TextureData* backendTexture = static_cast<TextureData*>(tex->BackendUserData)) {
        SafeRelease(backendTexture->view);
        SafeRelease(backendTexture->texture);
        IM_DELETE(backendTexture);
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
    }
    tex->SetStatus(ImTextureStatus_Destroyed);
}

using D3DCompileProc = HRESULT(WINAPI*)(LPCVOID,
                                        SIZE_T,
                                        LPCSTR,
                                        const D3D_SHADER_MACRO*,
                                        ID3DInclude*,
                                        LPCSTR,
                                        LPCSTR,
                                        UINT,
                                        UINT,
                                        ID3DBlob**,
                                        ID3DBlob**);

bool CompileShader(D3DCompileProc compiler,
                   const char* source,
                   const char* target,
                   ID3DBlob** blob) {
    if (!compiler || !source || !target || !blob)
        return false;
    *blob = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr = compiler(source,
                                std::strlen(source),
                                "SquareStar ImGui DX11 shader",
                                nullptr,
                                nullptr,
                                "main",
                                target,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                0,
                                blob,
                                &errors);
    SafeRelease(errors);
    return SUCCEEDED(hr) && *blob;
}

void SetupRenderState(ImDrawData* drawData) {
    BackendData* bd = GetBackendData();
    if (!bd || !drawData)
        return;

    D3D11_VIEWPORT viewport{};
    viewport.Width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    viewport.Height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    bd->context->RSSetViewports(1, &viewport);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (bd->vertexConstantBuffer &&
        bd->context->Map(bd->vertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped) == S_OK) {
        const float left = drawData->DisplayPos.x;
        const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
        const float top = drawData->DisplayPos.y;
        const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
        const float matrix[4][4] = {
            {2.0f / (right - left), 0.0f, 0.0f, 0.0f},
            {0.0f, 2.0f / (top - bottom), 0.0f, 0.0f},
            {0.0f, 0.0f, 0.5f, 0.0f},
            {(right + left) / (left - right),
             (top + bottom) / (bottom - top),
             0.5f,
             1.0f},
        };
        std::memcpy(static_cast<VertexConstantBuffer*>(mapped.pData)->mvp,
                    matrix,
                    sizeof(matrix));
        bd->context->Unmap(bd->vertexConstantBuffer, 0);
    }

    const UINT stride = sizeof(ImDrawVert);
    const UINT offset = 0;
    bd->context->IASetInputLayout(bd->inputLayout);
    bd->context->IASetVertexBuffers(0, 1, &bd->vertexBuffer, &stride, &offset);
    bd->context->IASetIndexBuffer(bd->indexBuffer,
                                  sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT
                                                         : DXGI_FORMAT_R32_UINT,
                                  0);
    bd->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    bd->context->VSSetShader(bd->vertexShader, nullptr, 0);
    bd->context->VSSetConstantBuffers(0, 1, &bd->vertexConstantBuffer);
    bd->context->GSSetShader(nullptr, nullptr, 0);
    bd->context->HSSetShader(nullptr, nullptr, 0);
    bd->context->DSSetShader(nullptr, nullptr, 0);
    bd->context->CSSetShader(nullptr, nullptr, 0);
    bd->context->PSSetSamplers(0, 1, &bd->samplerLinear);

    const float blendFactor[4]{};
    bd->context->OMSetBlendState(bd->blendState, blendFactor, 0xffffffffu);
    bd->context->OMSetDepthStencilState(bd->depthStencilState, 0);
    bd->context->RSSetState(bd->rasterizerState);
}

void DrawCallbackResetRenderState(const ImDrawList*, const ImDrawCmd*) {}
void DrawCallbackSetSamplerLinear(const ImDrawList*, const ImDrawCmd*) {
    if (BackendData* bd = GetBackendData())
        bd->context->PSSetSamplers(0, 1, &bd->samplerLinear);
}
void DrawCallbackSetSamplerNearest(const ImDrawList*, const ImDrawCmd*) {
    if (BackendData* bd = GetBackendData())
        bd->context->PSSetSamplers(0, 1, &bd->samplerNearest);
}

bool CreateRenderTarget(IDXGISwapChain* swapChain, ID3D11RenderTargetView** output) {
    BackendData* bd = GetBackendData();
    if (!bd || !swapChain || !output)
        return false;
    *output = nullptr;
    ID3D11Texture2D* backBuffer = nullptr;
    const HRESULT getBuffer = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(getBuffer) || !backBuffer)
        return false;
    const HRESULT createView = bd->device->CreateRenderTargetView(backBuffer, nullptr, output);
    backBuffer->Release();
    return SUCCEEDED(createView) && *output;
}

bool CreateViewportSwapChain(HWND hwnd, UINT width, UINT height, IDXGISwapChain** output) {
    BackendData* bd = GetBackendData();
    if (!bd || !bd->factory || !hwnd || !output)
        return false;
    *output = nullptr;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // Secondary ImGui windows are short-lived dropdowns/modals. The classic
    // discard model is the most broadly compatible path for these HWND-owned
    // swap chains and matches Dear ImGui's compatibility-first DX11 backend.
    desc.BufferCount = 1;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr = bd->factory->CreateSwapChain(bd->device, &desc, output);
    if (SUCCEEDED(hr))
        bd->factory->MakeWindowAssociation(
            hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    return SUCCEEDED(hr) && *output;
}

void RendererCreateWindow(ImGuiViewport* viewport) {
    if (!viewport)
        return;
    auto* vd = IM_NEW(ViewportData)();
    viewport->RendererUserData = vd;
    HWND hwnd = viewport->PlatformHandleRaw
                    ? static_cast<HWND>(viewport->PlatformHandleRaw)
                    : static_cast<HWND>(viewport->PlatformHandle);
    if (!CreateViewportSwapChain(hwnd,
                                 static_cast<UINT>(viewport->Size.x),
                                 static_cast<UINT>(viewport->Size.y),
                                 &vd->swapChain) ||
        !CreateRenderTarget(vd->swapChain, &vd->renderTarget)) {
        SafeRelease(vd->renderTarget);
        SafeRelease(vd->swapChain);
    }
}

void RendererDestroyWindow(ImGuiViewport* viewport) {
    if (!viewport)
        return;
    if (ViewportData* vd = static_cast<ViewportData*>(viewport->RendererUserData)) {
        SafeRelease(vd->renderTarget);
        SafeRelease(vd->swapChain);
        IM_DELETE(vd);
    }
    viewport->RendererUserData = nullptr;
}

void RendererSetWindowSize(ImGuiViewport* viewport, ImVec2 size) {
    BackendData* bd = GetBackendData();
    auto* vd = viewport ? static_cast<ViewportData*>(viewport->RendererUserData) : nullptr;
    if (!bd || !vd || !vd->swapChain || size.x <= 0.0f || size.y <= 0.0f)
        return;
    SafeRelease(vd->renderTarget);
    bd->context->OMSetRenderTargets(0, nullptr, nullptr);
    if (SUCCEEDED(vd->swapChain->ResizeBuffers(0,
                                               static_cast<UINT>(size.x),
                                               static_cast<UINT>(size.y),
                                               DXGI_FORMAT_UNKNOWN,
                                               0)))
        CreateRenderTarget(vd->swapChain, &vd->renderTarget);
}

void RendererRenderWindow(ImGuiViewport* viewport, void*) {
    BackendData* bd = GetBackendData();
    auto* vd = viewport ? static_cast<ViewportData*>(viewport->RendererUserData) : nullptr;
    if (!bd || !vd || !vd->renderTarget)
        return;
    bd->context->OMSetRenderTargets(1, &vd->renderTarget, nullptr);
    if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear)) {
        const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bd->context->ClearRenderTargetView(vd->renderTarget, clear);
    }
    ImGui_ImplDX11_RenderDrawData(viewport->DrawData);
}

void RendererSwapBuffers(ImGuiViewport* viewport, void*) {
    auto* vd = viewport ? static_cast<ViewportData*>(viewport->RendererUserData) : nullptr;
    if (vd && vd->swapChain)
        vd->swapChain->Present(0, 0);
}

void InitMultiViewportSupport() {
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Renderer_CreateWindow = RendererCreateWindow;
    platformIo.Renderer_DestroyWindow = RendererDestroyWindow;
    platformIo.Renderer_SetWindowSize = RendererSetWindowSize;
    platformIo.Renderer_RenderWindow = RendererRenderWindow;
    platformIo.Renderer_SwapBuffers = RendererSwapBuffers;
}

} // namespace

void ImGui_ImplDX11_UpdateTexture(ImTextureData* tex) {
    BackendData* bd = GetBackendData();
    if (!bd || !tex)
        return;

    if (tex->Status == ImTextureStatus_WantCreate) {
        DestroyTexture(tex);
        auto* backendTexture = IM_NEW(TextureData)();
        backendTexture->format = tex->Format == ImTextureFormat_Alpha8
                                     ? DXGI_FORMAT_R8_UNORM
                                     : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(tex->Width);
        desc.Height = static_cast<UINT>(tex->Height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = backendTexture->format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA subresource{};
        subresource.pSysMem = tex->GetPixels();
        subresource.SysMemPitch = static_cast<UINT>(tex->GetPitch());
        if (FAILED(bd->device->CreateTexture2D(&desc,
                                                &subresource,
                                                &backendTexture->texture)) ||
            FAILED(bd->device->CreateShaderResourceView(backendTexture->texture,
                                                         nullptr,
                                                         &backendTexture->view))) {
            SafeRelease(backendTexture->view);
            SafeRelease(backendTexture->texture);
            IM_DELETE(backendTexture);
            tex->BackendUserData = nullptr;
            tex->SetTexID(ImTextureID_Invalid);
            return;
        }
        tex->BackendUserData = backendTexture;
        tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(backendTexture->view)));
        tex->SetStatus(ImTextureStatus_OK);
        return;
    }

    if (tex->Status == ImTextureStatus_WantUpdates) {
        auto* backendTexture = static_cast<TextureData*>(tex->BackendUserData);
        if (!backendTexture || !backendTexture->texture)
            return;
        for (const ImTextureRect& rect : tex->Updates) {
            D3D11_BOX box{};
            box.left = rect.x;
            box.top = rect.y;
            box.front = 0;
            box.right = static_cast<UINT>(rect.x + rect.w);
            box.bottom = static_cast<UINT>(rect.y + rect.h);
            box.back = 1;
            bd->context->UpdateSubresource(backendTexture->texture,
                                           0,
                                           &box,
                                           tex->GetPixelsAt(rect.x, rect.y),
                                           static_cast<UINT>(tex->GetPitch()),
                                           0);
        }
        tex->SetStatus(ImTextureStatus_OK);
        return;
    }

    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        DestroyTexture(tex);
}

bool ImGui_ImplDX11_CreateDeviceObjects() {
    BackendData* bd = GetBackendData();
    if (!bd || !bd->device)
        return false;
    if (bd->vertexShader)
        return true;

    HMODULE compilerModule = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compilerModule)
        compilerModule = LoadLibraryW(L"d3dcompiler_43.dll");
    if (!compilerModule)
        return false;
    auto compiler = reinterpret_cast<D3DCompileProc>(GetProcAddress(compilerModule, "D3DCompile"));
    if (!compiler) {
        FreeLibrary(compilerModule);
        return false;
    }

    static const char* vertexShaderSource =
        "cbuffer vertexBuffer : register(b0) { float4x4 ProjectionMatrix; };"
        "struct VS_INPUT { float2 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
        "struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
        "PS_INPUT main(VS_INPUT input) { PS_INPUT output;"
        "output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));"
        "output.col = input.col; output.uv = input.uv; return output; }";
    static const char* pixelShaderRgbaSource =
        "struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
        "sampler sampler0; Texture2D texture0;"
        "float4 main(PS_INPUT input) : SV_Target { return input.col * texture0.Sample(sampler0, input.uv); }";
    static const char* pixelShaderAlphaSource =
        "struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };"
        "sampler sampler0; Texture2D texture0;"
        "float4 main(PS_INPUT input) : SV_Target { float a = texture0.Sample(sampler0, input.uv).r;"
        "return input.col * float4(1.f, 1.f, 1.f, a); }";

    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* rgbaBlob = nullptr;
    ID3DBlob* alphaBlob = nullptr;
    bool ok = CompileShader(compiler, vertexShaderSource, "vs_4_0", &vertexBlob) &&
              CompileShader(compiler, pixelShaderRgbaSource, "ps_4_0", &rgbaBlob) &&
              CompileShader(compiler, pixelShaderAlphaSource, "ps_4_0", &alphaBlob);
    if (!ok) {
        SafeRelease(vertexBlob);
        SafeRelease(rgbaBlob);
        SafeRelease(alphaBlob);
        FreeLibrary(compilerModule);
        return false;
    }

    ok = SUCCEEDED(bd->device->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                                   vertexBlob->GetBufferSize(),
                                                   nullptr,
                                                   &bd->vertexShader));
    if (ok) {
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        ok = SUCCEEDED(bd->device->CreateInputLayout(layout,
                                                      3,
                                                      vertexBlob->GetBufferPointer(),
                                                      vertexBlob->GetBufferSize(),
                                                      &bd->inputLayout));
    }
    if (ok)
        ok = SUCCEEDED(bd->device->CreatePixelShader(rgbaBlob->GetBufferPointer(),
                                                      rgbaBlob->GetBufferSize(),
                                                      nullptr,
                                                      &bd->pixelShaderRgba));
    if (ok)
        ok = SUCCEEDED(bd->device->CreatePixelShader(alphaBlob->GetBufferPointer(),
                                                      alphaBlob->GetBufferSize(),
                                                      nullptr,
                                                      &bd->pixelShaderAlpha));
    SafeRelease(vertexBlob);
    SafeRelease(rgbaBlob);
    SafeRelease(alphaBlob);
    FreeLibrary(compilerModule);
    if (!ok) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    D3D11_BUFFER_DESC constantBufferDesc{};
    constantBufferDesc.ByteWidth = sizeof(VertexConstantBuffer);
    constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(bd->device->CreateBuffer(&constantBufferDesc,
                                         nullptr,
                                         &bd->vertexConstantBuffer))) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(bd->device->CreateBlendState(&blendDesc, &bd->blendState))) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.ScissorEnable = TRUE;
    rasterizerDesc.DepthClipEnable = TRUE;
    if (FAILED(bd->device->CreateRasterizerState(&rasterizerDesc, &bd->rasterizerState))) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = FALSE;
    depthDesc.StencilEnable = FALSE;
    if (FAILED(bd->device->CreateDepthStencilState(&depthDesc, &bd->depthStencilState))) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = 0.0f;
    if (FAILED(bd->device->CreateSamplerState(&samplerDesc, &bd->samplerLinear))) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (FAILED(bd->device->CreateSamplerState(&samplerDesc, &bd->samplerNearest))) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    return true;
}

void ImGui_ImplDX11_InvalidateDeviceObjects() {
    BackendData* bd = GetBackendData();
    if (!bd)
        return;

    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex && tex->BackendUserData)
            DestroyTexture(tex);
    }
    SafeRelease(bd->samplerLinear);
    SafeRelease(bd->samplerNearest);
    SafeRelease(bd->indexBuffer);
    SafeRelease(bd->vertexBuffer);
    SafeRelease(bd->blendState);
    SafeRelease(bd->depthStencilState);
    SafeRelease(bd->rasterizerState);
    SafeRelease(bd->pixelShaderRgba);
    SafeRelease(bd->pixelShaderAlpha);
    SafeRelease(bd->vertexConstantBuffer);
    SafeRelease(bd->inputLayout);
    SafeRelease(bd->vertexShader);
}

void ImGui_ImplDX11_CompactBufferMemory() {
    BackendData* bd = GetBackendData();
    if (!bd)
        return;
    // The immediate context can retain references to currently bound buffers.
    // Unbind first so Release() actually drops the transient high-water storage.
    ID3D11Buffer* nullVertexBuffer = nullptr;
    const UINT zero = 0;
    bd->context->IASetVertexBuffers(0, 1, &nullVertexBuffer, &zero, &zero);
    bd->context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
    SafeRelease(bd->vertexBuffer);
    SafeRelease(bd->indexBuffer);
    bd->vertexBufferSize = 5000;
    bd->indexBufferSize = 10000;
    bd->context->Flush();
}

bool ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    if (!device || !deviceContext || io.BackendRendererUserData)
        return false;

    auto* bd = IM_NEW(BackendData)();
    io.BackendRendererUserData = bd;
    io.BackendRendererName = "squarestar_imgui_impl_dx11";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
#ifdef IMGUI_HAS_VIEWPORT
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
#endif

    bd->device = device;
    bd->context = deviceContext;
    bd->device->AddRef();
    bd->context->AddRef();

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) && dxgiDevice &&
        SUCCEEDED(dxgiDevice->GetParent(IID_PPV_ARGS(&adapter))) && adapter)
        adapter->GetParent(IID_PPV_ARGS(&bd->factory));
    SafeRelease(adapter);
    SafeRelease(dxgiDevice);
    if (!bd->factory) {
        ImGui_ImplDX11_Shutdown();
        return false;
    }

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Renderer_TextureMaxWidth = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    platformIo.Renderer_TextureMaxHeight = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    platformIo.DrawCallback_ResetRenderState = DrawCallbackResetRenderState;
    platformIo.DrawCallback_SetSamplerLinear = DrawCallbackSetSamplerLinear;
    platformIo.DrawCallback_SetSamplerNearest = DrawCallbackSetSamplerNearest;
#ifdef IMGUI_HAS_VIEWPORT
    InitMultiViewportSupport();
#endif
    return true;
}

void ImGui_ImplDX11_Shutdown() {
    BackendData* bd = GetBackendData();
    if (!bd)
        return;
#ifdef IMGUI_HAS_VIEWPORT
    ImGui::DestroyPlatformWindows();
#endif
    ImGui_ImplDX11_InvalidateDeviceObjects();
    SafeRelease(bd->factory);
    SafeRelease(bd->context);
    SafeRelease(bd->device);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset |
                         ImGuiBackendFlags_RendererHasTextures |
                         ImGuiBackendFlags_RendererHasViewports);
    ImGui::GetPlatformIO().ClearRendererHandlers();
    IM_DELETE(bd);
}

void ImGui_ImplDX11_NewFrame() {
    BackendData* bd = GetBackendData();
    IM_ASSERT(bd != nullptr && "ImGui DX11 backend was not initialized");
    if (bd && !bd->vertexShader) {
        const bool created = ImGui_ImplDX11_CreateDeviceObjects();
        IM_ASSERT(created && "ImGui_ImplDX11_CreateDeviceObjects() failed");
        (void)created;
    }
}

void ImGui_ImplDX11_RenderDrawData(ImDrawData* drawData) {
    BackendData* bd = GetBackendData();
    if (!bd || !drawData || drawData->DisplaySize.x <= 0.0f ||
        drawData->DisplaySize.y <= 0.0f)
        return;

    if (drawData->Textures) {
        for (ImTextureData* tex : *drawData->Textures) {
            if (tex && tex->Status != ImTextureStatus_OK)
                ImGui_ImplDX11_UpdateTexture(tex);
        }
    }

    if (!bd->vertexBuffer || bd->vertexBufferSize < drawData->TotalVtxCount) {
        SafeRelease(bd->vertexBuffer);
        bd->vertexBufferSize = drawData->TotalVtxCount + 5000;
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = static_cast<UINT>(bd->vertexBufferSize * sizeof(ImDrawVert));
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(bd->device->CreateBuffer(&desc, nullptr, &bd->vertexBuffer)))
            return;
    }
    if (!bd->indexBuffer || bd->indexBufferSize < drawData->TotalIdxCount) {
        SafeRelease(bd->indexBuffer);
        bd->indexBufferSize = drawData->TotalIdxCount + 10000;
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = static_cast<UINT>(bd->indexBufferSize * sizeof(ImDrawIdx));
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(bd->device->CreateBuffer(&desc, nullptr, &bd->indexBuffer)))
            return;
    }

    D3D11_MAPPED_SUBRESOURCE vertexResource{};
    D3D11_MAPPED_SUBRESOURCE indexResource{};
    if (bd->context->Map(bd->vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &vertexResource) != S_OK)
        return;
    if (bd->context->Map(bd->indexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &indexResource) != S_OK) {
        bd->context->Unmap(bd->vertexBuffer, 0);
        return;
    }
    auto* vertexDestination = static_cast<ImDrawVert*>(vertexResource.pData);
    auto* indexDestination = static_cast<ImDrawIdx*>(indexResource.pData);
    for (const ImDrawList* list : drawData->CmdLists) {
        std::memcpy(vertexDestination,
                    list->VtxBuffer.Data,
                    static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        std::memcpy(indexDestination,
                    list->IdxBuffer.Data,
                    static_cast<size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
        vertexDestination += list->VtxBuffer.Size;
        indexDestination += list->IdxBuffer.Size;
    }
    bd->context->Unmap(bd->vertexBuffer, 0);
    bd->context->Unmap(bd->indexBuffer, 0);

    SetupRenderState(drawData);
    ImGui::GetPlatformIO().Renderer_RenderState = bd;

    int globalIndexOffset = 0;
    int globalVertexOffset = 0;
    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback == DrawCallbackResetRenderState)
                    SetupRenderState(drawData);
                else
                    command.UserCallback(list, &command);
                continue;
            }

            const ImVec2 clipMin((command.ClipRect.x - clipOffset.x) * clipScale.x,
                                 (command.ClipRect.y - clipOffset.y) * clipScale.y);
            const ImVec2 clipMax((command.ClipRect.z - clipOffset.x) * clipScale.x,
                                 (command.ClipRect.w - clipOffset.y) * clipScale.y);
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
                continue;
            D3D11_RECT scissor{static_cast<LONG>(clipMin.x),
                               static_cast<LONG>(clipMin.y),
                               static_cast<LONG>(clipMax.x),
                               static_cast<LONG>(clipMax.y)};
            bd->context->RSSetScissorRects(1, &scissor);

            auto* textureView = reinterpret_cast<ID3D11ShaderResourceView*>(
                static_cast<intptr_t>(command.GetTexID()));
            ID3D11PixelShader* pixelShader = bd->pixelShaderRgba;
            if (textureView) {
                D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
                textureView->GetDesc(&viewDesc);
                if (viewDesc.Format == DXGI_FORMAT_R8_UNORM)
                    pixelShader = bd->pixelShaderAlpha;
            }
            bd->context->PSSetShader(pixelShader, nullptr, 0);
            bd->context->PSSetShaderResources(0, 1, &textureView);
            bd->context->DrawIndexed(command.ElemCount,
                                     command.IdxOffset + globalIndexOffset,
                                     command.VtxOffset + globalVertexOffset);
        }
        globalIndexOffset += list->IdxBuffer.Size;
        globalVertexOffset += list->VtxBuffer.Size;
    }
    // Do not keep the last texture alive solely because it remains bound on
    // the immediate context. This also makes font-atlas rebuild/destruction
    // release its old SRV promptly.
    ID3D11ShaderResourceView* nullTextureView = nullptr;
    bd->context->PSSetShaderResources(0, 1, &nullTextureView);
    ImGui::GetPlatformIO().Renderer_RenderState = nullptr;
}

#endif // IMGUI_DISABLE
