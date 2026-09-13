#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace squarestar::search {

struct SymbolSearchHttpResponse {
    std::string body;
    long statusCode = 0;
    bool success = false;
};

struct SymbolSearchDependencies {
    std::function<std::string()> getApiKey;
    std::function<std::string(std::string_view)> urlEncode;
    std::function<SymbolSearchHttpResponse(std::string)> fetch;
};

class SymbolSearchService {
  public:
    using Match = std::pair<std::string, std::string>;
    using Results = std::vector<Match>;

    explicit SymbolSearchService(SymbolSearchDependencies dependencies);

    Results Lookup(std::string_view query, bool bypassCache = false);
    void ClearCache();

  private:
    struct CacheEntry {
        Results matches;
        std::chrono::steady_clock::time_point savedAt;
        std::size_t heapBytes = 0;
    };

    static constexpr std::size_t kMaxCacheEntries = 24;
    static constexpr std::size_t kMaxCacheHeapBytes = 128 * 1024;
    static constexpr auto kCacheTtl = std::chrono::minutes(5);
    static constexpr auto kRateLimitCooldown = std::chrono::seconds(15);

    std::optional<Results> LookupCached(std::string_view normalizedQuery,
                                        std::chrono::steady_clock::time_point now);
    void StoreCache(std::string normalizedQuery,
                    Results results,
                    std::chrono::steady_clock::time_point now);
    void PruneExpired(std::chrono::steady_clock::time_point now);

    SymbolSearchDependencies dependencies_;
    std::mutex mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::chrono::steady_clock::time_point providerCooldownUntil_{};
};

// Application entry points backed by the process-wide search service.
SymbolSearchService::Results LookupSymbols(std::string_view query);
void ClearSymbolSearchCache();

} // namespace squarestar::search
