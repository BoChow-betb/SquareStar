#include "application/notification_channel.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace squarestar::application {

NativeNotificationChannel& NotificationChannel() noexcept {
    static NativeNotificationChannel channel;
    return channel;
}

void NativeNotificationChannel::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    notifications_.clear();
    priceMoveBatches_.clear();
    nextPriceMoveBatchId_ = 0;
}

void NativeNotificationChannel::Enqueue(NativeNotificationRequest request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request.groupId != 0) {
        for (auto it = notifications_.rbegin(); it != notifications_.rend(); ++it) {
            if (it->groupId != request.groupId)
                continue;
            *it = std::move(request);
            return;
        }
    }
    constexpr std::size_t kMaxQueuedNotifications = 16;
    if (notifications_.size() >= kMaxQueuedNotifications)
        notifications_.pop_front();
    notifications_.push_back(std::move(request));
}

std::optional<NativeNotificationRequest> NativeNotificationChannel::Take() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (notifications_.empty())
        return std::nullopt;
    NativeNotificationRequest request = std::move(notifications_.front());
    notifications_.pop_front();
    return request;
}

void NativeNotificationChannel::QueuePriceMove(
    StockMoveNotification row,
    bool batchNearbyMoves) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    PendingPriceMoveBatch* batch = nullptr;
    if (batchNearbyMoves && !priceMoveBatches_.empty()) {
        PendingPriceMoveBatch& candidate = priceMoveBatches_.back();
        const bool collecting =
            candidate.lastQueuedAt == std::chrono::steady_clock::time_point{} ||
            now - candidate.lastQueuedAt <= kNotificationGroupingWindow;
        const bool alreadyPresent =
            std::any_of(candidate.rows.begin(), candidate.rows.end(), [&](const auto& move) {
                return move.ticker == row.ticker;
            });
        if (collecting &&
            (alreadyPresent ||
             candidate.rows.size() < kNotificationMaxGroupedRows)) {
            batch = &candidate;
        }
    }
    if (!batch) {
        constexpr std::size_t kMaxPendingBatches = kNotificationMaxVisibleBlocks;
        if (priceMoveBatches_.size() >= kMaxPendingBatches)
            priceMoveBatches_.pop_front();
        priceMoveBatches_.push_back({});
        batch = &priceMoveBatches_.back();
        batch->id = ++nextPriceMoveBatchId_;
    }

    const auto existing =
        std::find_if(batch->rows.begin(), batch->rows.end(), [&](const auto& move) {
            return move.ticker == row.ticker;
        });
    if (existing != batch->rows.end()) {
        if (!std::isfinite(existing->before) || existing->before <= 0.0)
            existing->before = row.before;
        existing->after = row.after;
        existing->priceAlert = existing->priceAlert || row.priceAlert;
        if (row.priceAlert && std::isfinite(row.alertThreshold) && row.alertThreshold > 0.0)
            existing->alertThreshold = row.alertThreshold;
        if (!row.currency.empty())
            existing->currency = std::move(row.currency);
    } else {
        batch->rows.push_back(std::move(row));
    }
    batch->lastQueuedAt =
        batchNearbyMoves ? now : std::chrono::steady_clock::time_point{};
    ++batch->revision;
}

std::vector<NativeNotificationRequest>
NativeNotificationChannel::TakeReadyPriceMoveNotifications(
    bool backgroundNotificationsEnabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NativeNotificationRequest> ready;
    if (!backgroundNotificationsEnabled) {
        priceMoveBatches_.clear();
        return ready;
    }

    const auto now = std::chrono::steady_clock::now();
    while (!priceMoveBatches_.empty()) {
        PendingPriceMoveBatch& batch = priceMoveBatches_.front();
        const bool dirty = batch.revision != batch.publishedRevision;
        const bool batchReady =
            batch.rows.size() >= kNotificationMaxGroupedRows ||
            batch.lastQueuedAt == std::chrono::steady_clock::time_point{} ||
            now - batch.lastQueuedAt >= kNotificationGroupingWindow;


if (dirty) {
            auto text = FormatBackgroundStockMoves(batch.rows);
            std::string actionTicker;
            if (batch.rows.size() == 1)
                actionTicker = batch.rows.front().ticker;
            ready.push_back({std::move(text.title),
                             std::move(text.message),
                             std::move(text.accentText),
                             text.accentDirection,
                             batch.id,
                             {},
                             std::move(actionTicker)});
            batch.publishedRevision = batch.revision;
        }

        if (batchReady) {
            priceMoveBatches_.pop_front();
            continue;
        }


        break;
    }
    return ready;
}

double NativeNotificationChannel::SecondsUntilPriceMoveFlush() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (priceMoveBatches_.empty())
        return 30.0;

    const PendingPriceMoveBatch& batch = priceMoveBatches_.front();
    if (batch.revision != batch.publishedRevision ||
        batch.rows.size() >= kNotificationMaxGroupedRows ||
        batch.lastQueuedAt == std::chrono::steady_clock::time_point{}) {
        return 0.01;
    }
    const double groupingSeconds =
        std::chrono::duration<double>(kNotificationGroupingWindow).count();
    const double remaining =
        groupingSeconds -
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      batch.lastQueuedAt)
            .count();
    return std::clamp(remaining, 0.01, groupingSeconds);
}


}
