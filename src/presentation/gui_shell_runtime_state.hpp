#pragma once

#include "imgui.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace squarestar::presentation {

struct AnimatedFloatingMenuState {
    bool open = false;
    float animation = 0.0f;
    int ignoreFrame = 0;
    int generation = 0;
};

struct AnimatedFloatingMenuFrame {
    bool visible = false;
    bool open = false;
    bool needsRedraw = false;
    float animation = 0.0f;
    int ignoreFrame = 0;
};

struct ObjectFocusRegion {
    ImVec2 min = ImVec2(0.0f, 0.0f);
    ImVec2 max = ImVec2(0.0f, 0.0f);
};

struct ObjectFocusFrameState {
    std::vector<ObjectFocusRegion> regions;
    std::vector<ObjectFocusRegion> fadingRegions;
    int priority = -1;
};

struct PendingGuiCapture {
    bool active = false;
    int cleanFramesRemaining = 0;
    ImGuiID viewportId = 0;
    ImVec2 screenMin{};
    ImVec2 screenMax{};
    std::string path;
};

struct GuiShellRuntimeSnapshot {
    std::unordered_map<ImGuiID, AnimatedFloatingMenuState> animatedFloatingMenus;
    ImGuiID currentMenuId = 0;
    int animatedMenuGeneration = 1;
    ObjectFocusFrameState objectFocusFrame;
    bool forceCleanGuiCaptureFrame = false;
    float cleanGuiCaptureScale = 1.0f;
    int settingsPage = 0;
};


class GuiShellRuntimeState final {
  public:
    class ChartExportSerialization final {
      public:
        ChartExportSerialization(ChartExportSerialization&&) noexcept = default;
        ChartExportSerialization& operator=(ChartExportSerialization&&) noexcept = default;
        ChartExportSerialization(const ChartExportSerialization&) = delete;
        ChartExportSerialization& operator=(const ChartExportSerialization&) = delete;

      private:
        friend class GuiShellRuntimeState;
        explicit ChartExportSerialization(std::mutex& mutex) : lock_(mutex) {}
        std::unique_lock<std::mutex> lock_;
    };

    AnimatedFloatingMenuFrame UpdateAnimatedFloatingMenu(ImGuiID id,
                                                          bool toggle,
                                                          bool escapePressed,
                                                          int frame,
                                                          bool animationsEnabled,
                                                          float deltaTime);
    [[nodiscard]] bool DismissAnimatedFloatingMenu(ImGuiID id, bool instant);
    [[nodiscard]] bool DismissCurrentAnimatedFloatingMenu(bool instant);
    void CloseAllAnimatedFloatingMenus() noexcept;
    void EndAnimatedFloatingMenu() noexcept;

    void BeginObjectFocusFrame();
    void AddObjectFocusRegion(ObjectFocusRegion region, int priority);
    [[nodiscard]] bool ObjectFocusRegionsEmpty() const noexcept;
    [[nodiscard]] std::vector<ObjectFocusRegion> ObjectFocusRegionsSnapshot() const;
    void PreserveObjectFocusRegionsForFade();
    void ClearObjectFocusFadingRegions();

    void QueueGuiCapture(PendingGuiCapture capture);
    [[nodiscard]] bool HasPendingGuiCapture() const noexcept;
    [[nodiscard]] bool AdvancePendingGuiCaptureCleanFrame() noexcept;
    [[nodiscard]] PendingGuiCapture PendingGuiCaptureSnapshot() const;
    void ClearPendingGuiCapture() noexcept;

    void SetCleanGuiCaptureState(bool forceClean, float scale) noexcept;
    [[nodiscard]] bool ForceCleanGuiCaptureFrame() const noexcept;
    [[nodiscard]] float CleanGuiCaptureScale() const noexcept;

    [[nodiscard]] ChartExportSerialization SerializeChartExportRendering();

    [[nodiscard]] int SettingsPage() const noexcept;
    void SetSettingsPage(int page) noexcept;

    [[nodiscard]] GuiShellRuntimeSnapshot Snapshot() const;
    void Restore(GuiShellRuntimeSnapshot snapshot) noexcept;

  private:
    std::unordered_map<ImGuiID, AnimatedFloatingMenuState> animatedFloatingMenus_;
    ImGuiID currentMenuId_ = 0;
    int animatedMenuGeneration_ = 1;
    ObjectFocusFrameState objectFocusFrame_;
    std::mutex chartExportRenderMutex_;
    PendingGuiCapture pendingGuiCapture_;
    bool forceCleanGuiCaptureFrame_ = false;
    float cleanGuiCaptureScale_ = 1.0f;
    int settingsPage_ = 0;
};

GuiShellRuntimeState& GuiShellRuntime() noexcept;

}
