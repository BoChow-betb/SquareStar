#include "presentation/gui_renderer_context.hpp"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <string>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_glfw.h"
#include "implot.h"

namespace squarestar::presentation {
namespace {

std::atomic<ImGuiContext*> g_MainImGuiContext{nullptr};
std::atomic<ImPlotContext*> g_MainImPlotContext{nullptr};
std::atomic<void*> g_ExpectedPlatformBackendData{nullptr};
std::atomic<void*> g_ExpectedRendererBackendData{nullptr};

struct D3D11Runtime {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* mainRenderTarget = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    std::string adapterName;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
};

D3D11Runtime g_D3D11;

template <typename T>
void SafeRelease(T*& object) noexcept {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

std::string WideToUtf8(const wchar_t* text) {
    if (!text || !*text)
        return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1)
        return {};
    std::string result(static_cast<size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            0,
                            text,
                            -1,
                            result.data(),
                            required,
                            nullptr,
                            nullptr) <= 0)
        return {};
    result.pop_back();
    return result;
}

bool CreateMainRenderTarget() {
    if (!g_D3D11.device || !g_D3D11.swapChain)
        return false;
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(g_D3D11.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer)
        return false;
    const HRESULT hr =
        g_D3D11.device->CreateRenderTargetView(backBuffer, nullptr, &g_D3D11.mainRenderTarget);
    backBuffer->Release();
    return SUCCEEDED(hr) && g_D3D11.mainRenderTarget;
}

bool ResizeMainRenderTarget(int width, int height) {
    if (!g_D3D11.swapChain || !g_D3D11.context || width <= 0 || height <= 0)
        return false;
    if (g_D3D11.mainRenderTarget && g_D3D11.framebufferWidth == width &&
        g_D3D11.framebufferHeight == height)
        return true;

    g_D3D11.context->OMSetRenderTargets(0, nullptr, nullptr);
    SafeRelease(g_D3D11.mainRenderTarget);
    const HRESULT resize = g_D3D11.swapChain->ResizeBuffers(
        0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(resize))
        return false;
    if (!CreateMainRenderTarget())
        return false;
    g_D3D11.framebufferWidth = width;
    g_D3D11.framebufferHeight = height;
    return true;
}

IDXGIAdapter1* SelectMinimumPowerAdapter() {
    IDXGIFactory1* factory1 = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory1))) || !factory1)
        return nullptr;

    IDXGIFactory6* factory6 = nullptr;
    if (FAILED(factory1->QueryInterface(IID_PPV_ARGS(&factory6))) || !factory6) {
        SafeRelease(factory1);
        return nullptr;
    }
    SafeRelease(factory1);

    IDXGIAdapter1* selected = nullptr;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* candidate = nullptr;
        const HRESULT hr = factory6->EnumAdapterByGpuPreference(
            index,
            DXGI_GPU_PREFERENCE_MINIMUM_POWER,
            IID_PPV_ARGS(&candidate));
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
            break;

        DXGI_ADAPTER_DESC1 desc{};
        if (candidate && SUCCEEDED(candidate->GetDesc1(&desc)) &&
            (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            selected = candidate;
            break;
        }
        SafeRelease(candidate);
    }

    SafeRelease(factory6);
    return selected;
}

bool CreateDeviceAndSwapChain(HWND hwnd,
                              int width,
                              int height,
                              IDXGIAdapter* adapter,
                              D3D_DRIVER_TYPE driverType,
                              bool flipModel) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = static_cast<UINT>(std::max(width, 1));
    desc.BufferDesc.Height = static_cast<UINT>(std::max(height, 1));
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = flipModel ? 2u : 1u;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = flipModel ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL requested[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(adapter,
                                                driverType,
                                                nullptr,
                                                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                                requested,
                                                static_cast<UINT>(std::size(requested)),
                                                D3D11_SDK_VERSION,
                                                &desc,
                                                &g_D3D11.swapChain,
                                                &g_D3D11.device,
                                                &g_D3D11.featureLevel,
                                                &g_D3D11.context);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDeviceAndSwapChain(adapter,
                                            driverType,
                                            nullptr,
                                            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                            requested + 1,
                                            static_cast<UINT>(std::size(requested) - 1),
                                            D3D11_SDK_VERSION,
                                            &desc,
                                            &g_D3D11.swapChain,
                                            &g_D3D11.device,
                                            &g_D3D11.featureLevel,
                                            &g_D3D11.context);
    }
    return SUCCEEDED(hr) && g_D3D11.device && g_D3D11.context && g_D3D11.swapChain;
}

void CaptureAdapterName() {
    g_D3D11.adapterName.clear();
    if (!g_D3D11.device)
        return;
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(g_D3D11.device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) && dxgiDevice &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter) {
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc)))
            g_D3D11.adapterName = WideToUtf8(desc.Description);
    }
    SafeRelease(adapter);
    SafeRelease(dxgiDevice);
}

}

void SetGuiRendererContexts(ImGuiContext* imguiContext, ImPlotContext* implotContext) noexcept {
    g_MainImGuiContext.store(imguiContext, std::memory_order_release);
    g_MainImPlotContext.store(implotContext, std::memory_order_release);
}

void SetGuiRendererBackendData(void* platformBackend, void* rendererBackend) noexcept {
    g_ExpectedPlatformBackendData.store(platformBackend, std::memory_order_release);
    g_ExpectedRendererBackendData.store(rendererBackend, std::memory_order_release);
}

void ClearGuiRendererContextRegistry() noexcept {
    g_ExpectedRendererBackendData.store(nullptr, std::memory_order_release);
    g_ExpectedPlatformBackendData.store(nullptr, std::memory_order_release);
    g_MainImPlotContext.store(nullptr, std::memory_order_release);
    g_MainImGuiContext.store(nullptr, std::memory_order_release);
}

ImGuiContext* MainGuiImGuiContext() noexcept {
    return g_MainImGuiContext.load(std::memory_order_acquire);
}

ImPlotContext* MainGuiImPlotContext() noexcept {
    return g_MainImPlotContext.load(std::memory_order_acquire);
}


bool InitializeGuiD3D11(HWND hwnd,
                        int framebufferWidth,
                        int framebufferHeight,
                        std::string* failureReason) {
    const auto fail = [&](const char* message) {
        if (failureReason)
            *failureReason = message;
        ShutdownGuiD3D11();
        return false;
    };
    if (!hwnd)
        return fail("The native window handle is unavailable for Direct3D 11.");
    if (g_D3D11.device)
        return true;

    const int width = std::max(framebufferWidth, 1);
    const int height = std::max(framebufferHeight, 1);


IDXGIAdapter1* preferredAdapter = SelectMinimumPowerAdapter();
    bool created = false;
    if (preferredAdapter) {
        created = CreateDeviceAndSwapChain(
            hwnd, width, height, preferredAdapter, D3D_DRIVER_TYPE_UNKNOWN, true);
        if (!created) {
            ShutdownGuiD3D11();
            created = CreateDeviceAndSwapChain(
                hwnd, width, height, preferredAdapter, D3D_DRIVER_TYPE_UNKNOWN, false);
        }
    }
    SafeRelease(preferredAdapter);

    if (!created) {
        ShutdownGuiD3D11();
        created = CreateDeviceAndSwapChain(
            hwnd, width, height, nullptr, D3D_DRIVER_TYPE_HARDWARE, true);
    }
    if (!created) {
        ShutdownGuiD3D11();
        created = CreateDeviceAndSwapChain(
            hwnd, width, height, nullptr, D3D_DRIVER_TYPE_HARDWARE, false);
    }
    if (!created) {
        ShutdownGuiD3D11();
        created = CreateDeviceAndSwapChain(
            hwnd, width, height, nullptr, D3D_DRIVER_TYPE_WARP, false);
    }
    if (!created)
        return fail("Direct3D 11 device/swap-chain creation failed.");

    IDXGIFactory* factory = nullptr;
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(g_D3D11.device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) && dxgiDevice &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter &&
        SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) && factory) {
        factory->MakeWindowAssociation(
            hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    }
    SafeRelease(factory);
    SafeRelease(adapter);
    SafeRelease(dxgiDevice);

    if (!CreateMainRenderTarget())
        return fail("Direct3D 11 could not create the main render target.");
    g_D3D11.framebufferWidth = width;
    g_D3D11.framebufferHeight = height;
    CaptureAdapterName();
    if (failureReason)
        failureReason->clear();
    return true;
}

void ShutdownGuiD3D11() noexcept {
    if (g_D3D11.context) {
        g_D3D11.context->OMSetRenderTargets(0, nullptr, nullptr);
        g_D3D11.context->ClearState();
        g_D3D11.context->Flush();
    }
    SafeRelease(g_D3D11.mainRenderTarget);
    SafeRelease(g_D3D11.swapChain);
    SafeRelease(g_D3D11.context);
    SafeRelease(g_D3D11.device);
    g_D3D11.featureLevel = D3D_FEATURE_LEVEL_9_1;
    g_D3D11.adapterName.clear();
    g_D3D11.framebufferWidth = 0;
    g_D3D11.framebufferHeight = 0;
}

bool BeginGuiD3D11MainFrame(int framebufferWidth,
                            int framebufferHeight,
                            const float clearColor[4]) {
    if (!g_D3D11.device || !g_D3D11.context || !g_D3D11.swapChain ||
        framebufferWidth <= 0 || framebufferHeight <= 0)
        return false;
    if (!ResizeMainRenderTarget(framebufferWidth, framebufferHeight))
        return false;
    g_D3D11.context->OMSetRenderTargets(1, &g_D3D11.mainRenderTarget, nullptr);
    if (clearColor)
        g_D3D11.context->ClearRenderTargetView(g_D3D11.mainRenderTarget, clearColor);
    return true;
}

bool PresentGuiD3D11MainFrame(bool vsync) {
    if (!g_D3D11.swapChain)
        return false;
    const HRESULT hr = g_D3D11.swapChain->Present(vsync ? 1u : 0u, 0);
    return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
}

void FlushGuiD3D11() noexcept {
    if (g_D3D11.context)
        g_D3D11.context->Flush();
}

ID3D11Device* GuiD3D11Device() noexcept {
    return g_D3D11.device;
}

ID3D11DeviceContext* GuiD3D11DeviceContext() noexcept {
    return g_D3D11.context;
}

std::string GuiD3D11AdapterName() {
    return g_D3D11.adapterName.empty() ? std::string("unknown") : g_D3D11.adapterName;
}

std::string GuiD3D11FeatureLevelName() {
    switch (g_D3D11.featureLevel) {
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    case D3D_FEATURE_LEVEL_9_3: return "9_3";
    case D3D_FEATURE_LEVEL_9_2: return "9_2";
    default: return "9_1";
    }
}

bool EnsureGuiRendererContext(GLFWwindow* window) {
    if (!window || !GuiD3D11Device() || !GuiD3D11DeviceContext())
        return false;
    ImGuiContext* imguiContext = MainGuiImGuiContext();
    ImPlotContext* implotContext = MainGuiImPlotContext();
    if (!imguiContext || !implotContext)
        return false;
    if (ImGui::GetCurrentContext() != imguiContext)
        ImGui::SetCurrentContext(imguiContext);
    if (ImPlot::GetCurrentContext() != implotContext)
        ImPlot::SetCurrentContext(implotContext);
    if (ImGui::GetCurrentContext() != imguiContext || ImPlot::GetCurrentContext() != implotContext)
        return false;
    ImGuiIO& io = ImGui::GetIO();
    void* expectedPlatform = g_ExpectedPlatformBackendData.load(std::memory_order_acquire);
    void* expectedRenderer = g_ExpectedRendererBackendData.load(std::memory_order_acquire);
    return expectedPlatform && expectedRenderer && io.BackendPlatformUserData == expectedPlatform &&
           io.BackendRendererUserData == expectedRenderer;
}

bool PrimeGuiRendererForStartup(GLFWwindow* window, std::string* failureReason) {
    const auto fail = [&](const char* message) {
        if (failureReason)
            *failureReason = message;
        return false;
    };
    if (!window)
        return fail("The GLFW window is unavailable.");
    if (!EnsureGuiRendererContext(window))
        return fail("The ImGui/ImPlot context or Direct3D backend userdata is invalid.");
    int windowWidth = 0;
    int windowHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (windowWidth <= 0 || windowHeight <= 0)
        return fail("The GLFW window has no logical size.");
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
        return fail("The GLFW window has no drawable framebuffer.");

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGuiIO& primeIo = ImGui::GetIO();
    primeIo.DisplaySize = ImVec2(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    primeIo.DisplayFramebufferScale =
        ImVec2(static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth),
               static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight));
    primeIo.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGuiViewport* primeViewport = ImGui::GetMainViewport();
    const ImVec2 primePos = primeViewport ? primeViewport->Pos : ImVec2(0, 0);
    ImGui::SetNextWindowPos(primePos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(96.0f, 64.0f), ImGuiCond_Always);
#ifdef IMGUI_HAS_VIEWPORT
    if (primeViewport)
        ImGui::SetNextWindowViewport(primeViewport->ID);
#endif
    ImGui::Begin("##RendererStartupPrime",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted("renderer prime");
    ImGui::End();
    ImGui::Render();
#ifdef IMGUI_HAS_VIEWPORT
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        ImGui::UpdatePlatformWindows();
#endif
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->CmdListsCount <= 0 || drawData->TotalVtxCount <= 0 ||
        drawData->TotalIdxCount <= 0)
        return fail("The startup frame produced no ImGui draw geometry.");

    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!BeginGuiD3D11MainFrame(framebufferWidth, framebufferHeight, clear))
        return fail("The Direct3D 11 startup render target is unavailable.");
    ImGui_ImplDX11_RenderDrawData(drawData);
    FlushGuiD3D11();
    if (failureReason)
        failureReason->clear();
    return true;
}

}
