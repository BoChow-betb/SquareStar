#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "application/contextual_keybind_policy.hpp"
#include "application/notification_text.hpp"

namespace squarestar::application {

// Small render-only state objects keep notification lifetime and cleanup rules
// next to the data they govern. The shell decides *what* to show; these types
// only own transient presentation state.
struct TimedNoticeRenderState {
    std::chrono::steady_clock::time_point until{};
    float animation = 0.0f;

    [[nodiscard]] bool Holding(std::chrono::steady_clock::time_point now) const noexcept {
        return until != std::chrono::steady_clock::time_point{} && now < until;
    }

    void Clear() noexcept {
        until = {};
        animation = 0.0f;
    }
};

struct InteractionNoticeRenderState {
    std::string title;
    std::string body;
    std::string actionLabel;
    std::string actionUrl;
    std::string actionPath;
    TimedNoticeRenderState presentation;
    bool pointerDismissArmed = false;

    [[nodiscard]] bool Holding(std::chrono::steady_clock::time_point now) const noexcept {
        return !title.empty() && presentation.Holding(now);
    }

    void Clear() noexcept {
        title.clear();
        body.clear();
        actionLabel.clear();
        actionUrl.clear();
        actionPath.clear();
        presentation.Clear();
        pointerDismissArmed = false;
    }

    void Dismiss() noexcept {
        presentation.until = {};
        pointerDismissArmed = false;
    }
};

struct MarketMoveNotice {
    std::vector<StockMoveNotification> rows;
    std::string title;
    std::string priceText;
    std::string deltaText;
    int direction = 0;
    std::chrono::steady_clock::time_point until{};
    bool presentationStarted = false;
    bool pointerDismissArmed = false;
    float animation = 0.0f;
    std::uint64_t serial = 0;

    [[nodiscard]] bool Holding(std::chrono::steady_clock::time_point now) const noexcept {
        return !presentationStarted ||
               (until != std::chrono::steady_clock::time_point{} && now < until);
    }

    void MarkPresented(std::chrono::steady_clock::time_point now) noexcept {
        if (presentationStarted)
            return;
        presentationStarted = true;
        until = now + kNotificationHoldDuration;
    }

    void RefreshVisibleHold(std::chrono::steady_clock::time_point now) noexcept {
        if (presentationStarted && until != std::chrono::steady_clock::time_point{})
            until = now + kNotificationHoldDuration;
    }

    void Dismiss() noexcept {
        presentationStarted = true;
        until = {};
    }
};

struct MarketMoveRenderState {
    std::deque<MarketMoveNotice> notices;
    std::chrono::steady_clock::time_point lastQueuedAt{};
    std::uint64_t nextSerial = 0;

    void Clear() noexcept {
        notices.clear();
        lastQueuedAt = {};
        // Keep nextSerial monotonic for the process lifetime so an ImGui window
        // ID is never accidentally reused while old internal state may linger.
    }
};


struct NotificationCenterEntry {
    StockMoveNotification row;
    std::chrono::system_clock::time_point occurredAt{};
    float focusAnimation = 0.0f;
    std::uint64_t serial = 0;
};

struct NotificationCenterRenderState {
    std::deque<NotificationCenterEntry> entries;
    float panelAnimation = 0.0f;
    std::uint64_t nextSerial = 0;
    std::uint64_t focusedSerial = 0;
    float scrollTarget = 0.0f;
    bool scrollTargetInitialized = false;

    void ScrollToTop() noexcept {
        scrollTarget = 0.0f;
        scrollTargetInitialized = true;
    }

    void ClearPointerFocus() noexcept {
        focusedSerial = 0;
        for (auto& entry : entries)
            entry.focusAnimation = 0.0f;
    }

    void Push(StockMoveNotification row) {
        const auto now = std::chrono::system_clock::now();
        if (row.priceAlert) {
            const auto duplicate = std::find_if(entries.rbegin(), entries.rend(), [&](const auto& entry) {
                return entry.row.priceAlert && entry.row.ticker == row.ticker &&
                       now - entry.occurredAt <= std::chrono::seconds(2);
            });
            if (duplicate != entries.rend()) {
                auto duplicateIt = std::prev(duplicate.base());
                NotificationCenterEntry refreshed = std::move(*duplicateIt);
                entries.erase(duplicateIt);
                refreshed.row = std::move(row);
                refreshed.occurredAt = now;
                refreshed.focusAnimation = 0.0f;
                if (focusedSerial == refreshed.serial)
                    focusedSerial = 0;
                entries.push_back(std::move(refreshed));
                ScrollToTop();
                return;
            }
        }

        while (entries.size() >= kNotificationCenterMaxEntries) {
            if (focusedSerial == entries.front().serial)
                focusedSerial = 0;
            entries.pop_front();
        }

        NotificationCenterEntry entry;
        entry.row = std::move(row);
        entry.occurredAt = now;
        entry.serial = ++nextSerial;
        entries.push_back(std::move(entry));
        ScrollToTop();
    }

    bool Remove(std::uint64_t serial) {
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.serial == serial;
        });
        if (found == entries.end())
            return false;
        const bool removedFocused = focusedSerial == serial;
        entries.erase(found);
        if (removedFocused)
            focusedSerial = 0;
        return true;
    }

    [[nodiscard]] bool HasDismissibleEntries() const noexcept {
        return std::any_of(entries.begin(), entries.end(), [](const auto& entry) {
            return !entry.row.priceAlert;
        });
    }

    void ClearDismissible() noexcept {
        const bool removedFocused = std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.serial == focusedSerial && !entry.row.priceAlert;
        });
        std::erase_if(entries, [](const auto& entry) {
            return !entry.row.priceAlert;
        });
        if (entries.empty() || removedFocused)
            focusedSerial = 0;
        ScrollToTop();
    }

    bool RemovePriceAlertForTicker(std::string_view ticker) noexcept {
        const bool removedFocused = std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.serial == focusedSerial && entry.row.priceAlert && entry.row.ticker == ticker;
        });
        const std::size_t oldSize = entries.size();
        std::erase_if(entries, [&](const auto& entry) {
            return entry.row.priceAlert && entry.row.ticker == ticker;
        });
        if (entries.size() == oldSize)
            return false;
        if (entries.empty() || removedFocused)
            focusedSerial = 0;
        ScrollToTop();
        return true;
    }

    void Clear() noexcept {
        entries.clear();
        focusedSerial = 0;
        ScrollToTop();
    }
};

struct FirstFetchWarmupRenderState {
    bool pending = false;
    TimedNoticeRenderState presentation;

    void Clear() noexcept {
        pending = false;
        presentation.Clear();
    }
};

struct MonitorModeHintRenderState {
    TimedNoticeRenderState presentation;
    bool dismissed = false;

    void ClearPresentation() noexcept { presentation.Clear(); }
};

struct ContextualKeybindHintRenderState {
    ContextualKeybindSurface surface = ContextualKeybindSurface::Uninitialized;
    ContextualKeybindSurface context = ContextualKeybindSurface::None;
    std::chrono::steady_clock::time_point showAt{};
    TimedNoticeRenderState presentation;

    void ClearActive() noexcept {
        context = ContextualKeybindSurface::None;
        showAt = {};
        presentation.Clear();
    }

    void ResetSurface() noexcept {
        surface = ContextualKeybindSurface::None;
        ClearActive();
    }
};

struct NotificationRenderState {
    MonitorModeHintRenderState monitorModeHint;
    TimedNoticeRenderState marketOpen;
    FirstFetchWarmupRenderState firstFetchWarmup;
    InteractionNoticeRenderState interaction;
    MarketMoveRenderState marketMoves;
    NotificationCenterRenderState notificationCenter;
    ContextualKeybindHintRenderState keybindHint;

    // LiteGUI only shows price-alert and market-move cards. Drop the FullGUI
    // card state when switching modes so old cards do not reappear later.
    void ClearLiteGuiSuppressed() noexcept {
        interaction.Clear();
        marketOpen.Clear();
        firstFetchWarmup.Clear();
        monitorModeHint.ClearPresentation();
        keybindHint.ResetSurface();
    }

    // Hidden/minimized windows cannot show foreground cards. Keep the first-fetch
    // warmup pending until the normal GUI is visible again.
    void ClearUnavailableForeground() noexcept {
        interaction.Clear();
        marketOpen.Clear();
        marketMoves.Clear();
        monitorModeHint.ClearPresentation();
        keybindHint.ClearActive();
    }
};

} // namespace squarestar::application
