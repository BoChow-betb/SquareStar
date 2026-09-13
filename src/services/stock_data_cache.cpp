#include "services/stock_data_cache.hpp"

#include "services/stock_data_service.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace squarestar::marketdata {
namespace {

struct StockCacheEntry {
    std::string key;
    squarestar::market::StockFetchResult result;
    std::chrono::steady_clock::time_point savedAt;
};

std::mutex g_stockCacheMutex;
// Cache one immediate duplicate open. Large history vectors otherwise stay
// with their active context.
std::optional<StockCacheEntry> g_stockCache;

} // namespace

std::optional<squarestar::market::StockFetchResult> ConsumeStockMemoryCache(
    std::string_view key,
    bool activeSession) {
    std::lock_guard<std::mutex> lock(g_stockCacheMutex);
    const auto now = std::chrono::steady_clock::now();
    const auto ttl = std::chrono::seconds(activeSession ? 3 : 30);
    if (g_stockCache && now - g_stockCache->savedAt >= ttl)
        g_stockCache.reset();
    if (!g_stockCache || g_stockCache->key != key)
        return std::nullopt;
    auto result = std::move(g_stockCache->result);
    g_stockCache.reset();
    return result;
}

void StoreStockMemoryCache(
    std::string_view key,
    const squarestar::market::StockFetchResult& result) {
    constexpr std::size_t maxHeapBytes = 128 * 1024;
    std::lock_guard<std::mutex> lock(g_stockCacheMutex);
    g_stockCache.reset();
    if (squarestar::market::ApproximateStockDataHeapBytes(result.marketData) <=
        maxHeapBytes) {
        g_stockCache.emplace(
            StockCacheEntry{std::string(key), result, std::chrono::steady_clock::now()});
    }
}

void ClearStockMemoryCache() {
    std::lock_guard<std::mutex> lock(g_stockCacheMutex);
    g_stockCache.reset();
}

} // namespace squarestar::marketdata
