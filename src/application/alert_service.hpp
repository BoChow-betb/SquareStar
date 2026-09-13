#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "application/app_limits.hpp"
#include "application/stock_context.hpp"

namespace squarestar::alerts {

struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

inline constexpr float kPriceAlertEnterSeconds = 0.32f;
inline constexpr float kPriceAlertHoldSeconds = 7.0f;
inline constexpr float kPriceAlertExitSeconds = 0.36f;
inline constexpr float kPriceAlertTotalSeconds =
    kPriceAlertEnterSeconds + kPriceAlertHoldSeconds + kPriceAlertExitSeconds;

struct PriceAlertToast {
    std::string ticker;
    double price = 0.0;
    double threshold = 0.0;
    std::chrono::steady_clock::time_point startedAt{};
};

inline float PriceAlertToastElapsedSeconds(
    const PriceAlertToast& toast,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept {
    if (toast.startedAt == std::chrono::steady_clock::time_point{})
        return 0.0f;
    const float elapsed = std::chrono::duration<float>(now - toast.startedAt).count();
    return elapsed < 0.0f ? 0.0f : elapsed;
}

inline bool PriceAlertToastNeedsContinuousRedraw(
    const PriceAlertToast& toast,
    bool animationsEnabled,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept {
    if (toast.startedAt == std::chrono::steady_clock::time_point{})
        return true;
    if (!animationsEnabled)
        return false;
    const float elapsed = PriceAlertToastElapsedSeconds(toast, now);
    return elapsed < kPriceAlertEnterSeconds ||
           (elapsed > kPriceAlertEnterSeconds + kPriceAlertHoldSeconds &&
            elapsed < kPriceAlertTotalSeconds);
}

inline bool ShouldPresentMarketOpenNotice(bool stateInitialized,
                                          bool previouslyOpen,
                                          bool currentlyOpen) noexcept {
    return currentlyOpen && (!stateInitialized || !previouslyOpen);
}

struct MarketMoveTriggerResult {
    bool dailyThreshold = false;
    bool fiftyTwoWeekExtreme = false;
    bool stateChange = false;

    [[nodiscard]] bool Any() const noexcept {
        return dailyThreshold || fiftyTwoWeekExtreme || stateChange;
    }
};

inline bool CrossedPriceReference(double before, double after, double reference) noexcept {
    if (!std::isfinite(before) || !std::isfinite(after) || !std::isfinite(reference) ||
        before <= 0.0 || after <= 0.0 || reference <= 0.0)
        return false;
    const double beforeDelta = before - reference;
    const double afterDelta = after - reference;
    return (beforeDelta < 0.0 && afterDelta >= 0.0) ||
           (beforeDelta > 0.0 && afterDelta <= 0.0);
}

inline MarketMoveTriggerResult EvaluateMarketMoveTriggers(double before,
                                                           double after,
                                                           double previousClose,
                                                           double openPrice,
                                                           double fiftyTwoWeekHigh,
                                                           double fiftyTwoWeekLow,
                                                           double thresholdPct,
                                                           bool include52WeekEvents,
                                                           bool includeStateChanges) noexcept {
    MarketMoveTriggerResult result;
    if (!std::isfinite(before) || !std::isfinite(after) || before <= 0.0 || after <= 0.0)
        return result;

    if (thresholdPct <= 0.0) {
        result.dailyThreshold = std::abs(after - before) > 1e-12;
    } else if (std::isfinite(previousClose) && previousClose > 0.0) {
        const double threshold = std::clamp(std::abs(thresholdPct), 0.1, 100.0);
        const double beforePct = (before - previousClose) / previousClose * 100.0;
        const double afterPct = (after - previousClose) / previousClose * 100.0;
        result.dailyThreshold =
            (beforePct < threshold && afterPct >= threshold) ||
            (beforePct > -threshold && afterPct <= -threshold);
    }

    if (include52WeekEvents) {
        const bool newHigh = std::isfinite(fiftyTwoWeekHigh) && fiftyTwoWeekHigh > 0.0 &&
                             before < fiftyTwoWeekHigh && after >= fiftyTwoWeekHigh;
        const bool newLow = std::isfinite(fiftyTwoWeekLow) && fiftyTwoWeekLow > 0.0 &&
                            before > fiftyTwoWeekLow && after <= fiftyTwoWeekLow;
        result.fiftyTwoWeekExtreme = newHigh || newLow;
    }

    if (includeStateChanges) {
        result.stateChange = CrossedPriceReference(before, after, previousClose) ||
                             CrossedPriceReference(before, after, openPrice);
    }
    return result;
}

// Owns alert configuration, presentation state, and background monitor
// contexts. Containers stay private so threshold, silence, mute, queue-size,
// and cleanup invariants cannot be bypassed by callers.
class AlertService {
public:
    using ThresholdMap = std::map<std::string, double, std::less<>>;
    using MuteMap = std::map<std::string, int64_t, std::less<>>;
    using MonitorContexts =
        std::list<std::unique_ptr<squarestar::application::StockContext>>;

    [[nodiscard]] const ThresholdMap& Thresholds() const noexcept {
        return priceAlerts_;
    }

    [[nodiscard]] std::uint64_t ThresholdRevision() const noexcept {
        return thresholdRevision_;
    }

    [[nodiscard]] bool HasThreshold(std::string_view ticker) const {
        return priceAlerts_.contains(ticker);
    }

    [[nodiscard]] std::optional<double> Threshold(std::string_view ticker) const {
        const auto found = priceAlerts_.find(ticker);
        if (found == priceAlerts_.end())
            return std::nullopt;
        return found->second;
    }

    [[nodiscard]] std::optional<double> ActiveThreshold(std::string_view ticker) const {
        const auto threshold = Threshold(ticker);
        return threshold && std::isfinite(*threshold) && *threshold > 0.0
                   ? threshold
                   : std::nullopt;
    }

    bool SetThreshold(std::string ticker, double threshold) {
        if (ticker.empty() || !std::isfinite(threshold) || threshold <= 0.0)
            return false;
        const auto existing = priceAlerts_.find(ticker);
        if (existing == priceAlerts_.end() &&
            priceAlerts_.size() >= squarestar::application::kMaxPriceAlerts)
            return false;
        const bool changed = existing == priceAlerts_.end() ||
                             std::islessgreater(existing->second, threshold);
        ClearSilence(ticker);
        priceAlerts_.insert_or_assign(std::move(ticker), threshold);
        if (changed)
            AdvanceThresholdRevision();
        return true;
    }

    void ClearThresholds() {
        const bool changed = !priceAlerts_.empty();
        priceAlerts_.clear();
        ClearMutesAndSilences();
        if (changed)
            AdvanceThresholdRevision();
    }

    bool RemoveThreshold(std::string_view ticker) {
        const auto alert = priceAlerts_.find(ticker);
        const bool removed = alert != priceAlerts_.end();
        if (removed)
            priceAlerts_.erase(alert);
        ClearSilence(ticker);
        if (removed)
            AdvanceThresholdRevision();
        return removed;
    }

    [[nodiscard]] bool IsSilenced(std::string_view ticker) const {
        return silencedPriceAlerts_.contains(ticker);
    }

    void Silence(std::string ticker) {
        // An untimed silence supersedes any older settlement mute. Otherwise
        // ExpireMutes would later erase this newly requested silence as if it
        // still belonged to the timed entry.
        priceAlertMutedUntil_.erase(ticker);
        silencedPriceAlerts_.insert(std::move(ticker));
    }

    void SilenceUntil(std::string ticker, int64_t expiry) {
        silencedPriceAlerts_.insert(ticker);
        priceAlertMutedUntil_.insert_or_assign(std::move(ticker), expiry);
    }

    [[nodiscard]] bool IsMutedUntil(std::string_view ticker, int64_t now) const {
        const auto found = priceAlertMutedUntil_.find(ticker);
        return found != priceAlertMutedUntil_.end() && found->second > now;
    }

    [[nodiscard]] const MuteMap& Mutes() const noexcept {
        return priceAlertMutedUntil_;
    }

    void ClearMutesAndSilences() {
        priceAlertMutedUntil_.clear();
        silencedPriceAlerts_.clear();
    }

    std::vector<std::string> ExpireMutes(int64_t now) {
        std::vector<std::string> expired;
        for (auto it = priceAlertMutedUntil_.begin(); it != priceAlertMutedUntil_.end();) {
            if (it->second > now) {
                ++it;
                continue;
            }
            expired.push_back(it->first);
            silencedPriceAlerts_.erase(it->first);
            it = priceAlertMutedUntil_.erase(it);
        }
        return expired;
    }

    void ClearSilence(std::string_view ticker) {
        if (const auto silence = silencedPriceAlerts_.find(ticker);
            silence != silencedPriceAlerts_.end())
            silencedPriceAlerts_.erase(silence);
        if (const auto mute = priceAlertMutedUntil_.find(ticker);
            mute != priceAlertMutedUntil_.end())
            priceAlertMutedUntil_.erase(mute);
    }

    void ClearSilencedFlag(std::string_view ticker) {
        if (const auto silence = silencedPriceAlerts_.find(ticker);
            silence != silencedPriceAlerts_.end())
            silencedPriceAlerts_.erase(silence);
    }

    [[nodiscard]] std::size_t ToastCount() const noexcept {
        return priceAlertToasts_.size();
    }

    [[nodiscard]] PriceAlertToast* ToastAt(std::size_t index) noexcept {
        return index < priceAlertToasts_.size() ? &priceAlertToasts_[index] : nullptr;
    }

    [[nodiscard]] const PriceAlertToast* ToastAt(std::size_t index) const noexcept {
        return index < priceAlertToasts_.size() ? &priceAlertToasts_[index] : nullptr;
    }

    void ClearToasts() noexcept { priceAlertToasts_.clear(); }

    void RemoveToastsForTicker(std::string_view ticker) {
        std::erase_if(priceAlertToasts_, [&](const PriceAlertToast& toast) {
            return toast.ticker == ticker;
        });
    }

    bool UpsertToast(std::string ticker,
                     double price,
                     double threshold,
                     std::size_t maximumQueuedAlerts) {
        const auto found = std::find_if(priceAlertToasts_.begin(),
                                        priceAlertToasts_.end(),
                                        [&](const PriceAlertToast& toast) {
                                            return toast.ticker == ticker;
                                        });
        if (found != priceAlertToasts_.end()) {
            found->price = price;
            found->threshold = threshold;
            return true;
        }
        if (maximumQueuedAlerts == 0)
            return false;
        while (priceAlertToasts_.size() >= maximumQueuedAlerts)
            priceAlertToasts_.pop_front();
        priceAlertToasts_.push_back(
            {std::move(ticker), price, threshold, {}});
        return true;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point
    AlertPresentationNotBefore() const noexcept {
        return alertPresentationNotBefore_;
    }

    void SetAlertPresentationNotBefore(
        std::chrono::steady_clock::time_point value) noexcept {
        alertPresentationNotBefore_ = value;
    }

    [[nodiscard]] bool MarketOpenStateInitialized() const noexcept {
        return marketOpenStateInitialized_;
    }

    [[nodiscard]] bool LastMarketOpenState() const noexcept {
        return lastMarketOpenState_;
    }

    void SetMarketOpenState(bool initialized, bool open) noexcept {
        marketOpenStateInitialized_ = initialized;
        lastMarketOpenState_ = open;
    }

    [[nodiscard]] bool MarketOpenSoundPending() const noexcept {
        return marketOpenSoundPending_;
    }

    void SetMarketOpenSoundPending(bool pending) noexcept {
        marketOpenSoundPending_ = pending;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point
    NextMarketOpenCheckAt() const noexcept {
        return nextMarketOpenCheckAt_;
    }

    void SetNextMarketOpenCheckAt(
        std::chrono::steady_clock::time_point value) noexcept {
        nextMarketOpenCheckAt_ = value;
    }

    [[nodiscard]] bool IsMarketMoveCoolingDown(
        std::string_view ticker,
        std::chrono::steady_clock::time_point now) const {
        const auto found = marketMoveCooldownUntil_.find(ticker);
        return found != marketMoveCooldownUntil_.end() && now < found->second;
    }

    void SetMarketMoveCooldown(
        std::string ticker,
        std::chrono::steady_clock::time_point until) {
        marketMoveCooldownUntil_.insert_or_assign(std::move(ticker), until);
    }

    void ClearMarketMoveCooldown(std::string_view ticker) {
        marketMoveCooldownUntil_.erase(std::string(ticker));
    }

    void ClearMarketMoveCooldowns() noexcept {
        marketMoveCooldownUntil_.clear();
    }

    void PruneMarketMoveCooldowns(
        std::chrono::steady_clock::time_point now,
        std::size_t maximumEntries) {
        if (marketMoveCooldownUntil_.size() <= maximumEntries)
            return;
        std::erase_if(marketMoveCooldownUntil_, [&](const auto& entry) {
            return entry.second <= now;
        });
    }

    [[nodiscard]] const MonitorContexts& Monitors() const noexcept {
        return priceAlertContexts_;
    }

    void AddMonitor(std::unique_ptr<squarestar::application::StockContext> context) {
        if (context)
            priceAlertContexts_.push_back(std::move(context));
    }

    template <typename Predicate>
    void RemoveMonitorsIf(Predicate&& predicate) {
        for (auto it = priceAlertContexts_.begin(); it != priceAlertContexts_.end();) {
            if (predicate(*it))
                it = priceAlertContexts_.erase(it);
            else
                ++it;
        }
    }

private:
    void AdvanceThresholdRevision() noexcept {
        ++thresholdRevision_;
        if (thresholdRevision_ == 0)
            ++thresholdRevision_;
    }

    ThresholdMap priceAlerts_;
    std::uint64_t thresholdRevision_ = 1;
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>>
        silencedPriceAlerts_;
    MuteMap priceAlertMutedUntil_;
    std::deque<PriceAlertToast> priceAlertToasts_;
    std::chrono::steady_clock::time_point alertPresentationNotBefore_{};
    bool marketOpenStateInitialized_ = false;
    bool lastMarketOpenState_ = false;
    bool marketOpenSoundPending_ = false;
    std::chrono::steady_clock::time_point nextMarketOpenCheckAt_{};
    std::unordered_map<std::string,
                       std::chrono::steady_clock::time_point,
                       TransparentStringHash,
                       std::equal_to<>> marketMoveCooldownUntil_;
    MonitorContexts priceAlertContexts_;
};

} // namespace squarestar::alerts
