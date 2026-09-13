#include "application/alert_service.hpp"
#include "application/app_limits.hpp"
#include "application/app_state.hpp"
#include "application/price_alert_policy.hpp"
#include "application/notification_text.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

bool NearlyEqual(double left, double right) {
    return std::abs(left - right) < 0.000001;
}

template <typename Value>
const Value& RequireNotNull(const Value* value, const char* message) {
    if (value != nullptr)
        return *value;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using squarestar::alerts::AlertService;

    AlertService alerts;
    Require(!alerts.SetThreshold("", 10.0), "empty ticker is rejected");
    Require(!alerts.SetThreshold("AAPL", 0.0), "zero threshold is rejected");
    Require(!alerts.SetThreshold("AAPL", std::numeric_limits<double>::quiet_NaN()),
            "non-finite threshold is rejected");
    Require(alerts.Thresholds().empty(), "rejected thresholds are not stored");

    alerts.SilenceUntil("AAPL", 200);
    Require(alerts.IsSilenced("AAPL"), "ticker silence is recorded");
    Require(alerts.IsMutedUntil("AAPL", 100), "ticker mute is active");
    Require(alerts.SetThreshold("AAPL", 185.25), "valid threshold is stored");
    Require(!alerts.IsSilenced("AAPL"), "threshold update clears ticker silence");
    Require(!alerts.IsMutedUntil("AAPL", 100), "threshold update clears ticker mute");
    Require(NearlyEqual(alerts.ActiveThreshold("AAPL").value_or(0.0), 185.25),
            "active threshold is returned");

    alerts.SilenceUntil("AAPL", 90);
    const auto expired = alerts.ExpireMutes(100);
    Require(expired == std::vector<std::string>{"AAPL"}, "expired mute is reported");
    Require(!alerts.IsSilenced("AAPL"), "expired mute clears ticker silence");

    Require(alerts.UpsertToast("AAPL", 180.0, 185.25, 2), "first toast is queued");
    Require(alerts.UpsertToast("MSFT", 420.0, 400.0, 2), "second toast is queued");
    Require(alerts.UpsertToast("NVDA", 130.0, 125.0, 2), "toast queue remains bounded");
    Require(alerts.ToastCount() == 2, "toast queue exposes its bounded size");
    const auto& frontToast =
        RequireNotNull(alerts.ToastAt(0), "toast queue exposes its front item");
    Require(frontToast.ticker == "MSFT", "oldest toast is evicted");
    Require(alerts.UpsertToast("MSFT", 421.0, 400.0, 2), "existing toast is updated");
    const auto& updatedToast =
        RequireNotNull(alerts.ToastAt(0), "updated toast remains queued");
    Require(NearlyEqual(updatedToast.price, 421.0),
            "toast update stores the latest price");
    Require(alerts.ToastAt(2) == nullptr, "toast lookup rejects an out-of-range index");

    alerts.ClearToasts();
    for (std::size_t index = 0;
         index <= squarestar::application::kNotificationMaxVisibleBlocks;
         ++index) {
        Require(alerts.UpsertToast("QUEUE" + std::to_string(index),
                                   100.0 + static_cast<double>(index),
                                   90.0,
                                   squarestar::application::kNotificationMaxVisibleBlocks),
                "five-card alert queue accepts and bounds incoming alerts");
    }
    Require(alerts.ToastCount() == squarestar::application::kNotificationMaxVisibleBlocks,
            "price-alert queue is capped at five visible cards");
    const auto& boundedFront =
        RequireNotNull(alerts.ToastAt(0), "bounded toast queue keeps a front item");
    Require(boundedFront.ticker == "QUEUE1",
            "sixth alert evicts the oldest card from the five-card queue");

    Require(alerts.RemoveThreshold("AAPL"), "existing threshold is removed");
    Require(!alerts.HasThreshold("AAPL"), "removed threshold is absent");
    Require(!alerts.RemoveThreshold("AAPL"), "missing threshold removal is reported");

    const std::uint64_t revisionBeforeLimitTest = alerts.ThresholdRevision();
    for (std::size_t index = 0;
         index < squarestar::application::kMaxPriceAlerts;
         ++index) {
        Require(alerts.SetThreshold("LIMIT" + std::to_string(index),
                                    10.0 + static_cast<double>(index)),
                "threshold capacity accepts every allowed item");
    }
    Require(alerts.Thresholds().size() == squarestar::application::kMaxPriceAlerts,
            "threshold storage reaches its configured capacity");
    Require(!alerts.SetThreshold("ONE_TOO_MANY", 42.0),
            "threshold storage rejects an item beyond capacity");
    Require(alerts.SetThreshold("LIMIT0", 99.0),
            "existing threshold remains editable at capacity");
    Require(alerts.Thresholds().size() == squarestar::application::kMaxPriceAlerts,
            "threshold update does not change storage size");
    Require(alerts.ThresholdRevision() > revisionBeforeLimitTest,
            "threshold mutations advance the revision");

    {
        squarestar::application::AppState state;
        Require(state.alerts.SetThreshold("AAPL", 180.0),
                "alert removal fixture stores threshold");
        Require(state.alerts.UpsertToast("AAPL", 175.0, 180.0, 5),
                "alert removal fixture stores toast");
        squarestar::application::StockMoveNotification alertHistoryRow;
        alertHistoryRow.ticker = "AAPL";
        alertHistoryRow.after = 175.0;
        alertHistoryRow.priceAlert = true;
        alertHistoryRow.alertThreshold = 180.0;
        alertHistoryRow.currency = "USD";
        state.render.notifications.notificationCenter.Push(alertHistoryRow);

        auto active = std::make_unique<squarestar::application::StockContext>("AAPL");
        active->alerts.priceAlertTriggered = true;
        active->alerts.priceAlertSoundPlaysRemaining = 2;
        auto* activePtr = active.get();
        state.marketData.activeContexts.push_back(std::move(active));

        auto retired = std::make_unique<squarestar::application::StockContext>("AAPL");
        retired->alerts.priceAlertTriggered = true;
        retired->alerts.priceAlertSoundPlaysRemaining = 1;
        auto* retiredPtr = retired.get();
        state.marketData.retiredLiteContexts.push_back(std::move(retired));

        state.alerts.AddMonitor(
            std::make_unique<squarestar::application::StockContext>("AAPL"));

        Require(squarestar::application::RemoveConfiguredPriceAlert(state, "AAPL"),
                "central alert removal reports existing threshold");
        Require(!state.alerts.HasThreshold("AAPL"),
                "central alert removal clears threshold");
        Require(state.alerts.ToastCount() == 0,
                "central alert removal clears visible toast");
        Require(state.render.notifications.notificationCenter.entries.empty(),
                "central alert removal clears protected notification-center history");
        Require(!activePtr->alerts.priceAlertTriggered &&
                    activePtr->alerts.priceAlertSoundPlaysRemaining == 0,
                "central alert removal resets active alert state");
        Require(!retiredPtr->alerts.priceAlertTriggered &&
                    retiredPtr->alerts.priceAlertSoundPlaysRemaining == 0,
                "central alert removal resets retired alert state");
        Require(state.alerts.Monitors().empty(),
                "central alert removal clears background monitor");
        Require(!squarestar::application::RemoveConfiguredPriceAlert(state, "AAPL"),
                "central alert removal reports already-cleared threshold");
    }

    std::cout << "AlertService invariants passed\n";
    return 0;
}
