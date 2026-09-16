#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "application/notification_text.hpp"

namespace squarestar::application {

struct NativeNotificationRequest {
    std::string title;
    std::string message;
    std::string accentText;
    int accentDirection = 0;
    std::uint64_t groupId = 0;
    std::string actionPath;
    std::string actionTicker;
};


class NativeNotificationChannel final {
  public:
    void Clear();
    void Enqueue(NativeNotificationRequest request);
    std::optional<NativeNotificationRequest> Take();

    void QueuePriceMove(StockMoveNotification row,
                        bool batchNearbyMoves);
    std::vector<NativeNotificationRequest> TakeReadyPriceMoveNotifications(
        bool backgroundNotificationsEnabled);
    double SecondsUntilPriceMoveFlush() const;

  private:
    struct PendingPriceMoveBatch {
        std::vector<StockMoveNotification> rows;
        std::chrono::steady_clock::time_point lastQueuedAt{};
        std::uint64_t id = 0;
        std::uint64_t revision = 0;
        std::uint64_t publishedRevision = 0;
    };

    mutable std::mutex mutex_;
    std::deque<NativeNotificationRequest> notifications_;
    std::deque<PendingPriceMoveBatch> priceMoveBatches_;
    std::uint64_t nextPriceMoveBatchId_ = 0;
};

NativeNotificationChannel& NotificationChannel() noexcept;

}
