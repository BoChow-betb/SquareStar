#include "application/screener_cache.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace squarestar::application {
namespace {

struct ScreenerCacheEntry {
    std::vector<ScreenerItem> items;
    std::chrono::steady_clock::time_point fetchedAt;
    std::chrono::system_clock::time_point wallFetchedAt;
    std::size_t heapBytes = 0;
};

constexpr std::size_t kMaxScreenerCacheEntries = 3;
constexpr std::size_t kMaxScreenerCacheHeapBytes = 2 * 1024 * 1024;
constexpr auto kMaximumDisplayAge = std::chrono::minutes(10);

std::vector<ScreenerItem> CopyCacheableItems(
    const std::vector<ScreenerItem>& items) {
    std::vector<ScreenerItem> cachedItems;
    cachedItems.reserve(items.size());
    for (const ScreenerItem& item : items) {
        ScreenerItem cachedItem;
        static_cast<squarestar::market::ScreenerData&>(cachedItem) =
            static_cast<const squarestar::market::ScreenerData&>(item);
        cachedItems.push_back(std::move(cachedItem));
    }
    return cachedItems;
}

void InsertCacheEntryLocked(
    std::unordered_map<std::string, ScreenerCacheEntry>& entries,
    const std::string& cacheKey,
    ScreenerCacheEntry entry) {
    entries.erase(cacheKey);
    std::size_t cachedBytes = 0;
    for (const auto& cacheEntry : entries)
        cachedBytes += cacheEntry.second.heapBytes;
    while (!entries.empty() &&
           (entries.size() >= kMaxScreenerCacheEntries ||
            cachedBytes > kMaxScreenerCacheHeapBytes - entry.heapBytes)) {
        const auto oldest = std::min_element(
            entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.second.fetchedAt < b.second.fetchedAt;
            });
        if (oldest == entries.end())
            break;
        cachedBytes -= std::min(cachedBytes, oldest->second.heapBytes);
        entries.erase(oldest);
    }
    entries.insert_or_assign(cacheKey, std::move(entry));
}

}

struct ScreenerCache::Impl {
    mutable std::mutex mutex;
    std::unordered_map<std::string, ScreenerCacheEntry> entries;
};

ScreenerCache::ScreenerCache() : impl_(std::make_unique<Impl>()) {}

ScreenerCache::~ScreenerCache() = default;

std::string BuildScreenerCacheKey(const std::string& screenerId,
                                  const std::vector<std::string>& watchlist) {
    std::string cacheKey = screenerId;
    if (screenerId == "watchlist") {
        for (const auto& symbol : watchlist) {
            cacheKey.push_back(':');
            cacheKey += symbol;
        }
    }
    return cacheKey;
}

bool ScreenerCache::LoadForRefresh(const std::string& cacheKey,
                                   std::vector<ScreenerItem>& items) const {
    return LoadForRefresh(cacheKey, items, kMaximumDisplayAge);
}

bool ScreenerCache::LoadForRefresh(
    const std::string& cacheKey,
    std::vector<ScreenerItem>& items,
    std::chrono::steady_clock::duration maximumDisplayAge) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto cached = impl_->entries.find(cacheKey);
    if (cached == impl_->entries.end() || cached->second.items.empty() ||
        std::chrono::steady_clock::now() - cached->second.fetchedAt >=
            maximumDisplayAge) {
        return false;
    }
    items = cached->second.items;
    return true;
}

bool ScreenerCache::Store(
    const std::string& cacheKey,
    const std::vector<ScreenerItem>& items,
    std::chrono::system_clock::time_point wallFetchedAt) {
    if (items.empty())
        return false;
    std::vector<ScreenerItem> cachedItems = CopyCacheableItems(items);
    const std::size_t entryBytes = ApproximateScreenerItemsHeapBytes(cachedItems);
    if (entryBytes > kMaxScreenerCacheHeapBytes)
        return false;

    const auto wallNow = std::chrono::system_clock::now();
    if (wallFetchedAt == std::chrono::system_clock::time_point{} ||
        wallFetchedAt > wallNow)
        return false;
    const auto wallAge = wallNow - wallFetchedAt;
    const auto steadyAge = std::chrono::duration_cast<
        std::chrono::steady_clock::duration>(wallAge);
    ScreenerCacheEntry entry;
    entry.items = std::move(cachedItems);
    entry.fetchedAt = std::chrono::steady_clock::now() - steadyAge;
    entry.wallFetchedAt = wallFetchedAt;
    entry.heapBytes = entryBytes;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    InsertCacheEntryLocked(impl_->entries, cacheKey, std::move(entry));
    return true;
}

std::optional<std::chrono::system_clock::time_point>
ScreenerCache::UpdateTrends(const std::string& cacheKey,
                            const std::vector<ScreenerItem>& items) {
    if (items.empty())
        return std::nullopt;
    std::vector<ScreenerItem> cachedItems = CopyCacheableItems(items);
    const std::size_t entryBytes = ApproximateScreenerItemsHeapBytes(cachedItems);
    if (entryBytes > kMaxScreenerCacheHeapBytes)
        return std::nullopt;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto cached = impl_->entries.find(cacheKey);
    if (cached == impl_->entries.end()) {
        return std::nullopt;
    }
    ScreenerCacheEntry updated;
    updated.items = std::move(cachedItems);
    updated.fetchedAt = cached->second.fetchedAt;
    updated.wallFetchedAt = cached->second.wallFetchedAt;
    updated.heapBytes = entryBytes;
    const auto wallFetchedAt = updated.wallFetchedAt;
    InsertCacheEntryLocked(impl_->entries, cacheKey, std::move(updated));
    return wallFetchedAt;
}

std::size_t MergeScreenerRefreshData(const std::vector<ScreenerItem>& cached,
                                     std::vector<ScreenerItem>& live) {
    std::unordered_map<std::string_view, const ScreenerItem*> cachedBySymbol;
    cachedBySymbol.reserve(cached.size());
    for (const ScreenerItem& item : cached) {
        if (!item.symbol.empty())
            cachedBySymbol.insert_or_assign(item.symbol, &item);
    }

    std::size_t merged = 0;
    for (ScreenerItem& item : live) {
        const auto found = cachedBySymbol.find(item.symbol);
        if (found == cachedBySymbol.end())
            continue;
        const ScreenerItem& old = *found->second;
        if (item.name.empty())
            item.name = old.name;
        const auto fillMissing = []<typename Value>(Value& liveValue,
                                                     bool& liveHasValue,
                                                     const Value& cachedValue,
                                                     bool cachedHasValue) {
            if (!liveHasValue && cachedHasValue) {
                liveValue = cachedValue;
                liveHasValue = true;
            }
        };
        fillMissing(item.price, item.hasPrice, old.price, old.hasPrice);
        fillMissing(item.change, item.hasChange, old.change, old.hasChange);
        fillMissing(item.changePercent,
                    item.hasChangePercent,
                    old.changePercent,
                    old.hasChangePercent);
        fillMissing(item.volume, item.hasVolume, old.volume, old.hasVolume);
        fillMissing(item.avgVol3M, item.hasAvgVol3M, old.avgVol3M, old.hasAvgVol3M);
        fillMissing(item.marketCap,
                    item.hasMarketCap,
                    old.marketCap,
                    old.hasMarketCap);
        fillMissing(item.peRatio, item.hasPeRatio, old.peRatio, old.hasPeRatio);
        fillMissing(item.fiftyTwoWkChange,
                    item.hasFiftyTwoWkChange,
                    old.fiftyTwoWkChange,
                    old.hasFiftyTwoWkChange);
        if (item.lastMarketTime == 0)
            item.lastMarketTime = old.lastMarketTime;
        if (!item.resolved) {
            item.resolved = old.resolved;
            item.suspectedInactive = old.suspectedInactive;
        }
        if (item.sparkline.size() < 2 && old.sparkline.size() >= 2) {
            squarestar::market::ReplaceScreenerSparkline(item, old.sparkline);
            item.sparklineAttempted = old.sparklineAttempted;
        }
        ++merged;
    }
    return merged;
}

std::size_t MergeScreenerTrendData(const std::vector<ScreenerItem>& source,
                                   std::size_t firstIndex,
                                   std::size_t count,
                                   std::vector<ScreenerItem>& destination,
                                   bool includeAttemptedFailures) {
    if (source.empty() || destination.empty() || count == 0 || firstIndex >= source.size())
        return 0;
    const std::size_t end = std::min(source.size(), firstIndex + count);
    std::unordered_map<std::string_view, const ScreenerItem*> trendsBySymbol;
    trendsBySymbol.reserve(end - firstIndex);
    for (std::size_t index = firstIndex; index < end; ++index) {
        const ScreenerItem& item = source[index];
        if (!item.symbol.empty() &&
            (item.sparkline.size() >= 2 ||
             (includeAttemptedFailures && item.sparklineAttempted))) {
            trendsBySymbol.insert_or_assign(item.symbol, &item);
        }
    }

    std::size_t merged = 0;
    for (ScreenerItem& item : destination) {
        const auto trend = trendsBySymbol.find(item.symbol);
        if (trend == trendsBySymbol.end())
            continue;
        const ScreenerItem& sourceItem = *trend->second;
        bool changed = false;
        if (item.sparkline != sourceItem.sparkline) {
            squarestar::market::ReplaceScreenerSparkline(item, sourceItem.sparkline);
            changed = true;
        }
        if (item.sparklineAttempted != sourceItem.sparklineAttempted) {
            item.sparklineAttempted = sourceItem.sparklineAttempted;
            changed = true;
        }
        if (changed)
            ++merged;
    }
    return merged;
}

void ScreenerCache::Clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::unordered_map<std::string, ScreenerCacheEntry>().swap(impl_->entries);
}

}
