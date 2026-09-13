#include "presentation/gui_shell_runtime_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace squarestar::presentation {

AnimatedFloatingMenuFrame GuiShellRuntimeState::UpdateAnimatedFloatingMenu(
    ImGuiID id,
    bool toggle,
    bool escapePressed,
    int frame,
    bool animationsEnabled,
    float deltaTime) {
    AnimatedFloatingMenuState& menu = animatedFloatingMenus_[id];
    if (menu.generation != animatedMenuGeneration_) {
        menu = {};
        menu.generation = animatedMenuGeneration_;
    }
    currentMenuId_ = id;
    bool needsRedraw = false;
    if (toggle) {
        const bool opening = !menu.open;
        if (opening) {
            for (auto& [otherId, otherMenu] : animatedFloatingMenus_)
                if (otherId != id)
                    otherMenu.open = false;
        }
        menu.open = opening;
        if (menu.open)
            menu.ignoreFrame = frame;
        needsRedraw = true;
    }
    if (menu.open && escapePressed) {
        menu.open = false;
        needsRedraw = true;
    }
    if (animationsEnabled) {
        const float target = menu.open ? 1.0f : 0.0f;
        menu.animation +=
            (target - menu.animation) * (1.0f - std::exp(-18.0f * deltaTime));
        if (std::abs(target - menu.animation) > 0.001f)
            needsRedraw = true;
    } else {
        menu.animation = menu.open ? 1.0f : 0.0f;
    }
    menu.animation = std::clamp(menu.animation, 0.0f, 1.0f);
    if (!menu.open && menu.animation <= 0.01f) {
        currentMenuId_ = 0;
        animatedFloatingMenus_.erase(id);
        return {false, false, needsRedraw, 0.0f, 0};
    }
    return {true, menu.open, needsRedraw, menu.animation, menu.ignoreFrame};
}

bool GuiShellRuntimeState::DismissAnimatedFloatingMenu(ImGuiID id, bool instant) {
    const auto found = animatedFloatingMenus_.find(id);
    if (found == animatedFloatingMenus_.end())
        return false;
    found->second.open = false;
    if (instant)
        found->second.animation = 0.0f;
    return true;
}

bool GuiShellRuntimeState::DismissCurrentAnimatedFloatingMenu(bool instant) {
    return currentMenuId_ != 0 && DismissAnimatedFloatingMenu(currentMenuId_, instant);
}

void GuiShellRuntimeState::CloseAllAnimatedFloatingMenus() noexcept {
    animatedMenuGeneration_ = animatedMenuGeneration_ == std::numeric_limits<int>::max()
                                  ? 1
                                  : animatedMenuGeneration_ + 1;
    animatedFloatingMenus_.clear();
    currentMenuId_ = 0;
}

void GuiShellRuntimeState::EndAnimatedFloatingMenu() noexcept {
    currentMenuId_ = 0;
}

void GuiShellRuntimeState::BeginObjectFocusFrame() {
    objectFocusFrame_.regions.clear();
    objectFocusFrame_.priority = -1;
}

void GuiShellRuntimeState::AddObjectFocusRegion(ObjectFocusRegion region, int priority) {
    if (priority < objectFocusFrame_.priority)
        return;
    if (priority > objectFocusFrame_.priority) {
        objectFocusFrame_.regions.clear();
        objectFocusFrame_.priority = priority;
    }
    objectFocusFrame_.regions.push_back(region);
}

bool GuiShellRuntimeState::ObjectFocusRegionsEmpty() const noexcept {
    return objectFocusFrame_.regions.empty();
}

std::vector<ObjectFocusRegion> GuiShellRuntimeState::ObjectFocusRegionsSnapshot() const {
    return objectFocusFrame_.regions;
}

void GuiShellRuntimeState::PreserveObjectFocusRegionsForFade() {
    objectFocusFrame_.fadingRegions = objectFocusFrame_.regions;
}

void GuiShellRuntimeState::ClearObjectFocusFadingRegions() {
    objectFocusFrame_.fadingRegions.clear();
}

void GuiShellRuntimeState::QueueGuiCapture(PendingGuiCapture capture) {
    pendingGuiCapture_ = std::move(capture);
}

bool GuiShellRuntimeState::HasPendingGuiCapture() const noexcept {
    return pendingGuiCapture_.active;
}

bool GuiShellRuntimeState::AdvancePendingGuiCaptureCleanFrame() noexcept {
    return pendingGuiCapture_.active && pendingGuiCapture_.cleanFramesRemaining-- > 0;
}

PendingGuiCapture GuiShellRuntimeState::PendingGuiCaptureSnapshot() const {
    return pendingGuiCapture_;
}

void GuiShellRuntimeState::ClearPendingGuiCapture() noexcept {
    pendingGuiCapture_ = {};
}

void GuiShellRuntimeState::SetCleanGuiCaptureState(bool forceClean, float scale) noexcept {
    forceCleanGuiCaptureFrame_ = forceClean;
    cleanGuiCaptureScale_ = scale;
}

bool GuiShellRuntimeState::ForceCleanGuiCaptureFrame() const noexcept {
    return forceCleanGuiCaptureFrame_;
}

float GuiShellRuntimeState::CleanGuiCaptureScale() const noexcept {
    return cleanGuiCaptureScale_;
}

GuiShellRuntimeState::ChartExportSerialization
GuiShellRuntimeState::SerializeChartExportRendering() {
    return ChartExportSerialization(chartExportRenderMutex_);
}

int GuiShellRuntimeState::SettingsPage() const noexcept {
    return settingsPage_;
}

void GuiShellRuntimeState::SetSettingsPage(int page) noexcept {
    settingsPage_ = page;
}

GuiShellRuntimeSnapshot GuiShellRuntimeState::Snapshot() const {
    return {animatedFloatingMenus_,
            currentMenuId_,
            animatedMenuGeneration_,
            objectFocusFrame_,
            forceCleanGuiCaptureFrame_,
            cleanGuiCaptureScale_,
            settingsPage_};
}

void GuiShellRuntimeState::Restore(GuiShellRuntimeSnapshot snapshot) noexcept {
    animatedFloatingMenus_ = std::move(snapshot.animatedFloatingMenus);
    currentMenuId_ = snapshot.currentMenuId;
    animatedMenuGeneration_ = snapshot.animatedMenuGeneration;
    objectFocusFrame_ = std::move(snapshot.objectFocusFrame);
    forceCleanGuiCaptureFrame_ = snapshot.forceCleanGuiCaptureFrame;
    cleanGuiCaptureScale_ = snapshot.cleanGuiCaptureScale;
    settingsPage_ = snapshot.settingsPage;
}

GuiShellRuntimeState& GuiShellRuntime() noexcept {
    static GuiShellRuntimeState state;
    return state;
}

} // namespace squarestar::presentation
