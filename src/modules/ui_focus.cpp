#include "modules/ui_focus.hpp"
#include "modules/core.hpp"

namespace squarestar::shell {

using squarestar::presentation::GuiShellRuntime;
using squarestar::presentation::ObjectFocusRegion;
using squarestar::application::AppState;
using squarestar::application::IsLightGuiTheme;

static bool IsValidObjectFocusRegion(const ObjectFocusRegion& region) {
    return region.max.x > region.min.x && region.max.y > region.min.y;
}

static ObjectFocusRegion CurrentWindowObjectFocusRegion() {
    const ImVec2 min = ImGui::GetWindowPos();
    return {min, ImVec2(min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetWindowHeight())};
}

static ObjectFocusRegion LastItemObjectFocusRegion() {
    return {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
}

void BeginObjectFocusFrame() {
    GuiShellRuntime().BeginObjectFocusFrame();
}

void DrawObjectFocusOutline(const AppState& state,
                            ImVec2 min,
                            ImVec2 max,
                            bool focused,
                            int priority) {
    if (!state.ObjectFocusEnabled() || !focused)
        return;
    if (max.x <= min.x || max.y <= min.y)
        return;
    GuiShellRuntime().AddObjectFocusRegion(
        {ImVec2(min.x - 3.0f, min.y - 3.0f),
         ImVec2(max.x + 3.0f, max.y + 3.0f)},
        priority);
}

void DrawObjectFocusRegion(const AppState& state,
                           const ObjectFocusRegion& region,
                           bool focused,
                           int priority) {
    if (IsValidObjectFocusRegion(region))
        DrawObjectFocusOutline(state, region.min, region.max, focused, priority);
}

void DrawLastItemFocusOutline(const AppState& state,
                              bool focused,
                              int priority) {
    DrawObjectFocusRegion(state, LastItemObjectFocusRegion(), focused, priority);
}

void DrawHoveredLastItemFocusOutline(const AppState& state, int priority) {
    DrawObjectFocusRegion(
        state, LastItemObjectFocusRegion(), ImGui::IsItemHovered(), priority);
}

void DrawCurrentWindowFocusOutline(const AppState& state, int priority) {
    ImGuiWindow* window = ImGui::GetCurrentWindowRead();
    if (!window || (window->Flags & ImGuiWindowFlags_NoInputs) != 0)
        return;
    DrawObjectFocusRegion(state, CurrentWindowObjectFocusRegion(), true, priority);
}

void RenderObjectFocusOverlay(AppState& state, ImVec2 viewportMin, ImVec2 viewportMax) {
    const bool popupOpen =
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    // The home watchlist is itself a focusable object; keep its focus mask while open.
    if (popupOpen && !state.navigation.watchlistOpen) {
        state.render.objectFocusAnim = 0.0f;
        GuiShellRuntime().ClearObjectFocusFadingRegions();
        return;
    }
    // Drop focus regions as soon as their source object or window disappears.
    if (GuiShellRuntime().ObjectFocusRegionsEmpty()) {
        state.render.objectFocusAnim = 0.0f;
        GuiShellRuntime().ClearObjectFocusFadingRegions();
        return;
    }
    GuiShellRuntime().PreserveObjectFocusRegionsForFade();
    const float target = state.ObjectFocusEnabled() ? 1.0f : 0.0f;
    if (state.UiAnimationsEnabled()) {
        const float dt = UiFrameDelta();
        state.render.objectFocusAnim += (target - state.render.objectFocusAnim) * (1.0f - std::exp(-18.0f * dt));
    } else {
        state.render.objectFocusAnim = target;
    }
    state.render.objectFocusAnim = std::clamp(state.render.objectFocusAnim, 0.0f, 1.0f);
    if (state.render.objectFocusAnim <= 0.01f) {
        GuiShellRuntime().ClearObjectFocusFadingRegions();
        return;
    }
    const std::vector<ObjectFocusRegion> sourceRegions =
        GuiShellRuntime().ObjectFocusRegionsSnapshot();
    if (sourceRegions.empty())
        return;
    std::vector<ObjectFocusRegion> regions;
    std::vector<float> xs;
    std::vector<float> ys;
    xs.push_back(viewportMin.x);
    xs.push_back(viewportMax.x);
    ys.push_back(viewportMin.y);
    ys.push_back(viewportMax.y);
    const size_t requiredEdges = sourceRegions.size() * 2 + 2;
    regions.reserve(sourceRegions.size());
    xs.reserve(requiredEdges);
    ys.reserve(requiredEdges);
    for (const auto& source : sourceRegions) {
        ObjectFocusRegion region{ImVec2(std::clamp(source.min.x, viewportMin.x, viewportMax.x),
                                        std::clamp(source.min.y, viewportMin.y, viewportMax.y)),
                                 ImVec2(std::clamp(source.max.x, viewportMin.x, viewportMax.x),
                                        std::clamp(source.max.y, viewportMin.y, viewportMax.y))};
        if (!IsValidObjectFocusRegion(region))
            continue;
        regions.push_back(region);
        xs.push_back(region.min.x);
        xs.push_back(region.max.x);
        ys.push_back(region.min.y);
        ys.push_back(region.max.y);
    }
    if (regions.empty())
        return;
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    const float baseAlpha = !IsLightGuiTheme(state.config.themeModeIndex) ? 0.62f : 0.68f;
    const ImVec4 veil = !IsLightGuiTheme(state.config.themeModeIndex)
                            ? ImVec4(0.0f, 0.0f, 0.0f, baseAlpha * state.render.objectFocusAnim)
                            : ImVec4(1.0f, 1.0f, 1.0f, baseAlpha * state.render.objectFocusAnim);
    const ImU32 veilColor = ImGui::ColorConvertFloat4ToU32(veil);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    for (size_t yi = 0; yi + 1 < ys.size(); ++yi) {
        for (size_t xi = 0; xi + 1 < xs.size(); ++xi) {
            const ImVec2 cellMin(xs[xi], ys[yi]);
            const ImVec2 cellMax(xs[xi + 1], ys[yi + 1]);
            const ImVec2 center((cellMin.x + cellMax.x) * 0.5f, (cellMin.y + cellMax.y) * 0.5f);
            const bool focusedCell =
                std::any_of(regions.begin(), regions.end(), [&](const ObjectFocusRegion& r) {
                    return center.x >= r.min.x && center.x <= r.max.x && center.y >= r.min.y &&
                           center.y <= r.max.y;
                });
            if (!focusedCell)
                draw->AddRectFilled(cellMin, cellMax, veilColor);
        }
    }
}

} // namespace squarestar::shell
