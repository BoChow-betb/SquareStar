#include "modules/core.hpp"
#include "application/runtime_state.hpp"
#include "services/http_client.hpp"

#include "services/config_save_queue.hpp"
#include "application/alert_service.hpp"
#include "application/app_config.hpp"
#include "application/foreground_notification_policy.hpp"
#include "application/navigation_state.hpp"
#include "application/notification_channel.hpp"
#include "application/notification_text.hpp"
#include "domain/market_calendar.hpp"
#include "domain/market_runtime.hpp"
#include "platform/audio_runtime.hpp"
#include "platform/glfw_runtime.hpp"
#include "presentation/ui_window_policy.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::platform::Win32AppRuntime;
using squarestar::market::CachedMarketOpen;
using squarestar::market::CurrentNewYorkTime;
using squarestar::market::MarketCloseMinutesForDate;
using squarestar::market::NextMarketSettlementAt;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::presentation::AnimatedFloatingMenuFrame;
using squarestar::presentation::GuiShellRuntime;
using squarestar::application::EaseOutCubic;
using squarestar::application::ConfiguredPriceAlert;
using squarestar::application::IsStockPriceAlertActive;
using squarestar::application::IsPriceAlertMutedUntil;
using squarestar::application::ApplyPriceAlertSilence;
using squarestar::application::ExpirePriceAlertMutes;
using squarestar::application::UpsertPriceAlertToast;
using squarestar::application::AppConfig;
using squarestar::application::AppState;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::ForegroundNotificationKind;
using squarestar::application::ShouldRenderForegroundNotification;
using squarestar::application::ShouldHoldMonitorModeHint;
using squarestar::application::KeyBind;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::alerts::ShouldPresentMarketOpenNotice;
using squarestar::platform::AppSoundDurationSeconds;
using squarestar::platform::StopAllAppAudio;
using squarestar::platform::PlayAppSoundRuntime;

// 36 px title bar + 16 px top margin + 40 px search bar + 16 px bottom margin.
// Search suggestions and LiteGUI confirmation dialogs use owned platform
// overlays, so the compact native host does not grow merely to contain them.
// Market ranges, world-clock zones, and cached market status compile independently.
ImVec4 ThemeVec(const float (&c)[4], float alpha) {
    return ImVec4(c[0], c[1], c[2], alpha >= 0.0f ? alpha : c[3]);
}
bool IsActionPressed(const AppConfig& state, TerminalAction action) {
#ifdef _WIN32
    if (!IsAppWindowForeground())
        return false;
#endif
    auto it = state.KeyBinds.find(action);
    if (it == state.KeyBinds.end() || it->second.key == ImGuiKey_None)
        return false;
    const KeyBind& bind = it->second;
    ImGuiIO& io = ImGui::GetIO();
    if (bind.ctrl != io.KeyCtrl)
        return false;
    if (bind.shift != io.KeyShift)
        return false;
    if (bind.alt != io.KeyAlt)
        return false;
    return ImGui::IsKeyPressed(bind.key, false);
}
void DrawOuterShadow(ImDrawList* dl,
                     ImVec2 min,
                     ImVec2 max,
                     float rounding,
                     float alphaMultiplier,
                     ImU32 shadowColor) {
    if (alphaMultiplier <= 0.0f)
        return;
    float shadowSize = 8.0f;
    for (float s = 2.0f; s <= shadowSize; s += 2.0f) {
        float alpha = (1.0f - (s / shadowSize)) * 0.12f * alphaMultiplier;
        ImU32 c = (shadowColor & 0x00FFFFFF) | ((int)(alpha * 255.0f) << 24);
        dl->AddRect(
            ImVec2(min.x - s, min.y - s), ImVec2(max.x + s, max.y + s), c, rounding + s, 0, 2.0f);
    }
}
void DrawIcon(ImDrawList* dl, ImVec2 center, float size, int type, ImU32 color) {
    float r = size * 0.5f;
    if (type == 0) {
        float w = r * 0.4f;
        dl->AddRectFilled(ImVec2(center.x - r, center.y + r * 0.2f),
                          ImVec2(center.x - r + w, center.y + r),
                          color);
        dl->AddRectFilled(ImVec2(center.x - w / 2, center.y - r * 0.5f),
                          ImVec2(center.x + w / 2, center.y + r),
                          color);
        dl->AddRectFilled(
            ImVec2(center.x + r - w, center.y - r), ImVec2(center.x + r, center.y + r), color);
    } else if (type == 1) {
        dl->AddRect(ImVec2(center.x - r * 0.7f, center.y - r),
                    ImVec2(center.x + r * 0.7f, center.y + r),
                    color,
                    2.0f,
                    0,
                    1.5f);
        dl->AddLine(ImVec2(center.x - r * 0.4f, center.y - r * 0.5f),
                    ImVec2(center.x + r * 0.4f, center.y - r * 0.5f),
                    color,
                    1.5f);
        dl->AddLine(ImVec2(center.x - r * 0.4f, center.y),
                    ImVec2(center.x + r * 0.4f, center.y),
                    color,
                    1.5f);
        dl->AddLine(ImVec2(center.x - r * 0.4f, center.y + r * 0.5f),
                    ImVec2(center.x + r * 0.2f, center.y + r * 0.5f),
                    color,
                    1.5f);
    } else if (type == 2) {
        dl->AddCircle(center, r * 0.9f, color, 0, 1.5f);
        dl->AddLine(center, ImVec2(center.x, center.y - r * 0.9f), color, 1.5f);
        dl->AddLine(center, ImVec2(center.x + r * 0.7f, center.y + r * 0.5f), color, 1.5f);
    } else if (type == 3) {
        dl->AddCircle(ImVec2(center.x - r * 0.2f, center.y - r * 0.2f), r * 0.5f, color, 0, 2.0f);
        dl->AddLine(ImVec2(center.x + r * 0.15f, center.y + r * 0.15f),
                    ImVec2(center.x + r * 0.8f, center.y + r * 0.8f),
                    color,
                    2.5f);
    } else if (type == 4) {
        dl->AddTriangleFilled(ImVec2(center.x, center.y - r),
                              ImVec2(center.x - r, center.y),
                              ImVec2(center.x + r, center.y),
                              color);
        dl->AddRectFilled(ImVec2(center.x - r * 0.6f, center.y),
                          ImVec2(center.x + r * 0.6f, center.y + r),
                          color);
    } else if (type == 5) {
        const float arm = r * 0.72f;
        dl->AddLine(
            ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), color, 2.0f);
        dl->AddLine(
            ImVec2(center.x, center.y - arm), ImVec2(center.x, center.y + arm), color, 2.0f);
    } else if (type == 6) {
        dl->AddCircle(center, r, color, 0, 2.0f);
        float sx = center.x, sy = center.y;
        dl->AddLine(ImVec2(sx, sy - r * 0.7f), ImVec2(sx, sy + r * 0.7f), color, 2.0f);
        dl->AddLine(ImVec2(sx + r * 0.3f, sy - r * 0.5f),
                    ImVec2(sx - r * 0.3f, sy - r * 0.2f),
                    color,
                    1.5f);
        dl->AddLine(ImVec2(sx - r * 0.3f, sy + r * 0.2f),
                    ImVec2(sx + r * 0.3f, sy + r * 0.5f),
                    color,
                    1.5f);
    } else if (type == 7) {
        ImVec2 points[5] = {ImVec2(center.x - r * 0.6f, center.y + r),
                            ImVec2(center.x + r * 0.6f, center.y + r),
                            ImVec2(center.x + r * 0.6f, center.y + r * 0.2f),
                            ImVec2(center.x, center.y - r),
                            ImVec2(center.x - r * 0.6f, center.y + r * 0.2f)};
        dl->AddConvexPolyFilled(points, 5, color);
    } else if (type == 8) {
        float s = r * 0.35f;
        float d = r * 0.45f;
        dl->AddRectFilled(ImVec2(center.x - d - s, center.y - d - s),
                          ImVec2(center.x - d + s, center.y - d + s),
                          color,
                          1.0f);
        dl->AddRectFilled(ImVec2(center.x + d - s, center.y - d - s),
                          ImVec2(center.x + d + s, center.y - d + s),
                          color,
                          1.0f);
        dl->AddRectFilled(ImVec2(center.x - d - s, center.y + d - s),
                          ImVec2(center.x - d + s, center.y + d + s),
                          color,
                          1.0f);
        dl->AddRectFilled(ImVec2(center.x + d - s, center.y + d - s),
                          ImVec2(center.x + d + s, center.y + d + s),
                          color,
                          1.0f);
    } else if (type == 9) {
        dl->AddRect(ImVec2(center.x - r * 0.3f, center.y - r * 0.8f),
                    ImVec2(center.x + r * 0.3f, center.y - r * 0.4f),
                    color,
                    2.0f,
                    0,
                    1.5f);
        dl->AddRectFilled(ImVec2(center.x - r * 0.9f, center.y - r * 0.4f),
                          ImVec2(center.x + r * 0.9f, center.y + r * 0.7f),
                          color,
                          2.0f);
    } else if (type == 10) {
        dl->AddQuad(ImVec2(center.x - r, center.y),
                    ImVec2(center.x, center.y - r * 0.6f),
                    ImVec2(center.x + r, center.y),
                    ImVec2(center.x, center.y + r * 0.6f),
                    color,
                    2.0f);
        dl->AddCircleFilled(center, r * 0.35f, color);
    } else if (type == 11) {
        dl->AddCircle(center, r * 0.6f, color, 0, 2.0f);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * IM_PI / 4.0f;
            dl->AddLine(ImVec2(center.x + cosf(a) * r * 0.6f, center.y + sinf(a) * r * 0.6f),
                        ImVec2(center.x + cosf(a) * r * 0.9f, center.y + sinf(a) * r * 0.9f),
                        color,
                        2.0f);
        }
    } else if (type == 12) {
        float hr = r * 0.45f;
        ImVec2 c1 = ImVec2(center.x - hr * 0.8f, center.y - hr * 0.4f);
        ImVec2 c2 = ImVec2(center.x + hr * 0.8f, center.y - hr * 0.4f);
        ImVec2 bottom = ImVec2(center.x, center.y + r * 0.8f);
        dl->AddCircleFilled(c1, hr, color);
        dl->AddCircleFilled(c2, hr, color);
        ImVec2 points[3] = {ImVec2(c1.x - hr * 0.95f, c1.y + hr * 0.3f),
                            ImVec2(c2.x + hr * 0.95f, c2.y + hr * 0.3f),
                            bottom};
        dl->AddTriangleFilled(points[0], points[1], points[2], color);
    } else if (type == 13) {
        const float left = center.x - r * 0.92f;
        const float step = r * 0.58f;
        const float weight = std::max(1.5f, r * 0.16f);
        dl->AddLine(ImVec2(left, center.y + r * 0.42f),
                    ImVec2(left + step, center.y - r * 0.18f),
                    color,
                    weight);
        dl->AddLine(ImVec2(left + step, center.y - r * 0.18f),
                    ImVec2(left + step * 2.0f, center.y + r * 0.05f),
                    color,
                    weight);
        dl->AddLine(ImVec2(left + step * 2.0f, center.y + r * 0.05f),
                    ImVec2(left + step * 3.0f, center.y - r * 0.70f),
                    color,
                    weight);
        const ImU32 secondaryAlpha =
            static_cast<ImU32>(static_cast<float>(color >> 24) * 0.68f);
        const ImU32 secondary = (color & 0x00FFFFFFu) | (secondaryAlpha << 24);
        dl->AddLine(ImVec2(left, center.y - r * 0.02f),
                    ImVec2(left + step, center.y + r * 0.62f),
                    secondary,
                    weight);
        dl->AddLine(ImVec2(left + step, center.y + r * 0.62f),
                    ImVec2(left + step * 2.0f, center.y + r * 0.30f),
                    secondary,
                    weight);
        dl->AddLine(ImVec2(left + step * 2.0f, center.y + r * 0.30f),
                    ImVec2(left + step * 3.0f, center.y + r * 0.73f),
                    secondary,
                    weight);
    } else if (type == 14) {
        const ImVec2 screenMin(center.x - r * 0.92f, center.y - r * 0.72f);
        const ImVec2 screenMax(center.x + r * 0.92f, center.y + r * 0.72f);
        dl->AddRect(screenMin, screenMax, color, 2.0f, 0, 1.7f);
        dl->AddRectFilled(ImVec2(screenMin.x + r * 0.18f, screenMin.y + r * 0.18f),
                          ImVec2(screenMax.x - r * 0.18f, screenMin.y + r * 0.38f),
                          color,
                          1.0f);
        const float chartLeft = screenMin.x + r * 0.22f;
        const float chartRight = screenMax.x - r * 0.22f;
        dl->AddLine(ImVec2(chartLeft, center.y + r * 0.30f),
                    ImVec2(chartLeft + (chartRight - chartLeft) * 0.34f,
                           center.y - r * 0.08f),
                    color,
                    1.5f);
        dl->AddLine(ImVec2(chartLeft + (chartRight - chartLeft) * 0.34f,
                           center.y - r * 0.08f),
                    ImVec2(chartRight, center.y + r * 0.12f),
                    color,
                    1.5f);
    } else if (type == 15) {
        // Speedometer gauge.
        const ImVec2 gaugeCenter(center.x, center.y + r * 0.28f);
        dl->PathArcTo(gaugeCenter, r * 0.86f, IM_PI, 2.0f * IM_PI, 20);
        dl->PathStroke(color, 0, 1.8f);
        dl->AddLine(ImVec2(center.x - r * 0.82f, gaugeCenter.y),
                    ImVec2(center.x + r * 0.82f, gaugeCenter.y),
                    color,
                    1.5f);
        const float needleAngle = -0.82f;
        dl->AddLine(gaugeCenter,
                    ImVec2(gaugeCenter.x + std::cos(needleAngle) * r * 0.62f,
                           gaugeCenter.y + std::sin(needleAngle) * r * 0.62f),
                    color,
                    2.2f);
        dl->AddCircleFilled(gaugeCenter, r * 0.13f, color);
    } else if (type == 16) {
        // Notification bell used by the global price-alert manager.
        const float bellTop = center.y - r * 0.62f;
        const float bellBottom = center.y + r * 0.42f;
        dl->PathLineTo(ImVec2(center.x - r * 0.62f, bellBottom));
        dl->PathBezierCubicCurveTo(
            ImVec2(center.x - r * 0.58f, center.y - r * 0.18f),
            ImVec2(center.x - r * 0.42f, bellTop),
            ImVec2(center.x, bellTop));
        dl->PathBezierCubicCurveTo(
            ImVec2(center.x + r * 0.42f, bellTop),
            ImVec2(center.x + r * 0.58f, center.y - r * 0.18f),
            ImVec2(center.x + r * 0.62f, bellBottom));
        dl->PathStroke(color, 0, 1.8f);
        dl->AddLine(ImVec2(center.x - r * 0.76f, bellBottom),
                    ImVec2(center.x + r * 0.76f, bellBottom),
                    color,
                    1.8f);
        dl->AddCircleFilled(ImVec2(center.x, center.y + r * 0.68f), r * 0.13f, color);
    }
}
struct AnimatedButtonFrame {
    ImGuiWindow* window = nullptr;
    ImVec2 center{};
    bool pressed = false;
    bool hovered = false;
    bool visible = false;
};
static AnimatedButtonFrame
DrawAnimatedButtonFrame(const AppState& state,
                        const char* label,
                        const ImVec2& size,
                        bool active,
                        bool animEnabled) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(label);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bounds(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bounds, id))
        return {};
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);
    float* hoverAnimation = ImGui::GetStateStorage()->GetFloatRef(id, 0.0f);
    const float previousAnimation = *hoverAnimation;
    const float target = hovered ? 1.0f : 0.0f;
    if (animEnabled) {
        const float dt = UiFrameDelta();
        *hoverAnimation += (target - *hoverAnimation) * (1.0f - std::exp(-30.0f * dt));
    } else {
        *hoverAnimation = target;
    }
    const float scale = 1.0f + *hoverAnimation * 0.05f;
    const ImVec2 center(bounds.Min.x + size.x * 0.5f, bounds.Min.y + size.y * 0.5f);
    const ImVec2 min(center.x - size.x * 0.5f * scale, center.y - size.y * 0.5f * scale);
    const ImVec2 max(center.x + size.x * 0.5f * scale, center.y + size.y * 0.5f * scale);
    if (!state.ZeroGraphicsEnabled()) {
        DrawOuterShadow(
            window->DrawList, min, max, g.Style.FrameRounding, 0.3f + previousAnimation * 0.7f);
    }
    ImU32 color = ImGui::GetColorU32((held && hovered)     ? ImGuiCol_ButtonActive
                                     : (hovered || active) ? ImGuiCol_ButtonHovered
                                                           : ImGuiCol_Button);
    if (active && !hovered)
        color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
    window->DrawList->AddRectFilled(min, max, color, g.Style.FrameRounding);
    return {window, center, pressed, hovered, true};
}
bool AnimatedButton(const AppState& state,
                    const char* label,
                    const ImVec2& requestedSize,
                    bool active,
                    bool animEnabled) {
    ImGuiContext& g = *GImGui;
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, labelEnd, true);
    const ImVec2 size = ImGui::CalcItemSize(requestedSize,
                                            labelSize.x + g.Style.FramePadding.x * 2.0f,
                                            labelSize.y + g.Style.FramePadding.y * 2.0f);
    const AnimatedButtonFrame frame = DrawAnimatedButtonFrame(state, label, size, active, animEnabled);
    if (!frame.visible)
        return false;
    frame.window->DrawList->AddText(
        ImVec2(frame.center.x - labelSize.x * 0.5f, frame.center.y - labelSize.y * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text),
        label,
        labelEnd);
    return frame.pressed;
}
void DrawRoundedCurrentWindowPanel(float rounding,
                                          ImGuiCol fillColumn) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window)
        return;
    const ImVec2 min = window->Pos;
    const ImVec2 max(min.x + window->Size.x, min.y + window->Size.y);
    const float borderSize = ImGui::GetStyle().WindowBorderSize;
    ImDrawList* draw = window->DrawList;
    draw->PushClipRectFullScreen();
    draw->AddRectFilled(min, max, ImGui::GetColorU32(fillColumn), rounding);
    if (borderSize > 0.0f) {
        const float inset = borderSize * 0.5f;
        draw->AddRect(ImVec2(min.x + inset, min.y + inset),
                      ImVec2(max.x - inset, max.y - inset),
                      ImGui::GetColorU32(ImGuiCol_Border),
                      rounding,
                      ImDrawFlags_RoundCornersAll,
                      borderSize);
    }
    draw->PopClipRect();
}
bool BeginAnimatedFloatingMenu(const AppState& state,
                               const char* menu_id,
                               bool& toggle_trigger,
                               ImVec2 pos,
                               ImVec2 pivot,
                               bool animEnabled,
                               ImVec2 minimumSize,
    ImVec2 maximumSize) {
    ImGuiID id = ImGui::GetID(menu_id);
    const bool toggled = toggle_trigger;
    const AnimatedFloatingMenuFrame menu =
        GuiShellRuntime().UpdateAnimatedFloatingMenu(
            id,
            toggled,
            ImGui::IsKeyPressed(ImGuiKey_Escape, false),
            ImGui::GetFrameCount(),
            animEnabled,
            UiFrameDelta());
    if (toggled)
        toggle_trigger = false;
    if (menu.needsRedraw)
        RequestGuiRedraw();
    if (!menu.visible)
        return false;
    float y_offset = (1.0f - menu.animation) * (pivot.y > 0.5f ? 8.0f : -8.0f);
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + y_offset), ImGuiCond_Always, pivot);
    if (minimumSize.x > 0.0f || minimumSize.y > 0.0f ||
        maximumSize.x < std::numeric_limits<float>::max() ||
        maximumSize.y < std::numeric_limits<float>::max())
        ImGui::SetNextWindowSizeConstraints(minimumSize, maximumSize);
    const float menuRounding = UiRounding(state, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, EaseOutCubic(menu.animation));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, menuRounding);
    ImVec4 bgCol = ImGui::GetStyle().Colors[ImGuiCol_PopupBg];
    bgCol.w = 1.0f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgCol);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyle().Colors[ImGuiCol_Border]);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoBackground;
    if (!menu.open) {
        flags |= ImGuiWindowFlags_NoInputs;
    }
    ImGui::Begin(menu_id, nullptr, flags);
    DrawRoundedCurrentWindowPanel(menuRounding);
    if (menu.open && ImGui::IsMouseClicked(0) &&
        ImGui::GetFrameCount() != menu.ignoreFrame) {
        ImVec2 m = ImGui::GetMousePos();
        ImVec2 min = ImGui::GetWindowPos();
        ImVec2 max = ImVec2(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
        if (m.x < min.x || m.x > max.x || m.y < min.y || m.y > max.y) {
            (void)GuiShellRuntime().DismissAnimatedFloatingMenu(id, false);
            RequestGuiRedraw();
        }
    }
    return true;
}
void CloseAnimatedFloatingMenu(bool instant) {
    if (GuiShellRuntime().DismissCurrentAnimatedFloatingMenu(instant))
        RequestGuiRedraw();
}
void CloseAllAnimatedFloatingMenus() {
    GuiShellRuntime().CloseAllAnimatedFloatingMenus();
}
void EndAnimatedFloatingMenu() {
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    GuiShellRuntime().EndAnimatedFloatingMenu();
}
ImVec2 ClampPopupPosition(ImGuiViewport* viewport, ImVec2 position, ImVec2 size) {
    ImGuiViewport* boundsViewport = viewport ? viewport : ImGui::GetMainViewport();
    const ImVec2 boundsMin = boundsViewport->WorkPos;
    // Animated context menus rise by as much as eight pixels while opening.
    // Keep that travel inside the viewport so the top rows and focus cutout do
    // not get clipped during the transition.
    constexpr float margin = 14.0f;
    const auto clamped = squarestar::presentation::ClampWindowToBounds(
        {position.x, position.y},
        {size.x, size.y},
        {boundsMin.x, boundsMin.y, boundsViewport->WorkSize.x, boundsViewport->WorkSize.y},
        margin);
    return ImVec2(clamped.x, clamped.y);
}
ImVec2 ClampTooltipPositionToOwningWindow(ImVec2 position,
                                                 ImVec2 size,
                                                 float margin) {
    ImGuiWindow* current = ImGui::GetCurrentWindow();
    ImGuiWindow* owner = current && current->RootWindow ? current->RootWindow : current;
    if (!owner)
        return ClampPopupPosition(ImGui::GetMainViewport(), position, size);

    const auto clamped = squarestar::presentation::ClampWindowToBounds(
        {position.x, position.y},
        {size.x, size.y},
        {owner->Pos.x, owner->Pos.y, owner->Size.x, owner->Size.y},
        margin);
    position = ImVec2(clamped.x, clamped.y);
#ifdef IMGUI_HAS_VIEWPORT
    if (owner->Viewport)
        ImGui::SetNextWindowViewport(owner->Viewport->ID);
#endif
    return position;
}
void DrawContainedTooltip(const char* text, float preferredWrapWidth) {
    if (!text || !*text)
        return;
    ImGuiWindow* current = ImGui::GetCurrentWindow();
    ImGuiWindow* owner = current && current->RootWindow ? current->RootWindow : current;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float ownerWidth = owner ? owner->Size.x : preferredWrapWidth;
    const float contentWidth = std::max(
        1.0f,
        std::min(preferredWrapWidth,
                 ownerWidth - style.WindowPadding.x * 2.0f - 16.0f));
    const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, contentWidth);
    const ImVec2 estimate(textSize.x + style.WindowPadding.x * 2.0f + 2.0f,
                          textSize.y + style.WindowPadding.y * 2.0f + 2.0f);
    const ImVec2 mouse = ImGui::GetMousePos();
    ImGui::SetNextWindowPos(
        ClampTooltipPositionToOwningWindow(ImVec2(mouse.x + 14.0f, mouse.y + 16.0f),
                                           estimate),
        ImGuiCond_Always);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
bool BeginClampedContextMenu(const AppState& state,
                             const char* id,
                             ImGuiViewport* viewport,
                             ImVec2 estimatedSize,
                             bool enabled,
                             bool animEnabled,
                             ImVec2 activationMin,
                             ImVec2 activationMax,
                             ImVec2 maximumSize) {
    if (!enabled)
        return false;

    // Keep chart context menus in the same animated window system as the rest
    // of the UI. Besides providing a smooth open/close transition, this lets
    // the object-focus overlay treat the menu as the active surface.
    ImGuiStorage* ownerStorage = ImGui::GetStateStorage();
    const ImGuiID menuId = ImGui::GetID(id);
    const bool hasActivationRect = activationMax.x > activationMin.x &&
                                   activationMax.y > activationMin.y;
    const bool activationHovered =
        hasActivationRect
            ? ImGui::IsMouseHoveringRect(activationMin, activationMax, false)
            : ImGui::IsItemHovered();
    // Toggle on the press. Waiting for release made the second activation easy
    // to lose when the event-driven loop changed the focused ImGui window after
    // the first context menu had appeared.
    bool toggleTrigger =
        activationHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right, false);
    float* storedX = ownerStorage->GetFloatRef(menuId + 4, ImGui::GetIO().MousePos.x);
    float* storedY = ownerStorage->GetFloatRef(menuId + 5, ImGui::GetIO().MousePos.y);
    ImVec2 clampSize = estimatedSize;
    if (ImGuiWindow* existingMenu = ImGui::FindWindowByName(id)) {
        clampSize.x = std::max(clampSize.x, existingMenu->SizeFull.x);
        clampSize.y = std::max(clampSize.y, existingMenu->SizeFull.y);
    }
    if (toggleTrigger) {
        const ImVec2 target =
            ClampPopupPosition(viewport, ImGui::GetIO().MousePos, clampSize);
        *storedX = target.x;
        *storedY = target.y;
    } else {
        const ImVec2 target =
            ClampPopupPosition(viewport, ImVec2(*storedX, *storedY), clampSize);
        *storedX = target.x;
        *storedY = target.y;
    }
#ifdef IMGUI_HAS_VIEWPORT
    if (viewport)
        ImGui::SetNextWindowViewport(viewport->ID);
#endif
    if (!BeginAnimatedFloatingMenu(state,
                                   id,
                                   toggleTrigger,
                                   ImVec2(*storedX, *storedY),
                                   ImVec2(0.0f, 0.0f),
                                   animEnabled,
                                   ImVec2(estimatedSize.x, 0.0f),
                                   maximumSize))
        return false;
    return true;
}
bool PlayUISound(const char* filename, const AppState& state) {
    if (!state.config.soundEnabled || !filename)
        return false;
    // LiteGUI keeps ordinary feedback audio-only. Price alerts use the separate
    // alert playback path.
    if (ApplicationRuntime().CurrentUiMode() ==
            squarestar::application::AppUiMode::LiteGui &&
        std::strcmp(filename, "key.wav") != 0 &&
        std::strcmp(filename, "transition.wav") != 0 &&
        std::strcmp(filename, "loading.wav") != 0 &&
        std::strcmp(filename, "click.wav") != 0 &&
        std::strcmp(filename, "decline.wav") != 0)
        return false;
#ifdef _WIN32
    if (std::strcmp(filename, "click.wav") == 0 &&
        ApplicationRuntime().CurrentUiMode() ==
            squarestar::application::AppUiMode::Gui) {
        if (!IsAppWindowForeground())
            return false;
        if (!ImGui::GetCurrentContext() ||
            (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
             !ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
             !ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
             !ImGui::IsMouseReleased(ImGuiMouseButton_Right)))
            return false;
    }
#endif
    return PlayAppSoundRuntime(filename);
}
void CommitUiSetting(AppState& state, const char* sound) {
    PlayUISound(sound, state);
    squarestar::config::RequestConfigSave();
}
bool UiCombo(AppState& state,
                    const char* label,
                    int* currentItem,
                    const char* const items[],
                    int itemCount) {
    if (!currentItem || !items || itemCount <= 0)
        return false;

    const int previewIndex = std::clamp(*currentItem, 0, itemCount - 1);
    const char* preview = items[previewIndex] ? items[previewIndex] : "";
    bool changed = false;

    const bool open = ImGui::BeginCombo(label, preview);
    // Capture activation before popup rows replace ImGui's last-item state.
    if (ImGui::IsItemActivated())
        PlayUISound("click.wav", state);

    if (open) {
        for (int i = 0; i < itemCount; ++i) {
            const bool selected = (*currentItem == i);
            if (ImGui::Selectable(items[i] ? items[i] : "", selected)) {
                *currentItem = i;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}
void SilencePriceAlertTicker(AppState& state,
                                    std::string_view ticker,
                                    bool muteUntilSettlement) {
    const std::optional<int64_t> muteUntil =
        muteUntilSettlement
            ? std::optional<int64_t>(static_cast<int64_t>(NextMarketSettlementAt(std::time(nullptr))))
            : std::nullopt;
    const bool stoppedSequence = ApplyPriceAlertSilence(state, ticker, muteUntil);
    if (stoppedSequence)
        StopAllAppAudio();
    RequestGuiRedraw();
}
bool ExpirePriceAlertSettlementMutes(AppState& state,
                                             std::time_t& lastCheckedSecond) {
    const std::time_t nowSecond = std::time(nullptr);
    if (nowSecond == lastCheckedSecond)
        return false;
    lastCheckedSecond = nowSecond;
    return ExpirePriceAlertMutes(state, static_cast<int64_t>(nowSecond));
}
static void QueuePriceAlertToast(AppState& state,
                                 const StockContext& ctx,
                                 double threshold) {
    if (!UseForegroundNotificationBlocks() ||
        !ShouldRenderForegroundNotification(
            ApplicationRuntime().CurrentUiMode(),
            ForegroundNotificationKind::PriceAlert))
        return;
    if (UpsertPriceAlertToast(state, ctx, threshold))
        RequestGuiRedraw();
}
void EvaluateStockPriceAlert(AppState& state, StockContext& ctx) {
    const auto alert = ConfiguredPriceAlert(state.alerts, ctx);
    if (!alert) {
        state.alerts.ClearSilence(ctx.navigation.ticker);
        ctx.alerts.priceAlertTriggered = false;
        ctx.alerts.priceAlertSoundPlaysRemaining = 0;
        if (state.render.notifications.notificationCenter.RemovePriceAlertForTicker(
                ctx.navigation.ticker))
            RequestGuiRedraw();
        return;
    }
    if (!ctx.RawData().success || !std::isfinite(ctx.RawData().currentPrice) ||
        ctx.RawData().currentPrice <= 0.0)
        return;
    const bool below = ctx.RawData().currentPrice <= *alert;
    if (below && (state.alerts.IsSilenced(ctx.navigation.ticker) ||
                  IsPriceAlertMutedUntil(state.alerts,
                                         ctx.navigation.ticker,
                                         static_cast<int64_t>(std::time(nullptr))))) {
        ctx.alerts.priceAlertTriggered = true;
        ctx.alerts.priceAlertSoundPlaysRemaining = 0;
    } else if (below && !ctx.alerts.priceAlertTriggered) {
        ctx.alerts.priceAlertTriggered = true;
        ctx.alerts.priceAlertSoundPlaysRemaining = 1;
        ctx.alerts.nextPriceAlertSoundAt = std::chrono::steady_clock::now();

        squarestar::application::StockMoveNotification historyRow;
        historyRow.ticker = ctx.navigation.ticker;
        historyRow.after = ctx.RawData().currentPrice;
        historyRow.priceAlert = true;
        historyRow.alertThreshold = *alert;
        historyRow.currency = "USD";
        state.render.notifications.notificationCenter.Push(std::move(historyRow));

        QueuePriceAlertToast(state, ctx, *alert);
    } else if (!below) {
        state.alerts.ClearSilencedFlag(ctx.navigation.ticker);
        ctx.alerts.priceAlertTriggered = false;
        ctx.alerts.priceAlertSoundPlaysRemaining = 0;
        if (state.render.notifications.notificationCenter.RemovePriceAlertForTicker(
                ctx.navigation.ticker))
            RequestGuiRedraw();
    }
}
void SilencePriceAlertForClosedTab(AppState& state, StockContext& ctx) {
    if (IsStockPriceAlertActive(state.alerts, ctx) || ctx.alerts.priceAlertTriggered ||
        ctx.alerts.priceAlertSoundPlaysRemaining > 0)
        SilencePriceAlertTicker(state, ctx.navigation.ticker, false);
    else {
        state.alerts.RemoveToastsForTicker(ctx.navigation.ticker);
    }
}
void PumpPriceAlertSounds(AppState& state) {
    const auto now = std::chrono::steady_clock::now();
    if (now < state.alerts.AlertPresentationNotBefore() ||
        (ApplicationRuntime().CurrentUiMode() ==
             squarestar::application::AppUiMode::Gui &&
         state.StartupAnimationVisible()))
        return;
    const auto pump = [&](auto& contexts) {
        for (auto& ctx : contexts) {
            if (!ctx || ctx->alerts.priceAlertSoundPlaysRemaining <= 0)
                continue;
            if (!state.config.soundEnabled) {
                ctx->alerts.priceAlertSoundPlaysRemaining = 0;
                continue;
            }
            if (now < ctx->alerts.nextPriceAlertSoundAt)
                continue;
            // Price alerts still play in LiteGUI even though other UI sounds are filtered.
            if (PlayAppSoundRuntime("error.wav")) {
                --ctx->alerts.priceAlertSoundPlaysRemaining;
                const double replayDelay =
                    AppSoundDurationSeconds("error.wav") + 1.0;
                ctx->alerts.nextPriceAlertSoundAt =
                    now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(replayDelay));
            }
        }
    };
    pump(state.marketData.activeContexts);
    pump(state.alerts.Monitors());
}
void PumpMarketOpenSound(AppState& state) {
    const auto now = std::chrono::steady_clock::now();
    if (now < state.alerts.NextMarketOpenCheckAt())
        return;
    // Mode changes and network completions have explicit wake events. A 30-second
    // boundary check is sufficient for the market-open cue.
    state.alerts.SetNextMarketOpenCheckAt(now + std::chrono::seconds(30));
    const bool marketOpen = CachedMarketOpen();
    const bool marketJustOpened = ShouldPresentMarketOpenNotice(
        state.alerts.MarketOpenStateInitialized(),
        state.alerts.LastMarketOpenState(),
        marketOpen);
    if (marketJustOpened) {
        state.alerts.SetMarketOpenSoundPending(state.config.soundEnabled);
        if (UseBackgroundNotificationBlock()) {
            const std::time_t wallNow = std::time(nullptr);
            const std::tm local = CurrentNewYorkTime(wallNow);
            const int closeMinutes =
                MarketCloseMinutesForDate(local.tm_year + 1900,
                                          local.tm_mon + 1,
                                          local.tm_mday);
            const char* closeLabel = closeMinutes == 13 * 60 ? "1:00 PM ET" : "4:00 PM ET";
            const std::string message =
                std::string("Regular trading is open. Price refreshes, movement notices, and "
                            "alerts are active until ") +
                closeLabel + ".";
            TriggerTrayNotification("US market is open", message.c_str());
        } else if (UseForegroundNotificationBlocks() &&
                   ShouldRenderForegroundNotification(
                       ApplicationRuntime().CurrentUiMode(),
                       ForegroundNotificationKind::MarketOpen)) {
            state.render.notifications.marketOpen.until =
                now + std::chrono::milliseconds(6500);
            state.render.notifications.marketOpen.animation =
                state.UiAnimationsEnabled() ? 0.0f : 1.0f;
            RequestGuiRedraw();
        }
    }
    if (!state.config.soundEnabled)
        state.alerts.SetMarketOpenSoundPending(false);
    if (state.alerts.MarketOpenSoundPending() &&
        PlayUISound("marketopenclose.wav", state))
        state.alerts.SetMarketOpenSoundPending(false);
    state.alerts.SetMarketOpenState(true, marketOpen);
}
void ApplyTheme(const AppState& state) {
    if (IsLightGuiTheme(state.config.themeModeIndex))
        ImGui::StyleColorsLight();
    else
        ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;
    const auto& t = state.config.theme;
    s.WindowRounding = state.ZeroGraphicsEnabled() ? 0.0f : t.windowRounding;
    s.FrameRounding = state.ZeroGraphicsEnabled() ? 0.0f : t.frameRounding;
    s.PopupRounding = state.ZeroGraphicsEnabled() ? 0.0f : t.popupRounding;
    s.ScrollbarRounding = state.ZeroGraphicsEnabled() ? 0.0f : t.scrollbarRounding;
    s.GrabRounding = state.ZeroGraphicsEnabled() ? 0.0f : t.grabRounding;
    s.TabRounding = state.ZeroGraphicsEnabled() ? 0.0f : t.tabRounding;
    s.ChildRounding = state.ZeroGraphicsEnabled() ? 0.0f : t.childRounding;
    s.WindowPadding = ImVec2(t.windowPaddingX, t.windowPaddingY);
    s.FramePadding = ImVec2(t.framePaddingX, t.framePaddingY);
    s.ItemSpacing = ImVec2(t.itemSpacingX, t.itemSpacingY);
    s.ItemInnerSpacing = ImVec2(t.itemInnerSpacingX, t.itemInnerSpacingY);
    s.ScrollbarSize = t.scrollbarSize;
    s.WindowBorderSize = state.ZeroGraphicsEnabled() ? 0.0f : t.windowBorderSize;
    s.FrameBorderSize = state.ZeroGraphicsEnabled() ? 0.0f : t.frameBorderSize;
    s.TabBorderSize = state.ZeroGraphicsEnabled() ? 0.0f : t.tabBorderSize;
#if IMGUI_VERSION_NUM >= 19090
    // The tab fill already communicates selection; the newer ImGui overline
    // added a redundant top stroke that looked like a stray border.
    s.TabBarOverlineSize = 0.0f;
#endif
    s.AntiAliasedLines = !state.ZeroGraphicsEnabled();
    s.AntiAliasedLinesUseTex = !state.ZeroGraphicsEnabled();
    s.AntiAliasedFill = !state.ZeroGraphicsEnabled();
    c[ImGuiCol_WindowBg] = {t.bg[0], t.bg[1], t.bg[2], t.bg[3]};
    c[ImGuiCol_ChildBg] = {t.bg[0] + .02f, t.bg[1] + .02f, t.bg[2] + .02f, 1.f};
    c[ImGuiCol_PopupBg] = {t.popupBg[0], t.popupBg[1], t.popupBg[2], t.popupBg[3]};
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] =
        c[ImGuiCol_TitleBgCollapsed] = {t.titleBg[0], t.titleBg[1], t.titleBg[2], t.titleBg[3]};
    c[ImGuiCol_Text] = {t.text[0], t.text[1], t.text[2], t.text[3]};
    c[ImGuiCol_TextDisabled] = {
        t.textDisabled[0], t.textDisabled[1], t.textDisabled[2], t.textDisabled[3]};
    c[ImGuiCol_Border] = {t.border[0], t.border[1], t.border[2], t.border[3]};
    c[ImGuiCol_FrameBg] = {t.frameBg[0], t.frameBg[1], t.frameBg[2], t.frameBg[3]};
    c[ImGuiCol_FrameBgHovered] = {
        t.frameBgHovered[0], t.frameBgHovered[1], t.frameBgHovered[2], t.frameBgHovered[3]};
    c[ImGuiCol_FrameBgActive] = {
        t.frameBgActive[0], t.frameBgActive[1], t.frameBgActive[2], t.frameBgActive[3]};
    c[ImGuiCol_Button] = {t.button[0], t.button[1], t.button[2], t.button[3]};
    c[ImGuiCol_ButtonHovered] = {
        t.buttonHover[0], t.buttonHover[1], t.buttonHover[2], t.buttonHover[3]};
    c[ImGuiCol_ButtonActive] = {
        t.buttonActive[0], t.buttonActive[1], t.buttonActive[2], t.buttonActive[3]};
    c[ImGuiCol_Tab] = c[ImGuiCol_TabDimmed] = ThemeVec(t.tabInactive);
    c[ImGuiCol_TabHovered] = ThemeVec(t.tabHovered);
    c[ImGuiCol_TabSelected] = ThemeVec(t.tabActive);
    c[ImGuiCol_TabDimmedSelected] = ThemeVec(t.tabActive, 0.82f);
    c[ImGuiCol_Header] = {t.header[0], t.header[1], t.header[2], t.header[3]};
    c[ImGuiCol_HeaderHovered] = {
        t.headerHovered[0], t.headerHovered[1], t.headerHovered[2], t.headerHovered[3]};
    c[ImGuiCol_HeaderActive] = {
        t.headerHovered[0] + .05f, t.headerHovered[1] + .05f, t.headerHovered[2] + .05f, 1.f};
    const ImVec4 selectionMark = ThemeVec(state.config.theme.inverseBg);
    c[ImGuiCol_CheckMark] = selectionMark;
    c[ImGuiCol_SliderGrab] = selectionMark;
    c[ImGuiCol_SliderGrabActive] = selectionMark;

    squarestar::platform::NativeNotificationStyle notificationStyle;
    const auto toNativeColor = [](const float color[4]) {
        const auto channel = [](float value) {
            return static_cast<BYTE>(
                std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        };
        return RGB(channel(color[0]), channel(color[1]), channel(color[2]));
    };
    notificationStyle.positive = toNativeColor(state.config.theme.positive);
    notificationStyle.negative = toNativeColor(state.config.theme.negative);
    if (IsLightGuiTheme(state.config.themeModeIndex)) {
        notificationStyle.background = RGB(249, 249, 250);
        notificationStyle.border = RGB(140, 140, 148);
        notificationStyle.title = RGB(20, 20, 23);
        notificationStyle.body = RGB(92, 92, 99);
    } else {
        notificationStyle.background = RGB(14, 14, 15);
        notificationStyle.border = RGB(87, 87, 92);
        notificationStyle.title = RGB(245, 245, 247);
        notificationStyle.body = RGB(179, 179, 186);
    }
    Win32AppRuntime().SetNotificationStyle(notificationStyle);
}
void SnapAllUiAnimations(AppState& state) {
    const auto now = std::chrono::steady_clock::now();
    const size_t visibleMonitorTabs = CountMonitorStockTiles(state.marketData);
    auto& notifications = state.render.notifications;

    notifications.monitorModeHint.presentation.animation =
        ShouldHoldMonitorModeHint(state.navigation.pureMonitorMode,
                                  visibleMonitorTabs < 2,
                                  state.config.monitorModeHintDisabled,
                                  notifications.monitorModeHint.dismissed,
                                  notifications.monitorModeHint.presentation.Holding(now))
            ? 1.0f
            : 0.0f;
    notifications.marketOpen.animation = notifications.marketOpen.Holding(now) ? 1.0f : 0.0f;
    notifications.firstFetchWarmup.presentation.animation =
        notifications.firstFetchWarmup.presentation.Holding(now) ? 1.0f : 0.0f;
    notifications.keybindHint.presentation.animation =
        notifications.keybindHint.presentation.Holding(now) ? 1.0f : 0.0f;
    for (auto& notice : notifications.marketMoves.notices)
        notice.animation = notice.Holding(now) ? 1.0f : 0.0f;
    state.render.sidebarAnim = !state.config.sidebarHidden && state.navigation.isSidebarHovered
                            ? state.config.theme.sidebarExpanded
                            : state.config.theme.sidebarCollapsed;
    for (auto& ctx : state.marketData.activeContexts) {
        if (!ctx)
            continue;
        ctx->render.animProgress = 1.0f;
        ctx->render.tabFadeAnim = 1.0f;
        ctx->render.openTransitionProgress = 1.0f;
        ctx->render.chartRevealProgress = 1.0f;
        ctx->render.priceFlashAnim = 0.0f;
        ctx->render.loadingBlockAnim = ctx->requests.isLoading ? 1.0f : 0.0f;
        ctx->render.fadeAlpha = ctx->requests.isLoading ? 0.0f : 1.0f;
        ctx->render.hoverAlpha = 0.0f;
        ctx->render.tooltipWipeAnim = 0.0f;
        ctx->render.xAxisHoverAlpha = 0.0f;
        ctx->render.comparisonXAxisHoverAlpha = 0.0f;
        ctx->render.transientNoticeAnim =
            ctx->render.transientNoticeUntil != std::chrono::steady_clock::time_point{} &&
                    std::chrono::steady_clock::now() < ctx->render.transientNoticeUntil
                ? 1.0f
                : 0.0f;
    }
}
void EnforceZeroGraphicsMode(AppState& state, bool snapImmediately) {
    if (state.ZeroGraphicsEnabled() && snapImmediately)
        SnapAllUiAnimations(state);
}

bool RenderUnifiedLink(const AppState& state,
                       const char* label,
                       const char* id,
                       float wrapWidth,
                       float restingContrast) {
    if (!label || label[0] == '\0')
        return false;
    ImGui::PushID(id ? id : label);
    constexpr float iconGap = 5.0f;
    constexpr float iconSize = 8.0f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float available = wrapWidth > 0.0f ? wrapWidth : ImGui::GetContentRegionAvail().x;
    const float textWrapWidth = std::max(1.0f, available - iconGap - iconSize);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, false, textWrapWidth);
    const ImVec2 hitSize(std::min(available, labelSize.x + iconGap + iconSize),
                         std::max(labelSize.y, iconSize));
    const bool hovered = ImGui::IsWindowHovered() &&
                         ImGui::IsMouseHoveringRect(
                             start, ImVec2(start.x + hitSize.x, start.y + hitSize.y));
    const ImVec4 normalText = ThemeVec(state.config.theme.text);
    const ImVec4 disabledText = ThemeVec(state.config.theme.textDisabled);
    const float restingLinkContrast = std::clamp(restingContrast, 0.0f, 1.0f);
    const ImVec4 restingColor(
        disabledText.x + (normalText.x - disabledText.x) * restingLinkContrast,
        disabledText.y + (normalText.y - disabledText.y) * restingLinkContrast,
        disabledText.z + (normalText.z - disabledText.z) * restingLinkContrast,
        disabledText.w);
    const ImVec4 color = hovered ? normalText : restingColor;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWrapWidth);
    ImGui::TextWrapped("%s", label);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    const ImVec2 linkMin = ImGui::GetItemRectMin();
    const ImVec2 linkMax = ImGui::GetItemRectMax();
    const ImU32 ink = ImGui::ColorConvertFloat4ToU32(color);
    const float iconX = std::min(linkMin.x + labelSize.x + iconGap,
                                 start.x + available - iconSize);
    const float iconY = linkMin.y + std::max(0.0f, (labelSize.y - iconSize) * 0.5f);
    const ImVec2 topRight(iconX + iconSize, iconY);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(iconX, iconY + iconSize), topRight, ink, 1.25f);
    draw->AddLine(topRight, ImVec2(topRight.x - 4.0f, topRight.y), ink, 1.25f);
    draw->AddLine(topRight, ImVec2(topRight.x, topRight.y + 4.0f), ink, 1.25f);
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddLine(ImVec2(linkMin.x, linkMax.y),
                      ImVec2(std::min(linkMin.x + labelSize.x, linkMax.x), linkMax.y),
                      ink,
                      1.0f);
    }
    const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    ImGui::PopID();
    return clicked;
}
// JSON escaping and API-key protection compile in standalone domain/service components.

} // namespace squarestar::shell
