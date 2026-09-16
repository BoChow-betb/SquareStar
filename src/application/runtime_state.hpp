#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace squarestar::application {

enum class AppUiMode { Gui, LiteGui };
enum class UiModeRequest { None, Gui, LiteGui };


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

}
