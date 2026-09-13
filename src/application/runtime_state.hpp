#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace squarestar::application {

enum class AppUiMode { Gui, LiteGui };
enum class UiModeRequest { None, Gui, LiteGui };

// Process-lifetime application coordination. Storage stays private so callers
// express requests and observations without choosing their own atomic protocol.
class ApplicationRuntimeState final {
  public:
    [[nodiscard]] bool QuitRequested() const noexcept;
    void RequestQuit() noexcept;

    [[nodiscard]] bool FactoryResetRequested() const noexcept;
    void RequestFactoryReset() noexcept;

    void RequestGuiFrameDeltaReset() noexcept;
    [[nodiscard]] bool ConsumeGuiFrameDeltaReset() noexcept;

    [[nodiscard]] AppUiMode CurrentUiMode() const noexcept;
    void SetUiMode(AppUiMode mode) noexcept;
    [[nodiscard]] UiModeRequest RequestedUiMode() const noexcept;
    void RequestUiMode(UiModeRequest request) noexcept;
    void ClearUiModeRequest() noexcept;

    // Native Win32 notification cards can be clicked while the GUI thread is
    // sleeping. Hand the requested ticker back to the main thread instead of
    // mutating application navigation from the platform window procedure.
    void RequestNotificationStockOpen(std::string ticker);
    [[nodiscard]] std::string ConsumeNotificationStockOpen();

    [[nodiscard]] bool GuiFullscreenSizeOverride() const noexcept;
    void SetGuiFullscreenSizeOverride(bool enabled) noexcept;

  private:
    std::atomic_bool quit_{false};
    std::atomic_bool factoryResetRequested_{false};
    std::atomic_bool resetGuiFrameDelta_{false};
    std::atomic<AppUiMode> uiMode_{AppUiMode::Gui};
    std::atomic<UiModeRequest> uiModeRequest_{UiModeRequest::None};
    std::mutex notificationStockMutex_;
    std::string pendingNotificationStock_;

    std::atomic_bool guiFullscreenSizeOverride_{false};
};

ApplicationRuntimeState& ApplicationRuntime() noexcept;

} // namespace squarestar::application
