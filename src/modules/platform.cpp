#include "modules/platform.hpp"
#include "application/main_loop_signal.hpp"
#include "modules/core.hpp"
#include "domain/json_text.hpp"
#include "domain/market_calendar.hpp"
#include "platform/application_paths.hpp"
#include "platform/windows_path.hpp"
#include "presentation/chart_export.hpp"
#include "presentation/chart_export_jobs.hpp"
#include "presentation/chart_types.hpp"
#include "presentation/gui_renderer_context.hpp"

#include <d3d11.h>
#include "imgui_impl_dx11.h"
#include "imgui_impl_glfw.h"

namespace squarestar::shell {

using squarestar::platform::Win32AppRuntime;
using squarestar::application::RequestGuiRedraw;
using squarestar::presentation::GuiShellRuntime;
using squarestar::application::AppState;
using squarestar::application::UserFeedback;
using squarestar::application::UserFeedbackType;
using squarestar::application::UserFeedbackDestination;
using squarestar::market::StockData;
using squarestar::presentation::ChartVisualType;
using squarestar::presentation::ChartVisualTypeName;
using squarestar::presentation::ChartVisualTypeSlug;
using squarestar::presentation::ChartExportMethod;
using squarestar::presentation::ChartExportFormat;
using squarestar::presentation::DetectChartExportFormat;
using squarestar::presentation::ChartExportFormatAllowed;
using squarestar::presentation::FormatDataTimestampUtc;
using squarestar::presentation::EscapeCsvField;
using squarestar::presentation::EscapeSpreadsheetCsvField;
using squarestar::presentation::NormalizeSpreadsheetTextField;
using squarestar::presentation::EnsureGuiRendererContext;
using squarestar::presentation::ChartExportResult;
using squarestar::presentation::QueueChartExportJob;
using squarestar::presentation::ChartExportJobBusy;
using squarestar::presentation::PollChartExportJob;
using squarestar::text::EscapeJsonStringValue;

// Application paths, font discovery, UTF-8 path conversion, and world-clock
static bool GetImageEncoderClsid(const WCHAR* mimeType, CLSID& clsid) {
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || !bytes)
        return false;
    std::vector<unsigned char> storage(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, mimeType) == 0) {
            clsid = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}
class GdiplusExportRuntime {
  public:
    GdiplusExportRuntime() {
        Gdiplus::GdiplusStartupInput input;
        ready_ = Gdiplus::GdiplusStartup(&token_, &input, nullptr) == Gdiplus::Ok;
        if (ready_) {
            pngReady_ = GetImageEncoderClsid(L"image/png", pngEncoder_);
            jpegReady_ = GetImageEncoderClsid(L"image/jpeg", jpegEncoder_);
        }
    }
    ~GdiplusExportRuntime() {
        if (ready_)
            Gdiplus::GdiplusShutdown(token_);
    }
    GdiplusExportRuntime(const GdiplusExportRuntime&) = delete;
    GdiplusExportRuntime& operator=(const GdiplusExportRuntime&) = delete;
    bool Ready() const {
        return ready_;
    }
    bool Encoder(bool jpeg, CLSID& encoder) const {
        if (jpeg ? !jpegReady_ : !pngReady_)
            return false;
        encoder = jpeg ? jpegEncoder_ : pngEncoder_;
        return true;
    }

  private:
    ULONG_PTR token_ = 0;
    CLSID pngEncoder_{}, jpegEncoder_{};
    bool ready_ = false, pngReady_ = false, jpegReady_ = false;
};
static GdiplusExportRuntime& GetGdiplusExportRuntime() {
    static GdiplusExportRuntime runtime;
    return runtime;
}
static bool CopyGuiCaptureToBitmap(const std::vector<unsigned char>& rgba,
                                   int width,
                                   int height,
                                   Gdiplus::Bitmap& bitmap) {
    if (width <= 0 || height <= 0 ||
        rgba.size() < (size_t)width * (size_t)height * 4)
        return false;
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData bits{};
    if (bitmap.LockBits(&rect,
                        Gdiplus::ImageLockModeWrite,
                        PixelFormat32bppARGB,
                        &bits) != Gdiplus::Ok)
        return false;
    for (int y = 0; y < height; ++y) {
        const unsigned char* src =
            rgba.data() + (size_t)(height - 1 - y) * (size_t)width * 4;
        BYTE* dst = bits.Stride >= 0
                        ? static_cast<BYTE*>(bits.Scan0) + (size_t)y * bits.Stride
                        : static_cast<BYTE*>(bits.Scan0) +
                              (size_t)(height - 1 - y) * (size_t)(-bits.Stride);
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    bitmap.UnlockBits(&bits);
    return true;
}
template <typename Destination>
static bool SaveGdiplusBitmap(Gdiplus::Bitmap& bitmap,
                              Destination destination,
                              const CLSID& encoder,
                              bool jpeg) {
    ULONG quality = 97;
    Gdiplus::EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;
    return bitmap.Save(destination, &encoder, jpeg ? &params : nullptr) == Gdiplus::Ok;
}
static bool EncodeGuiCaptureJpeg(const std::vector<unsigned char>& rgba,
                                 int width,
                                 int height,
                                 std::vector<unsigned char>& jpeg) {
    jpeg.clear();
    GdiplusExportRuntime& runtime = GetGdiplusExportRuntime();
    CLSID encoder{};
    if (!runtime.Ready() || !runtime.Encoder(true, encoder))
        return false;
    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    if (!CopyGuiCaptureToBitmap(rgba, width, height, bitmap))
        return false;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK || !stream)
        return false;
    const bool encoded = SaveGdiplusBitmap(bitmap, stream, encoder, true);
    STATSTG stat{};
    HGLOBAL memory = nullptr;
    bool copied = false;
    if (encoded && stream->Stat(&stat, STATFLAG_NONAME) == S_OK &&
        stat.cbSize.QuadPart > 0 &&
        stat.cbSize.QuadPart <= (ULONGLONG)std::numeric_limits<size_t>::max() &&
        GetHGlobalFromStream(stream, &memory) == S_OK && memory) {
        const size_t size = (size_t)stat.cbSize.QuadPart;
        if (const void* bytes = GlobalLock(memory)) {
            jpeg.assign((const unsigned char*)bytes, (const unsigned char*)bytes + size);
            GlobalUnlock(memory);
            copied = true;
        }
    }
    stream->Release();
    return copied;
}
static bool WriteGuiCapturePdf(const std::string& filename,
                               const std::vector<unsigned char>& rgba,
                               int width,
                               int height) {
    std::vector<unsigned char> jpeg;
    if (!EncodeGuiCaptureJpeg(rgba, width, height, jpeg))
        return false;
    // Keep a conventional landscape presentation page while preserving the
    // capture's exact aspect ratio and full pixel payload.
    const double pageScale =
        std::min(960.0 / std::max(1, width), 540.0 / std::max(1, height));
    const double pageWidth = width * pageScale;
    const double pageHeight = height * pageScale;
    std::ofstream out(squarestar::platform::Utf8FilesystemPath(filename),
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    std::array<std::streamoff, 7> offsets{};
    out << "%PDF-1.7\n%SquareStarGuiCapture\n";
    auto object = [&](int id, const std::string& body) {
        offsets[(size_t)id] = out.tellp();
        out << id << " 0 obj\n" << body << "\nendobj\n";
    };
    object(1, "<< /Type /Catalog /Pages 2 0 R >>");
    object(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    std::ostringstream page;
    page << std::fixed << std::setprecision(3)
         << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << pageWidth << ' '
         << pageHeight << "] /CropBox [0 0 " << pageWidth << ' ' << pageHeight
         << "] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>";
    object(3, page.str());
    std::ostringstream content;
    content << std::fixed << std::setprecision(3) << "q\n" << pageWidth << " 0 0 "
            << pageHeight << " 0 0 cm\n/Im0 Do\nQ\n";
    object(4, "<< /Length " + std::to_string(content.str().size()) + ">>\nstream\n" +
                  content.str() + "endstream");
    offsets[5] = out.tellp();
    out << "5 0 obj\n<< /Type /XObject /Subtype /Image /Width " << width << " /Height "
        << height
        << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length "
        << jpeg.size() << " >>\nstream\n";
    out.write((const char*)jpeg.data(), (std::streamsize)jpeg.size());
    out << "\nendstream\nendobj\n";
    object(6,
           "<< /Title (SquareStar GUI export) /Creator (SquareStar) "
           "/Producer (SquareStar GUI capture PDF) >>");
    const std::streamoff xref = out.tellp();
    out << "xref\n0 7\n0000000000 65535 f \n";
    for (int id = 1; id <= 6; ++id)
        out << std::setw(10) << std::setfill('0') << (long long)offsets[(size_t)id]
            << " 00000 n \n";
    out << "trailer\n<< /Size 7 /Root 1 0 R /Info 6 0 R >>\nstartxref\n"
        << (long long)xref << "\n%%EOF\n";
    return out.good();
}

// Direct3D 11 GUI captures use a render-target texture plus a CPU-readable
// staging texture. The live renderer and export path share one device/context,
// avoiding a second graphics device or any hidden WGL/OpenGL context.
ImVec2 GuiExportFramebufferScale(ImVec2 nativeScale) {
    const float safeNativeX =
        std::isfinite(nativeScale.x) && nativeScale.x > 0.0f ? nativeScale.x : 1.0f;
    const float safeNativeY =
        std::isfinite(nativeScale.y) && nativeScale.y > 0.0f ? nativeScale.y : 1.0f;
    // Export at the GUI's real render density. Do not upscale a lower-density
    // font/icon atlas to a nominal 3840x2160 buffer and present that as "4K".
    // A uniform density keeps the live GUI aspect ratio intact.
    const float density = std::max({1.0f, safeNativeX, safeNativeY});
    return ImVec2(density, density);
}
bool ExportGuiDrawDataImage(const std::string& filename,
                            ImGuiViewport* viewport,
                            ImVec2 screenMin,
                            ImVec2 screenMax,
                            int* outputWidth,
                            int* outputHeight) {
    const ChartExportFormat format = DetectChartExportFormat(filename);
    if (format != ChartExportFormat::Png && format != ChartExportFormat::Jpg &&
        format != ChartExportFormat::Pdf)
        return false;

    ImDrawData* drawData =
        viewport && viewport->DrawData ? viewport->DrawData : ImGui::GetDrawData();
    if (!drawData || !drawData->Valid || drawData->CmdListsCount <= 0 ||
        drawData->DisplaySize.x <= 1.0f || drawData->DisplaySize.y <= 1.0f)
        return false;

    ID3D11Device* device = squarestar::presentation::GuiD3D11Device();
    ID3D11DeviceContext* context = squarestar::presentation::GuiD3D11DeviceContext();
    if (!device || !context)
        return false;

    const ImVec2 displayMin = drawData->DisplayPos;
    const ImVec2 displayMax(displayMin.x + drawData->DisplaySize.x,
                            displayMin.y + drawData->DisplaySize.y);
    screenMin.x = std::clamp(screenMin.x, displayMin.x, displayMax.x);
    screenMin.y = std::clamp(screenMin.y, displayMin.y, displayMax.y);
    screenMax.x = std::clamp(screenMax.x, displayMin.x, displayMax.x);
    screenMax.y = std::clamp(screenMax.y, displayMin.y, displayMax.y);
    const float logicalWidth = screenMax.x - screenMin.x;
    const float logicalHeight = screenMax.y - screenMin.y;
    if (logicalWidth <= 1.0f || logicalHeight <= 1.0f)
        return false;

    const ImVec2 renderScale = GuiExportFramebufferScale(drawData->FramebufferScale);
    const int width = std::max(1, static_cast<int>(logicalWidth * renderScale.x));
    const int height = std::max(1, static_cast<int>(logicalHeight * renderScale.y));
    const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    constexpr uint64_t kMaximumGuiExportPixels = 64ull * 1024ull * 1024ull;
    if (width <= 1 || height <= 1 || pixelCount > kMaximumGuiExportPixels ||
        pixelCount > std::numeric_limits<size_t>::max() / 4 ||
        width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return false;

    D3D11_TEXTURE2D_DESC renderDesc{};
    renderDesc.Width = static_cast<UINT>(width);
    renderDesc.Height = static_cast<UINT>(height);
    renderDesc.MipLevels = 1;
    renderDesc.ArraySize = 1;
    renderDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    renderDesc.SampleDesc.Count = 1;
    renderDesc.Usage = D3D11_USAGE_DEFAULT;
    renderDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* renderTexture = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr;
    auto releaseResources = [&] {
        if (stagingTexture) {
            stagingTexture->Release();
            stagingTexture = nullptr;
        }
        if (renderTarget) {
            renderTarget->Release();
            renderTarget = nullptr;
        }
        if (renderTexture) {
            renderTexture->Release();
            renderTexture = nullptr;
        }
    };

    if (FAILED(device->CreateTexture2D(&renderDesc, nullptr, &renderTexture)) ||
        FAILED(device->CreateRenderTargetView(renderTexture, nullptr, &renderTarget))) {
        releaseResources();
        return false;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = renderDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture))) {
        releaseResources();
        return false;
    }

    context->OMSetRenderTargets(1, &renderTarget, nullptr);
    const ImVec4 opaqueBackground = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float clear[4] = {
        opaqueBackground.x, opaqueBackground.y, opaqueBackground.z, 1.0f};
    context->ClearRenderTargetView(renderTarget, clear);

    const ImVec2 previousDisplayPos = drawData->DisplayPos;
    const ImVec2 previousDisplaySize = drawData->DisplaySize;
    const ImVec2 previousFramebufferScale = drawData->FramebufferScale;
    drawData->DisplayPos = screenMin;
    drawData->DisplaySize = ImVec2(logicalWidth, logicalHeight);
    drawData->FramebufferScale = renderScale;
    ImGui_ImplDX11_RenderDrawData(drawData);
    drawData->DisplayPos = previousDisplayPos;
    drawData->DisplaySize = previousDisplaySize;
    drawData->FramebufferScale = previousFramebufferScale;

    context->CopyResource(stagingTexture, renderTexture);
    // Drop the context's RTV reference before releasing the temporary capture
    // resources. The next live frame will bind the main/viewport target again.
    context->OMSetRenderTargets(0, nullptr, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
        releaseResources();
        return false;
    }

    // Encoders consume a bottom-up RGBA buffer, so flip rows while copying from
    // D3D's top-down staging texture. RowPitch may include driver padding.
    std::vector<unsigned char> rgba(static_cast<size_t>(pixelCount) * 4);
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        const auto* src = static_cast<const unsigned char*>(mapped.pData) +
                          static_cast<size_t>(y) * mapped.RowPitch;
        auto* dst = rgba.data() + static_cast<size_t>(height - 1 - y) * rowBytes;
        std::memcpy(dst, src, rowBytes);
    }
    context->Unmap(stagingTexture, 0);
    releaseResources();

    if (outputWidth)
        *outputWidth = width;
    if (outputHeight)
        *outputHeight = height;

    auto renderSerialization = GuiShellRuntime().SerializeChartExportRendering();
    bool saved = false;
    if (format == ChartExportFormat::Pdf) {
        saved = WriteGuiCapturePdf(filename, rgba, width, height);
    } else {
        GdiplusExportRuntime& runtime = GetGdiplusExportRuntime();
        CLSID encoder{};
        const bool jpeg = format == ChartExportFormat::Jpg;
        if (!runtime.Ready() || !runtime.Encoder(jpeg, encoder))
            return false;
        Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
        if (CopyGuiCaptureToBitmap(rgba, width, height, bitmap)) {
            const std::wstring path = squarestar::platform::Utf8PathToWide(filename);
            saved = SaveGdiplusBitmap(bitmap, path.c_str(), encoder, jpeg);
        }
    }
    return saved;
}
bool IsCleanGuiCaptureFrame() {
    // Export frames rebuild the stock presentation without interactive chrome
    // or transient overlays; normal on-screen frames remain unchanged.
    return GuiShellRuntime().ForceCleanGuiCaptureFrame();
}
static void QueueGuiCapture(const std::string& path,
                            ImGuiViewport* viewport,
                            ImVec2 screenMin,
                            ImVec2 screenMax,
                            float capturePadding) {
    squarestar::presentation::PendingGuiCapture capture;
    capture.active = true;
    capture.cleanFramesRemaining = 1;
    capture.viewportId = viewport ? viewport->ID : 0;
    // Keep padding a caller-owned presentation choice. Chart/VS exports use a
    // small breathing margin, while monitor exports pass zero so a crop that
    // already spans the complete monitor surface cannot reach back into the
    // title bar or native window edge.
    capture.screenMin =
        ImVec2(std::min(screenMin.x, screenMax.x) - capturePadding,
               std::min(screenMin.y, screenMax.y) - capturePadding);
    capture.screenMax =
        ImVec2(std::max(screenMin.x, screenMax.x) + capturePadding,
               std::max(screenMin.y, screenMax.y) + capturePadding);
    capture.path = path;
    GuiShellRuntime().QueueGuiCapture(std::move(capture));
    RequestGuiRedraw();
}
void PumpGuiCapture(GLFWwindow* window,
                           AppState& state,
                           void (*renderCaptureSurface)(GLFWwindow*, AppState&)) {
    if (!GuiShellRuntime().HasPendingGuiCapture())
        return;
    if (GuiShellRuntime().AdvancePendingGuiCaptureCleanFrame()) {
        RequestGuiRedraw();
        return;
    }
    const squarestar::presentation::PendingGuiCapture capture =
        GuiShellRuntime().PendingGuiCaptureSnapshot();
    if (!window || !EnsureGuiRendererContext(window)) {
        PublishBackgroundError(
            state,
            "Render failed",
            "The GUI renderer was not available for the clean export pass.");
        PlayUISound("decline.wav", state);
        GuiShellRuntime().ClearPendingGuiCapture();
        return;
    }

    // Rebuild the stock presentation from the same ImGui/ImPlot code used by
    // the live GUI, but without transient menus, tooltips, or focus veils.
    auto previousGuiRuntime = GuiShellRuntime().Snapshot();
    CloseAllAnimatedFloatingMenus();
    const bool previousObjectFocus = state.config.objectFocus;
    const float previousObjectFocusAnim = state.render.objectFocusAnim;
    state.config.objectFocus = false;
    state.render.objectFocusAnim = 0.0f;
    GuiShellRuntime().ClearObjectFocusFadingRegions();
    GuiShellRuntime().SetCleanGuiCaptureState(true, 1.0f);
    ImGuiIO& captureIo = ImGui::GetIO();
    const ImVec2 previousFramebufferScale = captureIo.DisplayFramebufferScale;
    ImGuiViewport* captureViewport =
        capture.viewportId
            ? ImGui::FindViewportByID(capture.viewportId)
            : ImGui::GetMainViewport();
    const ImVec2 previousViewportFramebufferScale =
        captureViewport ? captureViewport->FramebufferScale : previousFramebufferScale;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    const ImVec2 nativeFramebufferScale = captureIo.DisplayFramebufferScale;
    const ImVec2 exportFramebufferScale = GuiExportFramebufferScale(nativeFramebufferScale);
    const float nativeDensityX =
        std::isfinite(nativeFramebufferScale.x) && nativeFramebufferScale.x > 0.0f
            ? nativeFramebufferScale.x
            : 1.0f;
    GuiShellRuntime().SetCleanGuiCaptureState(
        true, exportFramebufferScale.x / nativeDensityX);
    captureIo.DisplayFramebufferScale = exportFramebufferScale;
    const ImVec2 previousMousePos = captureIo.MousePos;
    std::array<bool, IM_ARRAYSIZE(captureIo.MouseDown)> previousMouseDown{};
    std::copy(std::begin(captureIo.MouseDown),
              std::end(captureIo.MouseDown),
              previousMouseDown.begin());
    const float previousDeltaTime = captureIo.DeltaTime;
    captureIo.DeltaTime = 1.0f / 60.0f;
    const float offscreenPointer = -std::numeric_limits<float>::max();
    captureIo.MousePos = ImVec2(offscreenPointer, offscreenPointer);
    for (bool& down : captureIo.MouseDown)
        down = false;
    ImGui::NewFrame();
    captureViewport = capture.viewportId
                          ? ImGui::FindViewportByID(capture.viewportId)
                          : ImGui::GetMainViewport();
    if (captureViewport)
        captureViewport->FramebufferScale = exportFramebufferScale;
    renderCaptureSurface(window, state);
    ImGui::Render();

    bool saved = false;
    int exportedWidth = 0;
    int exportedHeight = 0;
    try {
        saved = ExportGuiDrawDataImage(capture.path,
                                       captureViewport,
                                       capture.screenMin,
                                       capture.screenMax,
                                       &exportedWidth,
                                       &exportedHeight);
    } catch (...) {
        saved = false;
    }
    GuiShellRuntime().Restore(std::move(previousGuiRuntime));
    captureIo.DisplayFramebufferScale = previousFramebufferScale;
    captureIo.DeltaTime = previousDeltaTime;
    if (captureViewport)
        captureViewport->FramebufferScale = previousViewportFramebufferScale;
    captureIo.MousePos = previousMousePos;
    std::copy(previousMouseDown.begin(),
              previousMouseDown.end(),
              std::begin(captureIo.MouseDown));
    state.config.objectFocus = previousObjectFocus;
    state.render.objectFocusAnim = previousObjectFocusAnim;
    const std::string savedMessage =
        capture.path + " (" + std::to_string(exportedWidth) + " x " +
        std::to_string(exportedHeight) + ")";
    UserFeedback feedback;
    feedback.type = saved ? UserFeedbackType::Success : UserFeedbackType::Error;
    if (UseForegroundNotificationBlocks())
        feedback.destination = UserFeedbackDestination::Foreground;
    feedback.title = saved ? "GUI image saved" : "Render failed";
    feedback.body =
        saved ? savedMessage : "The GUI image export could not be saved.";
    if (saved)
        feedback.duration = std::chrono::seconds(6);
    if (saved) {
        feedback.actionLabel = "Open in folder";
        feedback.actionPath = capture.path;
    }
    PublishUserFeedback(state, std::move(feedback));
    GuiShellRuntime().ClearPendingGuiCapture();
}
ChartExportData MakeChartExportSnapshot(const StockData& source,
                                               const std::string& fallbackCompany) {
    ChartExportData snapshot;
    snapshot.timestamps = source.timestamps;
    snapshot.opens = source.opens;
    snapshot.highs = source.highs;
    snapshot.lows = source.lows;
    snapshot.closes = source.closes;
    snapshot.volumes = source.volumes;
    snapshot.companyName =
        (source.companyName.empty() || source.companyName == "Fetching...") &&
                !fallbackCompany.empty()
            ? fallbackCompany
            : source.companyName;
    return snapshot;
}
MonitorExportData MakeMonitorExportSnapshot(const StockData& source,
                                                   std::string ticker,
                                                   std::string range,
                                                   const std::string& fallbackCompany) {
    MonitorExportData snapshot;
    snapshot.ticker = std::move(ticker);
    snapshot.range = std::move(range);
    snapshot.chart = MakeChartExportSnapshot(source, fallbackCompany);
    return snapshot;
}
template <typename Data>
static bool ExportChartData(const Data& data,
                            const std::string& ticker,
                            const std::string& range,
                            const std::string& filename,
                            ChartVisualType type) {
    const size_t sourceCount = std::min(data.timestamps.size(), data.closes.size());
    std::vector<size_t> validIndices;
    validIndices.reserve(sourceCount);
    for (size_t i = 0; i < sourceCount; ++i) {
        if (std::isfinite(data.timestamps[i]) && data.timestamps[i] > 0.0 &&
            std::isfinite(data.closes[i]))
            validIndices.push_back(i);
    }
    const size_t count = validIndices.size();
    if (count < 2)
        return false;
    const ChartExportFormat format = DetectChartExportFormat(filename);
    if (format != ChartExportFormat::Csv && format != ChartExportFormat::Txt &&
        format != ChartExportFormat::Json)
        return false;
    std::ofstream out(squarestar::platform::Utf8FilesystemPath(filename), std::ios::binary);
    if (!out)
        return false;
    auto seriesValue = [&](const std::vector<double>& values, size_t index, double fallback) {
        return index < values.size() && std::isfinite(values[index]) ? values[index]
                                                                     : fallback;
    };
    auto volumeValue = [&](size_t index) {
        return index < data.volumes.size() && std::isfinite(data.volumes[index])
                   ? data.volumes[index]
                   : 0.0;
    };
    out << std::setprecision(15);
    if (format == ChartExportFormat::Csv) {
        // CSV stays a compact numeric table. Its default filename carries the
        // ticker/range/visual/currency/time-basis context instead of repeating
        // the same metadata on every row. ISO UTC text also prevents spreadsheet
        // apps from shortening the visible timestamp to minute precision.
        out << "timestamp_utc,open,high,low,close,volume\n";
        for (size_t row = 0; row < count; ++row) {
            const size_t i = validIndices[row];
            const double close = data.closes[i];
            out << EscapeCsvField(FormatDataTimestampUtc(data.timestamps[i], false)) << ','
                << seriesValue(data.opens, i, close) << ',' << seriesValue(data.highs, i, close)
                << ',' << seriesValue(data.lows, i, close) << ',' << close << ',' << volumeValue(i)
                << '\n';
        }
    } else if (format == ChartExportFormat::Txt) {
        out << "ticker\t" << NormalizeSpreadsheetTextField(ticker)
            << "\ncompany\t" << NormalizeSpreadsheetTextField(data.companyName)
            << "\nrange\t" << NormalizeSpreadsheetTextField(range)
            << "\nchart_type\t" << NormalizeSpreadsheetTextField(ChartVisualTypeName(type))
            << "\ncurrency\t"
            << "USD\ntime_basis\tUTC\n\n"
            << "timestamp_utc\topen\thigh\tlow\tclose\tvolume\n";
        for (size_t row = 0; row < count; ++row) {
            const size_t i = validIndices[row];
            const double close = data.closes[i];
            out << FormatDataTimestampUtc(data.timestamps[i], true) << '\t'
                << seriesValue(data.opens, i, close) << '\t' << seriesValue(data.highs, i, close)
                << '\t' << seriesValue(data.lows, i, close) << '\t' << close << '\t'
                << volumeValue(i) << '\n';
        }
    } else {
        out << "{\n  \"ticker\": \"" << EscapeJsonStringValue(ticker) << "\",\n  \"company\": \""
            << EscapeJsonStringValue(data.companyName) << "\",\n  \"range\": \""
            << EscapeJsonStringValue(range) << "\",\n  \"chartType\": \""
            << EscapeJsonStringValue(ChartVisualTypeName(type)) << "\",\n  \"currency\": \""
            << "USD"
            << "\",\n  \"timeBasis\": \"UTC\",\n  \"points\": [\n";
        for (size_t row = 0; row < count; ++row) {
            const size_t i = validIndices[row];
            const double close = data.closes[i];
            out << "    {\"timestamp\": \""
                << EscapeJsonStringValue(FormatDataTimestampUtc(data.timestamps[i], false))
                << "\", \"open\": " << seriesValue(data.opens, i, close)
                << ", \"high\": " << seriesValue(data.highs, i, close)
                << ", \"low\": " << seriesValue(data.lows, i, close) << ", \"close\": " << close
                << ", \"volume\": " << volumeValue(i) << '}' << (row + 1 < count ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
    }
    return out.good();
}
static bool ExportMonitorData(const std::vector<MonitorExportData>& data,
                              const std::string& filename) {
    const ChartExportFormat format = DetectChartExportFormat(filename);
    if (format != ChartExportFormat::Csv && format != ChartExportFormat::Txt &&
        format != ChartExportFormat::Json)
        return false;
    std::ofstream out(squarestar::platform::Utf8FilesystemPath(filename), std::ios::binary);
    if (!out)
        return false;

    out << std::setprecision(15);
    if (format == ChartExportFormat::Csv) {
        out << "symbol,company,range,timestamp_utc,open,high,low,close,volume\n";
    } else if (format == ChartExportFormat::Txt) {
        out << "mode\tmonitor\ntime_basis\tUTC\n\n"
            << "symbol\tcompany\trange\ttimestamp_utc\topen\thigh\tlow\tclose\tvolume\n";
    } else {
        out << "{\n  \"mode\": \"monitor\",\n  \"timeBasis\": \"UTC\",\n  \"stocks\": [\n";
    }

    bool wroteAnyPoint = false;
    bool firstJsonStock = true;
    for (const MonitorExportData& item : data) {
        const ChartExportData& chart = item.chart;
        const std::size_t sourceCount = std::min(chart.timestamps.size(), chart.closes.size());
        std::vector<std::size_t> validIndices;
        validIndices.reserve(sourceCount);
        for (std::size_t i = 0; i < sourceCount; ++i) {
            if (std::isfinite(chart.timestamps[i]) && chart.timestamps[i] > 0.0 &&
                std::isfinite(chart.closes[i]))
                validIndices.push_back(i);
        }
        if (validIndices.empty())
            continue;

        auto seriesValue = [&](const std::vector<double>& values,
                               std::size_t index,
                               double fallback) {
            return index < values.size() && std::isfinite(values[index]) ? values[index]
                                                                         : fallback;
        };
        auto volumeValue = [&](std::size_t index) {
            return index < chart.volumes.size() && std::isfinite(chart.volumes[index])
                       ? chart.volumes[index]
                       : 0.0;
        };

        if (format == ChartExportFormat::Json) {
            if (!firstJsonStock)
                out << ",\n";
            firstJsonStock = false;
            out << "    {\"symbol\": \"" << EscapeJsonStringValue(item.ticker)
                << "\", \"company\": \"" << EscapeJsonStringValue(chart.companyName)
                << "\", \"range\": \"" << EscapeJsonStringValue(item.range)
                << "\", \"points\": [\n";
        }

        bool firstJsonPoint = true;
        for (const std::size_t i : validIndices) {
            const double close = chart.closes[i];
            const double open = seriesValue(chart.opens, i, close);
            const double high = seriesValue(chart.highs, i, std::max(open, close));
            const double low = seriesValue(chart.lows, i, std::min(open, close));
            const double volume = volumeValue(i);
            const std::string timestamp =
                FormatDataTimestampUtc(chart.timestamps[i], format == ChartExportFormat::Txt);

            wroteAnyPoint = true;
            if (format == ChartExportFormat::Csv) {
                out << EscapeSpreadsheetCsvField(item.ticker) << ','
                    << EscapeSpreadsheetCsvField(chart.companyName) << ','
                    << EscapeSpreadsheetCsvField(item.range) << ','
                    << EscapeSpreadsheetCsvField(timestamp) << ','
                    << open << ',' << high << ',' << low << ',' << close << ',' << volume << '\n';
            } else if (format == ChartExportFormat::Txt) {
                out << NormalizeSpreadsheetTextField(item.ticker) << '\t'
                    << NormalizeSpreadsheetTextField(chart.companyName) << '\t'
                    << NormalizeSpreadsheetTextField(item.range) << '\t'
                    << NormalizeSpreadsheetTextField(timestamp) << '\t' << open << '\t'
                    << high << '\t' << low << '\t' << close << '\t' << volume << '\n';
            } else {
                if (!firstJsonPoint)
                    out << ",\n";
                firstJsonPoint = false;
                out << "      {\"timestamp\": \"" << EscapeJsonStringValue(timestamp)
                    << "\", \"open\": " << open << ", \"high\": " << high
                    << ", \"low\": " << low << ", \"close\": " << close
                    << ", \"volume\": " << volume << '}';
            }
        }
        if (format == ChartExportFormat::Json)
            out << "\n    ]}";
    }
    if (format == ChartExportFormat::Json)
        out << "\n  ]\n}\n";
    return wroteAnyPoint && out.good();
}

bool QueueChartFileExport(ChartExportData data,
                                 std::string ticker,
                                 std::string range,
                                 std::string path,
                                 ChartVisualType type) {
    const std::string resultPath = path;
    return QueueChartExportJob(
        resultPath,
        [data = std::move(data),
         ticker = std::move(ticker),
         range = std::move(range),
         path = std::move(path),
         type]() mutable {
            return ExportChartData(data, ticker, range, path, type);
        });
}
bool QueueMonitorFileExport(std::vector<MonitorExportData> data,
                            std::string path) {
    const std::string resultPath = path;
    return QueueChartExportJob(
        resultPath,
        [data = std::move(data), path = std::move(path)]() mutable {
            return ExportMonitorData(data, path);
        });
}
static bool ChartExportBusy() {
    return ChartExportJobBusy() || GuiShellRuntime().HasPendingGuiCapture();
}
bool QueueGuiPresentationExport(std::string path,
                                       ImGuiViewport* viewport,
                                       ImVec2 screenMin,
                                       ImVec2 screenMax,
                                       float capturePadding) {
    if (ChartExportBusy())
        return false;
    const ChartExportFormat format = DetectChartExportFormat(path);
    if (format != ChartExportFormat::Png && format != ChartExportFormat::Jpg &&
        format != ChartExportFormat::Pdf)
        return false;
    QueueGuiCapture(path, viewport, screenMin, screenMax, capturePadding);
    return true;
}
void PollChartFileExport(AppState& state) {
    const std::optional<ChartExportResult> completed = PollChartExportJob();
    if (!completed)
        return;
    const ChartExportResult& result = *completed;
    UserFeedback feedback;
    feedback.type = result.saved ? UserFeedbackType::Success : UserFeedbackType::Error;
    if (UseForegroundNotificationBlocks())
        feedback.destination = UserFeedbackDestination::Foreground;
    feedback.title = result.saved ? "Chart exported" : "Export failed";
    feedback.body =
        result.saved ? result.path : "The selected file could not be written.";
    if (result.saved)
        feedback.duration = std::chrono::seconds(6);
    if (result.saved) {
        feedback.actionLabel = "Open in folder";
        feedback.actionPath = result.path;
    }
    PublishUserFeedback(state, std::move(feedback));
    if (!result.saved) {
        MessageBoxA(Win32AppRuntime().MainWindow(),
                    "The selected chart file could not be written.",
                    "Export failed",
                    MB_OK | MB_ICONERROR);
    }
}
void DrawChartExportMenuItems(
                                     const std::function<void(ChartExportMethod)>& exportChart,
                                     const char* dataLabel) {
    if (ImGui::MenuItem("GUI export (PNG/JPEG/PDF)..."))
        exportChart(ChartExportMethod::GuiCapture);
    if (ImGui::MenuItem(dataLabel))
        exportChart(ChartExportMethod::SourceData);
}
struct ChartExportDialogSpec {
    const char* extension = "";
    const char* filters = "";
    const char* invalidFormatMessage = "";
};
static const ChartExportDialogSpec& ChartExportSpec(ChartExportMethod method) {
    static const ChartExportDialogSpec gui{
        ".png",
        "GUI PNG image (*.png)\0*.png\0"
        "GUI JPEG image (*.jpg)\0*.jpg;*.jpeg\0"
        "GUI PDF document (*.pdf)\0*.pdf\0\0",
        "GUI export supports .png, .jpg/.jpeg, and .pdf filenames."};
    static const ChartExportDialogSpec data{
        ".csv",
        "CSV source data (*.csv)\0*.csv\0"
        "Tab-delimited text (*.txt)\0*.txt\0"
        "JSON source data (*.json)\0*.json\0\0",
        "Source-data export supports .csv, .txt, and .json filenames."};
    return method == ChartExportMethod::SourceData ? data : gui;
}
std::string SuggestedChartExportFilename(const std::string& ticker,
                                                const std::string& range,
                                                ChartVisualType type,
                                                const std::string& currency,
                                                ChartExportMethod method,
                                                const char* extension) {
    std::string suggested = ticker + "_" + range + "_" + ChartVisualTypeSlug(type);
    if (method == ChartExportMethod::SourceData) {
        if (!currency.empty())
            suggested += "_" + currency;
        suggested += "_UTC";
    } else {
        suggested += "_chart";
    }
    suggested += extension;
    return suggested;
}
bool ChooseChartExportPathForMethod(HWND owner,
                                           const std::string& ticker,
                                           const std::string& range,
                                           ChartVisualType type,
                                           const std::string& currency,
                                           const std::string& directorySetting,
                                           ChartExportMethod method,
                                           std::string& path) {
    path.clear();
    const ChartExportDialogSpec& spec = ChartExportSpec(method);
    const std::string suggested = SuggestedChartExportFilename(
        ticker, range, type, currency, method, spec.extension);
    const std::string initialDirectory = squarestar::platform::ResolveExportDirectory(directorySetting);
    if (!initialDirectory.empty()) {
        std::error_code directoryError;
        std::filesystem::create_directories(
            squarestar::platform::Utf8FilesystemPath(initialDirectory), directoryError);
    }
    const std::wstring suggestedWide = squarestar::platform::Utf8PathToWide(suggested);
    const std::wstring initialDirectoryWide =
        squarestar::platform::Utf8PathToWide(initialDirectory);
    std::vector<wchar_t> buffer(32768, L'\0');
    std::copy_n(suggestedWide.c_str(),
                std::min(suggestedWide.size(), buffer.size() - 1),
                buffer.data());
    static constexpr wchar_t guiFilters[] =
        L"PNG image (*.png)\0*.png\0JPEG image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
        L"PDF document (*.pdf)\0*.pdf\0\0";
    static constexpr wchar_t dataFilters[] =
        L"CSV source data (*.csv)\0*.csv\0Text source data (*.txt)\0*.txt\0"
        L"JSON source data (*.json)\0*.json\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = method == ChartExportMethod::SourceData ? dataFilters : guiFilters;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = L"Export chart";
    // Leave extension selection to nFilterIndex below. A fixed lpstrDefExt would
    // silently turn a JPEG/PDF or TXT/JSON selection back into PNG/CSV.
    dialog.lpstrDefExt = nullptr;
    dialog.lpstrInitialDir = initialDirectoryWide.empty() ? nullptr : initialDirectoryWide.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.nFilterIndex = 1;
    if (!GetSaveFileNameW(&dialog))
        return false;
    path = squarestar::platform::WidePathToUtf8(buffer.data());
    if (path.empty())
        return false;
    if (DetectChartExportFormat(path) == ChartExportFormat::Unknown) {
        if (method == ChartExportMethod::GuiCapture)
            path += dialog.nFilterIndex == 2 ? ".jpg" : dialog.nFilterIndex == 3 ? ".pdf" : ".png";
        else
            path += dialog.nFilterIndex == 2 ? ".txt" : dialog.nFilterIndex == 3 ? ".json" : ".csv";
    }
    if (ChartExportFormatAllowed(method, DetectChartExportFormat(path)))
        return true;
    MessageBoxA(
        owner, spec.invalidFormatMessage, "Unsupported export format", MB_OK | MB_ICONWARNING);
    path.clear();
    return false;
}
} // namespace squarestar::shell
