#include "modules/notification_center.hpp"

#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/theme_profiles.hpp"
#include "application/ui_animation.hpp"
#include "domain/number_format.hpp"
#include "modules/core.hpp"
#include "modules/currency_display.hpp"
#include "modules/ui_focus.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <iterator>
#include <string>

namespace squarestar::shell {
namespace {

using squarestar::application::AppState;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;

constexpr float kNotificationCenterCardNormalInset = 10.0f;
constexpr float kNotificationCenterCardSidePadding = 16.0f;
constexpr float kNotificationCenterCardTopPadding = 13.0f;
constexpr float kNotificationCenterCardTitleBodyGap = 10.0f;
constexpr float kNotificationCenterCardBodyDetailGap = 6.0f;
constexpr float kNotificationCenterCardBottomPadding = 16.0f;
constexpr float kNotificationCenterCardFocusWidthGrowth = 12.0f;
constexpr float kNotificationCenterCardFocusHeightGrowth = 8.0f;
constexpr float kNotificationCenterCardGap = 9.0f;
constexpr float kNotificationCenterListFocusGuard = 4.0f;
constexpr float kNotificationCenterHeaderHeight = 49.0f;
constexpr float kNotificationCenterListHeightReserve =
    kNotificationCenterHeaderHeight + 16.0f;

struct NotificationCenterCardMetrics {
    ImFont* titleFont = nullptr;
    ImFont* bodyFont = nullptr;
    float titleFontSize = 0.0f;
    float bodyFontSize = 0.0f;
    float titleLineHeight = 0.0f;
    float bodyLineHeight = 0.0f;
    float bodyYLocal = 0.0f;
    float detailYLocal = 0.0f;
    float baseHeight = 0.0f;
};

NotificationCenterCardMetrics MeasureNotificationCenterCard(const AppState& state) {
    NotificationCenterCardMetrics metrics;
    metrics.titleFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    metrics.bodyFont = state.render.fontNormal ? state.render.fontNormal : ImGui::GetFont();
    metrics.titleFontSize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    metrics.bodyFontSize =
        state.render.fontNormal ? state.config.theme.fontBody : ImGui::GetFontSize();
    metrics.titleLineHeight =
        metrics.titleFont
            ->CalcTextSizeA(metrics.titleFontSize,
                            std::numeric_limits<float>::max(),
                            0.0f,
                            "Ag")
            .y;
    metrics.bodyLineHeight =
        metrics.bodyFont
            ->CalcTextSizeA(metrics.bodyFontSize,
                            std::numeric_limits<float>::max(),
                            0.0f,
                            "Ag")
            .y;
    metrics.bodyYLocal = kNotificationCenterCardTopPadding +
                         metrics.titleLineHeight +
                         kNotificationCenterCardTitleBodyGap;
    metrics.detailYLocal = metrics.bodyYLocal +
                           metrics.bodyLineHeight +
                           kNotificationCenterCardBodyDetailGap;
    metrics.baseHeight = metrics.detailYLocal +
                         metrics.bodyLineHeight +
                         kNotificationCenterCardBottomPadding;
    return metrics;
}

std::string NotificationAgeLabel(std::chrono::system_clock::time_point occurredAt) {
    if (occurredAt == std::chrono::system_clock::time_point{})
        return "Now";
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::max<std::int64_t>(
        0,
        std::chrono::duration_cast<std::chrono::seconds>(now - occurredAt).count());
    if (seconds < 60)
        return "Now";
    if (seconds < 60 * 60)
        return std::to_string(seconds / 60) + "m";
    if (seconds < 24 * 60 * 60)
        return std::to_string(seconds / (60 * 60)) + "h";
    return std::to_string(seconds / (24 * 60 * 60)) + "d";
}

struct NotificationCenterCardResult {
    bool selected = false;
    bool dismissed = false;
};

void DrawTrashIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float half = size * 0.5f;
    const float stroke = std::max(1.6f, size * 0.105f);

    const float handleWidth = size * 0.28f;
    const float handleHeight = size * 0.14f;
    const float handleTop = center.y - half * 0.88f;
    const float handleBottom = handleTop + handleHeight;

    const float lidLeft = center.x - half * 0.82f;
    const float lidRight = center.x + half * 0.82f;
    const float lidTop = center.y - half * 0.60f;
    const float lidBottom = center.y - half * 0.26f;

    const float bodyTopY = center.y - half * 0.12f;
    const float bodyBottomY = center.y + half * 0.78f;
    const float bodyTopHalfWidth = size * 0.32f;
    const float bodyBottomHalfWidth = size * 0.25f;
    const float bodyBottomRounding = size * 0.13f;

    draw->AddLine(ImVec2(center.x - handleWidth * 0.5f, handleBottom),
                  ImVec2(center.x + handleWidth * 0.5f, handleBottom),
                  color,
                  stroke);
    draw->AddLine(ImVec2(center.x - handleWidth * 0.34f, handleTop),
                  ImVec2(center.x + handleWidth * 0.34f, handleTop),
                  color,
                  stroke);

    draw->AddLine(ImVec2(lidLeft, lidBottom),
                  ImVec2(lidRight, lidBottom),
                  color,
                  stroke);
    draw->AddLine(ImVec2(lidLeft + size * 0.10f, lidTop),
                  ImVec2(lidRight - size * 0.10f, lidTop),
                  color,
                  stroke);

    const ImVec2 bodyTopLeft(center.x - bodyTopHalfWidth, bodyTopY);
    const ImVec2 bodyTopRight(center.x + bodyTopHalfWidth, bodyTopY);
    const ImVec2 bodyBottomLeft(center.x - bodyBottomHalfWidth, bodyBottomY);
    const ImVec2 bodyBottomRight(center.x + bodyBottomHalfWidth, bodyBottomY);

    draw->AddLine(bodyTopLeft, bodyBottomLeft, color, stroke);
    draw->AddLine(bodyTopRight, bodyBottomRight, color, stroke);
    draw->AddRect(ImVec2(bodyBottomLeft.x, bodyBottomY - bodyBottomRounding * 0.5f),
                  ImVec2(bodyBottomRight.x, bodyBottomY),
                  color,
                  bodyBottomRounding,
                  ImDrawFlags_RoundCornersBottom,
                  stroke);
}

void DrawCloseIcon(ImDrawList* draw, ImVec2 center, float size, ImU32 color) {
    const float half = size * 0.5f;
    const float stroke = std::max(1.5f, size * 0.17f);
    draw->AddLine(ImVec2(center.x - half * 0.52f, center.y - half * 0.52f),
                  ImVec2(center.x + half * 0.52f, center.y + half * 0.52f),
                  color,
                  stroke);
    draw->AddLine(ImVec2(center.x + half * 0.52f, center.y - half * 0.52f),
                  ImVec2(center.x - half * 0.52f, center.y + half * 0.52f),
                  color,
                  stroke);
}

NotificationCenterCardResult RenderNotificationCenterCard(
    AppState& state,
    squarestar::application::NotificationCenterEntry& entry,
    float availableWidth) {
    auto& center = state.render.notifications.notificationCenter;

    constexpr float dismissButtonSize = 22.0f;
    constexpr float dismissRightInset = 11.0f;
    constexpr float dismissHitPadding = 8.0f;

    const auto& row = entry.row;
    const std::string title =
        std::string(row.priceAlert ? "[Alert] " : "[Market move] ") + row.ticker;
    const std::string age = NotificationAgeLabel(entry.occurredAt);
    std::string body;
    std::string detail;

    if (row.priceAlert) {
        double displayAfter = 0.0;
        double displayThreshold = 0.0;
        const bool displayAfterReady =
            TryConvertUsdForDisplay(state, row.after, displayAfter);
        const bool displayThresholdReady =
            TryConvertUsdForDisplay(state, row.alertThreshold, displayThreshold);
        const std::string currency(DisplayCurrencyCode(state));
        body = "Current price  " +
               (displayAfterReady ? squarestar::format::FormatDouble(displayAfter)
                                  : std::string("...")) +
               " " + currency;
        detail = "Alert level  " +
                 (displayThresholdReady
                      ? squarestar::format::FormatDouble(displayThreshold)
                      : std::string("...")) +
                 " " + currency + " or below";
    } else {
        body = squarestar::application::FormatStockMovePrices(row.before, row.after, row.currency);
        detail = squarestar::application::FormatStockMoveDelta(row.before, row.after);
    }

    const NotificationCenterCardMetrics metrics = MeasureNotificationCenterCard(state);

    // Hover focus is pointer-owned, not state-owned. Probe against the card's
    // current animated bounds first, then animate toward the pointer state.
    // This prevents a notification from remaining enlarged after the cursor
    // leaves while still allowing the hover scale to ease smoothly.
    const float previousFocus = std::clamp(entry.focusAnimation, 0.0f, 1.0f);
    const float probeCardWidth =
        std::max(220.0f,
                 availableWidth -
                     kNotificationCenterCardNormalInset * 2.0f +
                     previousFocus * kNotificationCenterCardFocusWidthGrowth);
    const float probeXOffset =
        std::max(0.0f, (availableWidth - probeCardWidth) * 0.5f);
    const ImVec2 probeOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 probeMin(probeOrigin.x + probeXOffset, probeOrigin.y);
    const ImVec2 probeMax(
        probeMin.x + probeCardWidth,
        probeMin.y + metrics.baseHeight +
            previousFocus * kNotificationCenterCardFocusHeightGrowth);
    const bool listWindowHovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool pointerInsideCard =
        listWindowHovered && ImGui::IsMouseHoveringRect(probeMin, probeMax, false);
    const float focusTarget = pointerInsideCard ? 1.0f : 0.0f;
    const float focusBlend = state.UiAnimationsEnabled()
                                 ? std::clamp(UiFrameDelta() * 14.0f, 0.0f, 1.0f)
                                 : 1.0f;
    entry.focusAnimation += (focusTarget - entry.focusAnimation) * focusBlend;
    entry.focusAnimation = std::clamp(entry.focusAnimation, 0.0f, 1.0f);
    if (std::abs(entry.focusAnimation - focusTarget) > 0.001f)
        RequestGuiRedraw();

    const float focus = entry.focusAnimation;
    const float cardWidth =
        std::max(220.0f,
                 availableWidth -
                     kNotificationCenterCardNormalInset * 2.0f +
                     focus * kNotificationCenterCardFocusWidthGrowth);
    const float cardHeight =
        metrics.baseHeight + focus * kNotificationCenterCardFocusHeightGrowth;

    const float xOffset = std::max(0.0f, (availableWidth - cardWidth) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset);
    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
    const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);

    ImGui::PushID(static_cast<int>(entry.serial & 0x7fffffffULL));

    const bool showDismissButton = !row.priceAlert;
    ImVec2 dismissMin{};
    ImVec2 dismissMax{};
    ImVec2 dismissCenter{};
    if (showDismissButton) {
        dismissMin =
            ImVec2(cardMax.x - dismissRightInset - dismissButtonSize, cardMin.y + 9.0f);
        dismissMax =
            ImVec2(dismissMin.x + dismissButtonSize, dismissMin.y + dismissButtonSize);
        dismissCenter = ImVec2((dismissMin.x + dismissMax.x) * 0.5f,
                               (dismissMin.y + dismissMax.y) * 0.5f);
    }

    const float cardActionWidth =
        showDismissButton
            ? std::max(120.0f,
                       cardWidth -
                           (dismissButtonSize + dismissRightInset + dismissHitPadding))
            : cardWidth;
    const bool cardPressed =
        ImGui::InvisibleButton("##NotificationCard", ImVec2(cardActionWidth, cardHeight));
    const bool cardActionHovered = ImGui::IsItemHovered();

    bool dismissPressed = false;
    bool dismissHovered = false;
    if (showDismissButton) {
        ImGui::SetCursorScreenPos(dismissMin);
        dismissPressed =
            ImGui::InvisibleButton("##DismissNotificationCard",
                                   ImVec2(dismissButtonSize, dismissButtonSize));
        dismissHovered = ImGui::IsItemHovered();
    }

    const bool hoveredVisual =
        cardActionHovered ||
        (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
         ImGui::IsMouseHoveringRect(cardMin, cardMax, false));
    const bool hovered = hoveredVisual || dismissHovered;
    if (hovered) {
        if (center.focusedSerial != entry.serial) {
            center.focusedSerial = entry.serial;
            RequestGuiRedraw();
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    } else if (center.focusedSerial == entry.serial) {
        center.focusedSerial = 0;
        RequestGuiRedraw();
    }

    const bool light =
        squarestar::application::IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 background = light ? ImVec4(0.975f, 0.975f, 0.98f, 0.995f)
                                    : ImVec4(0.055f, 0.055f, 0.06f, 0.985f);
    const ImVec4 border = light ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                                : ImVec4(0.34f, 0.34f, 0.36f, 1.0f);
    const ImVec4 titleColor = light ? ImVec4(0.08f, 0.08f, 0.09f, 1.0f)
                                    : ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
    const ImVec4 dimColor = light ? ImVec4(0.36f, 0.36f, 0.39f, 1.0f)
                                  : ImVec4(0.66f, 0.66f, 0.69f, 1.0f);
    const ImU32 backgroundU32 = ImGui::ColorConvertFloat4ToU32(background);
    const ImU32 borderU32 = ImGui::ColorConvertFloat4ToU32(border);
    const ImU32 titleU32 = ImGui::ColorConvertFloat4ToU32(titleColor);
    const ImU32 dimU32 = ImGui::ColorConvertFloat4ToU32(dimColor);
    const float rounding = UiRounding(state, 10.0f + focus * 1.5f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (focus > 0.01f) {
        const ImVec4 glow = light ? ImVec4(0.0f, 0.0f, 0.0f, 0.10f * focus)
                                  : ImVec4(1.0f, 1.0f, 1.0f, 0.08f * focus);
        draw->AddRectFilled(ImVec2(cardMin.x - 2.0f * focus, cardMin.y - 2.0f * focus),
                            ImVec2(cardMax.x + 2.0f * focus, cardMax.y + 2.0f * focus),
                            ImGui::ColorConvertFloat4ToU32(glow),
                            rounding + 2.0f);
    }
    draw->AddRectFilled(cardMin, cardMax, backgroundU32, rounding);
    draw->AddRect(cardMin,
                  cardMax,
                  borderU32,
                  rounding,
                  ImDrawFlags_RoundCornersAll,
                  1.0f + focus * 0.7f);

    const float left = cardMin.x + kNotificationCenterCardSidePadding;
    const float titleY = cardMin.y + kNotificationCenterCardTopPadding;
    draw->AddText(metrics.titleFont,
                  metrics.titleFontSize,
                  ImVec2(left, titleY),
                  titleU32,
                  title.c_str());

    const float ageRightInset = showDismissButton ? 44.0f : 16.0f;
    const ImVec2 ageSize =
        metrics.bodyFont->CalcTextSizeA(metrics.bodyFontSize,
                                        std::numeric_limits<float>::max(),
                                        0.0f,
                                        age.c_str());
    draw->AddText(metrics.bodyFont,
                  metrics.bodyFontSize,
                  ImVec2(cardMax.x - ageRightInset - ageSize.x, titleY + 1.0f),
                  dimU32,
                  age.c_str());

    if (showDismissButton) {
        const ImU32 dismissFill = ImGui::ColorConvertFloat4ToU32(
            dismissHovered ? ThemeVec(state.config.theme.searchHover, 0.95f)
                           : (light ? ImVec4(0.90f, 0.90f, 0.93f, 0.92f)
                                    : ImVec4(0.13f, 0.13f, 0.15f, 0.92f)));
        const ImU32 dismissBorder = ImGui::ColorConvertFloat4ToU32(
            ThemeVec(dismissHovered ? state.config.theme.text
                                    : state.config.theme.textDisabled,
                     dismissHovered ? 0.45f : 0.28f));
        const ImU32 dismissIcon = ImGui::ColorConvertFloat4ToU32(
            ThemeVec(dismissHovered ? state.config.theme.text
                                    : state.config.theme.textDisabled));
        draw->AddRectFilled(dismissMin,
                            dismissMax,
                            dismissFill,
                            UiRounding(state, 6.0f));
        draw->AddRect(dismissMin,
                      dismissMax,
                      dismissBorder,
                      UiRounding(state, 6.0f),
                      ImDrawFlags_RoundCornersAll,
                      1.0f);
        DrawCloseIcon(draw, dismissCenter, 10.0f, dismissIcon);
    }

    ImU32 detailColor = dimU32;
    if (row.priceAlert) {
        detailColor =
            ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.negative));
    } else if (!detail.empty()) {
        detailColor = ImGui::ColorConvertFloat4ToU32(
            ThemeVec(row.after >= row.before ? state.config.theme.positive
                                             : state.config.theme.negative));
    }
    draw->AddText(metrics.bodyFont,
                  metrics.bodyFontSize,
                  ImVec2(left, cardMin.y + metrics.bodyYLocal + focus * 2.0f),
                  titleU32,
                  body.c_str());
    if (!detail.empty()) {
        draw->AddText(metrics.bodyFont,
                      metrics.bodyFontSize,
                      ImVec2(left, cardMin.y + metrics.detailYLocal + focus * 3.0f),
                      detailColor,
                      detail.c_str());
    }

    // The dismiss button is the last ImGui item submitted inside the card.
    // Reset the cursor to the real bottom of the animated card so the next list
    // item stacks below the full hover geometry. A zero-size item satisfies
    // Dear ImGui's parent-boundary accounting without introducing extra gap.
    ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y));
    const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x, 0.0f));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopStyleVar();

    ImGui::PopID();

    return {
        .selected = cardPressed && !dismissPressed,
        .dismissed = dismissPressed,
    };
}

std::string RenderNotificationCenterContents(
    AppState& state,
    float width,
    float height,
    bool snapToTop) {
    auto& center = state.render.notifications.notificationCenter;
    std::string selectedTicker;
    constexpr float inset = 14.0f;

    // Keep the full-size panel child transparent so the popup window's own
    // rounded background remains visible at the outer edges.
    ImGui::BeginChild("NotificationCenterPanel",
                      ImVec2(width, height),
                      false,
                      ImGuiWindowFlags_NoBackground |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    constexpr float headerTitleY = 13.0f;
    ImGui::SetCursorPos(ImVec2(inset + 3.0f, headerTitleY));
    ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
    ImGui::TextUnformatted("Notifications");
    ImGui::PopFont();

    if (center.HasDismissibleEntries()) {
        constexpr float clearButtonSize = 28.0f;
        ImGui::SetCursorPos(ImVec2(width - inset - clearButtonSize - 2.0f, 12.0f));
        const bool clearPressed = ImGui::InvisibleButton(
            "##ClearNotifications",
            ImVec2(clearButtonSize, clearButtonSize));
        const bool clearHovered = ImGui::IsItemHovered();
        if (clearHovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 clearMin = ImGui::GetItemRectMin();
        const ImVec2 clearMax = ImGui::GetItemRectMax();
        const ImVec2 clearCenter((clearMin.x + clearMax.x) * 0.5f,
                                 (clearMin.y + clearMax.y) * 0.5f + 1.2f);
        if (clearHovered) {
            draw->AddRectFilled(clearMin,
                                clearMax,
                                ImGui::ColorConvertFloat4ToU32(
                                    ThemeVec(state.config.theme.searchHover, 0.9f)),
                                UiRounding(state, 6.0f));
        }
        const ImU32 clearColor = ImGui::ColorConvertFloat4ToU32(
            ThemeVec(clearHovered ? state.config.theme.text
                                  : state.config.theme.textDisabled));
        DrawTrashIcon(draw, clearCenter, 20.0f, clearColor);
        if (clearPressed) {
            center.ClearDismissible();
            PlayUISound("click.wav", state);
            RequestGuiRedraw();
        }
    }

    const ImU32 divider = ImGui::ColorConvertFloat4ToU32(
        ThemeVec(state.config.theme.border, 0.55f));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(ImGui::GetWindowPos().x + inset,
               ImGui::GetWindowPos().y + kNotificationCenterHeaderHeight),
        ImVec2(ImGui::GetWindowPos().x + width - inset,
               ImGui::GetWindowPos().y + kNotificationCenterHeaderHeight),
        divider,
        1.0f);

    ImGui::SetCursorPos(ImVec2(inset, kNotificationCenterHeaderHeight + 9.0f));
    const float listHeight =
        std::max(60.0f, height - kNotificationCenterListHeightReserve);
    ImGui::BeginChild("##NotificationCenterList",
                      ImVec2(width - inset * 2.0f, listHeight),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (snapToTop) {
        ImGui::SetScrollY(0.0f);
        center.ScrollToTop();
    }

    if (center.entries.empty()) {
        ImGui::SetCursorPosY(std::max(12.0f, listHeight * 0.38f));
        const char* emptyText = "No notifications yet";
        const float textWidth = ImGui::CalcTextSize(emptyText).x;
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - textWidth) * 0.5f));
        ImGui::TextDisabled("%s", emptyText);
        center.ScrollToTop();
    } else {
        // Keep a small clip guard above and below the stack so the animated
        // border/glow never gets sliced by the child window, especially in
        // the higher-contrast light theme.
        ImGui::SetCursorPosY(kNotificationCenterListFocusGuard);
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        std::uint64_t dismissedSerial = 0;
        for (auto it = center.entries.rbegin(); it != center.entries.rend(); ++it) {
            auto& entry = *it;
            ImGui::SetCursorPosX(0.0f);
            const NotificationCenterCardResult result =
                RenderNotificationCenterCard(state, entry, contentWidth);
            if (result.dismissed && dismissedSerial == 0) {
                dismissedSerial = entry.serial;
            } else if (result.selected && dismissedSerial == 0) {
                center.focusedSerial = entry.serial;
                selectedTicker = entry.row.ticker;
            }
            if (std::next(it) != center.entries.rend()) {
                const ImVec2 gapItemSpacing = ImGui::GetStyle().ItemSpacing;
                ImGui::PushStyleVar(
                    ImGuiStyleVar_ItemSpacing, ImVec2(gapItemSpacing.x, 0.0f));
                ImGui::Dummy(ImVec2(0.0f, kNotificationCenterCardGap));
                ImGui::PopStyleVar();
            }
        }

        const ImVec2 listItemSpacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing, ImVec2(listItemSpacing.x, 0.0f));
        ImGui::Dummy(ImVec2(0.0f, kNotificationCenterListFocusGuard));
        ImGui::PopStyleVar();

        if (dismissedSerial != 0 && center.Remove(dismissedSerial)) {
            PlayUISound("click.wav", state);
            RequestGuiRedraw();
        }

        const bool listHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (listHovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
            if (!center.scrollTargetInitialized) {
                center.scrollTarget = ImGui::GetScrollY();
                center.scrollTargetInitialized = true;
            }
            center.scrollTarget -= ImGui::GetIO().MouseWheel * 118.0f;
        }
        const float maxScroll = ImGui::GetScrollMaxY();
        if (!center.scrollTargetInitialized) {
            center.scrollTarget = ImGui::GetScrollY();
            center.scrollTargetInitialized = true;
        }
        center.scrollTarget = std::clamp(center.scrollTarget, 0.0f, maxScroll);
        const float currentScroll = ImGui::GetScrollY();
        const float scrollBlend = state.UiAnimationsEnabled()
                                      ? std::clamp(UiFrameDelta() * 13.0f, 0.0f, 1.0f)
                                      : 1.0f;
        const float nextScroll = currentScroll +
                                 (center.scrollTarget - currentScroll) * scrollBlend;
        ImGui::SetScrollY(nextScroll);
        if (std::abs(center.scrollTarget - nextScroll) > 0.35f)
            RequestGuiRedraw();
    }
    ImGui::EndChild();
    ImGui::EndChild();
    return selectedTicker;
}

} // namespace

void RenderNotificationCenterMenu(
    AppState& state,
    const char* idSuffix,
    float buttonSize,
    float buttonSpacing,
    int visibleCardLimit,
    const std::function<void(const std::string&)>& onExecute) {
    ImGui::SameLine(0, buttonSpacing);
    const ImVec2 bellMin = ImGui::GetCursorScreenPos();
    const ImVec2 bellMax(bellMin.x + buttonSize,
                         bellMin.y + buttonSize);
    std::array<char, 128> buttonId{};
    std::array<char, 128> menuId{};
    std::snprintf(buttonId.data(), buttonId.size(), "##NotificationsBtn_%s", idSuffix);
    std::snprintf(menuId.data(), menuId.size(), "NotificationsMenu_%s", idSuffix);

    ImGui::InvisibleButton(buttonId.data(), ImVec2(buttonSize, buttonSize));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    auto& center = state.render.notifications.notificationCenter;
    const bool hasNotifications = !center.entries.empty();
    bool openedThisFrame = false;
    if (ImGui::IsItemClicked()) {
        if (!hasNotifications) {
            state.navigation.notificationCenterOpen = false;
            center.panelAnimation = 0.0f;
            PublishUserFeedback(
                state,
                squarestar::application::UserFeedbackType::Warning,
                "No notifications",
                "Notification list is empty.");
            RequestGuiRedraw();
        } else {
            if (state.navigation.watchlistOpen) {
                state.navigation.watchlistOpen = false;
                CloseAllAnimatedFloatingMenus();
            }
            PlayUISound(state.navigation.liteGuiActive ? "click.wav" : "transition.wav", state);
            state.navigation.notificationCenterOpen = true;
            center.panelAnimation = 0.0f;
            center.ClearPointerFocus();
            ImGui::OpenPopup(menuId.data());
            openedThisFrame = true;
        }
    }

    const ImU32 bellColor = ImGui::ColorConvertFloat4ToU32(
        ThemeVec(hovered ? state.config.theme.text : state.config.theme.textDisabled));
    DrawIcon(ImGui::GetWindowDrawList(),
             ImVec2(bellMin.x + buttonSize * 0.5f,
                    bellMin.y + buttonSize * 0.5f),
             buttonSize * 0.5f,
             16,
             bellColor);
    if (hasNotifications) {
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(bellMax.x - 7.0f, bellMin.y + 7.0f),
            3.5f,
            ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.danger)));
    }
    DrawObjectFocusOutline(
        state, bellMin, bellMax, hovered || state.navigation.notificationCenterOpen, 3);

    if (!ImGui::IsPopupOpen(menuId.data())) {
        state.navigation.notificationCenterOpen = false;
        center.panelAnimation = 0.0f;
        center.ClearPointerFocus();
        return;
    }

    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    if (!viewport)
        viewport = ImGui::GetMainViewport();
    const float width = std::clamp(viewport->WorkSize.x * 0.30f, 340.0f, 430.0f);
    const float maxHeight =
        std::max(220.0f, std::min(580.0f, viewport->WorkSize.y - 52.0f));
    const std::size_t maxVisibleCards =
        static_cast<std::size_t>(std::max(1, visibleCardLimit));
    const std::size_t visibleCards =
        std::min(center.entries.size(), maxVisibleCards);

    // Size the panel from the active font metrics instead of a fixed card
    // stride. Reserve the full hover growth plus clip guards, so every visible
    // card remains completely contained even while it is zoomed/focused.
    const NotificationCenterCardMetrics cardMetrics =
        MeasureNotificationCenterCard(state);
    const float focusedCardHeight =
        cardMetrics.baseHeight + kNotificationCenterCardFocusHeightGrowth;
    const float visibleStackHeight =
        kNotificationCenterListFocusGuard * 2.0f +
        static_cast<float>(visibleCards) * focusedCardHeight +
        static_cast<float>(visibleCards > 0 ? visibleCards - 1 : 0) *
            kNotificationCenterCardGap;
    const float desiredHeight =
        kNotificationCenterListHeightReserve + visibleStackHeight;
    const float height = std::clamp(desiredHeight, 220.0f, maxHeight);
    const float targetX = std::clamp(
        bellMax.x - width,
        viewport->WorkPos.x + 10.0f,
        viewport->WorkPos.x + viewport->WorkSize.x - width - 10.0f);
    float targetY = bellMax.y + 8.0f;
    if (targetY + height > viewport->WorkPos.y + viewport->WorkSize.y - 10.0f)
        targetY = bellMin.y - height - 8.0f;
    const ImVec2 menuPosition(targetX, targetY);

    if (state.navigation.notificationCenterOpen) {
        const float animationTarget = 1.0f;
        const float animationBlend = state.UiAnimationsEnabled()
                                         ? std::clamp(UiFrameDelta() * 15.0f, 0.0f, 1.0f)
                                         : 1.0f;
        center.panelAnimation +=
            (animationTarget - center.panelAnimation) * animationBlend;
        if (std::abs(animationTarget - center.panelAnimation) > 0.001f)
            RequestGuiRedraw();
    } else {
        center.panelAnimation = 0.0f;
    }
    center.panelAnimation = std::clamp(center.panelAnimation, 0.0f, 1.0f);
    const float panelEase = squarestar::application::EaseOutCubic(center.panelAnimation);
    const float panelYOffset = (1.0f - panelEase) * -8.0f;

    // A real ImGui modal supplies the dim layer at render time, behind the
    // notification panel but above every application window. The previous
    // full-screen helper window was submitted too early in some layouts and
    // could dim the wrong layer.
    ImGui::SetNextWindowPos(ImVec2(menuPosition.x, menuPosition.y + panelYOffset),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
#ifdef IMGUI_HAS_VIEWPORT
    ImGui::SetNextWindowViewport(viewport->ID);
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.01f, panelEase));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiRounding(state, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiRounding(state, 12.0f));
    const ImVec4 background = ThemeVec(state.config.theme.floatingBg);
    const ImVec4 dimBackground = squarestar::application::IsLightGuiTheme(state.config.themeModeIndex)
                                     ? ImVec4(0.0f, 0.0f, 0.0f, 0.34f)
                                     : ImVec4(0.0f, 0.0f, 0.0f, 0.52f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBackground);

    std::string selectedTicker;
    const ImGuiWindowFlags modalFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginPopupModal(menuId.data(), nullptr, modalFlags)) {
        const ImVec2 popupMin = ImGui::GetWindowPos();
        const ImVec2 popupMax(popupMin.x + ImGui::GetWindowWidth(),
                              popupMin.y + ImGui::GetWindowHeight());
        DrawObjectFocusOutline(state, bellMin, bellMax, true, 3);
        DrawObjectFocusOutline(state, popupMin, popupMax, true, 3);

        selectedTicker = RenderNotificationCenterContents(
            state, width, height, openedThisFrame);

        const ImVec2 mouse = ImGui::GetMousePos();
        const bool clickedOutside =
            !openedThisFrame &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            (mouse.x < popupMin.x || mouse.x > popupMax.x ||
             mouse.y < popupMin.y || mouse.y > popupMax.y);
        const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool selectionMade = !selectedTicker.empty();

        if (center.entries.empty() ||
            !state.navigation.notificationCenterOpen ||
            clickedOutside || escapePressed || selectionMade) {
            state.navigation.notificationCenterOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    state.navigation.notificationCenterOpen = ImGui::IsPopupOpen(menuId.data());
    if (!state.navigation.notificationCenterOpen) {
        center.panelAnimation = 0.0f;
        center.ClearPointerFocus();
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);

    if (!selectedTicker.empty()) {
        PlayUISound("click.wav", state);
        onExecute(selectedTicker);
        RequestGuiRedraw();
    }
}

} // namespace squarestar::shell
