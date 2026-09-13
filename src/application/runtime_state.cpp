#include "application/runtime_state.hpp"

#include <utility>

namespace squarestar::application {

bool ApplicationRuntimeState::QuitRequested() const noexcept {
    return quit_.load(std::memory_order_acquire);
}

void ApplicationRuntimeState::RequestQuit() noexcept {
    quit_.store(true, std::memory_order_release);
}

bool ApplicationRuntimeState::FactoryResetRequested() const noexcept {
    return factoryResetRequested_.load(std::memory_order_acquire);
}

void ApplicationRuntimeState::RequestFactoryReset() noexcept {
    factoryResetRequested_.store(true, std::memory_order_release);
}

void ApplicationRuntimeState::RequestGuiFrameDeltaReset() noexcept {
    resetGuiFrameDelta_.store(true, std::memory_order_release);
}

bool ApplicationRuntimeState::ConsumeGuiFrameDeltaReset() noexcept {
    return resetGuiFrameDelta_.exchange(false, std::memory_order_acq_rel);
}

AppUiMode ApplicationRuntimeState::CurrentUiMode() const noexcept {
    return uiMode_.load(std::memory_order_acquire);
}

void ApplicationRuntimeState::SetUiMode(AppUiMode mode) noexcept {
    uiMode_.store(mode, std::memory_order_release);
}

UiModeRequest ApplicationRuntimeState::RequestedUiMode() const noexcept {
    return uiModeRequest_.load(std::memory_order_acquire);
}

void ApplicationRuntimeState::RequestUiMode(UiModeRequest request) noexcept {
    uiModeRequest_.store(request, std::memory_order_release);
}

void ApplicationRuntimeState::ClearUiModeRequest() noexcept {
    RequestUiMode(UiModeRequest::None);
}

void ApplicationRuntimeState::RequestNotificationStockOpen(std::string ticker) {
    std::lock_guard<std::mutex> lock(notificationStockMutex_);
    pendingNotificationStock_ = std::move(ticker);
}

std::string ApplicationRuntimeState::ConsumeNotificationStockOpen() {
    std::lock_guard<std::mutex> lock(notificationStockMutex_);
    std::string ticker;
    ticker.swap(pendingNotificationStock_);
    return ticker;
}

bool ApplicationRuntimeState::GuiFullscreenSizeOverride() const noexcept {
    return guiFullscreenSizeOverride_.load(std::memory_order_relaxed);
}

void ApplicationRuntimeState::SetGuiFullscreenSizeOverride(bool enabled) noexcept {
    guiFullscreenSizeOverride_.store(enabled, std::memory_order_relaxed);
}

ApplicationRuntimeState& ApplicationRuntime() noexcept {
    static ApplicationRuntimeState runtime;
    return runtime;
}

} // namespace squarestar::application
