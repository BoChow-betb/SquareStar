#include "services/symbol_search_service.hpp"

#include "application/debug_diagnostics.hpp"

#include "domain/futures_catalog.hpp"
#include "domain/market_symbol.hpp"
#include "domain/symbol_search.hpp"
#include "services/json_access.hpp"
#include "services/secret_protection.hpp"

#include <algorithm>
#include <unordered_set>

namespace squarestar::search {
namespace {

using squarestar::json::JsonString;
using squarestar::json::ParseJsonInSitu;

std::string Upper(std::string_view value) {
    std::string normalized(value);
    squarestar::text::UppercaseInPlace(normalized);
    return normalized;
}

std::size_t EstimateResultsHeapBytes(const SymbolSearchService::Results& results) {
    std::size_t bytes = results.capacity() * sizeof(SymbolSearchService::Match);
    for (const auto& [symbol, description] : results)
        bytes += symbol.size() + description.size();
    return bytes;
}

struct RankedResult {
    std::string symbol;
    std::string description;
    int score = 0;
};

void SeedBuiltinResults(const QueryInfo& query,
                        const SymbolSearchService::Results& builtinResults,
                        std::vector<RankedResult>& ranked,
                        std::unordered_set<std::string>& seen) {
    for (const auto& [symbol, description] : builtinResults) {
        const std::string normalized = Upper(symbol);
        seen.insert(normalized);
        const int score = RankMatch(query, symbol, description, "Futures", 0);
        if (score > 0)
            ranked.push_back({symbol, description, score + 800});
    }
}

SymbolSearchService::Results FinalizeRankedResults(
    std::string_view query,
    std::vector<RankedResult> ranked) {
    const size_t resultCount = std::min<std::size_t>(10, ranked.size());
    std::partial_sort(ranked.begin(),
                      ranked.begin() + static_cast<std::ptrdiff_t>(resultCount),
                      ranked.end(),
                      [](const RankedResult& left, const RankedResult& right) {
                          if (left.score != right.score)
                              return left.score > right.score;
                          if (left.symbol.size() != right.symbol.size())
                              return left.symbol.size() < right.symbol.size();
                          return left.symbol < right.symbol;
                      });

    SymbolSearchService::Results results;
    results.reserve(resultCount);
    for (size_t i = 0; i < resultCount; ++i)
        results.emplace_back(std::move(ranked[i].symbol),
                             std::move(ranked[i].description));
    RemoveAmbiguousPlaceholderMatches(query, results);
    return results;
}

bool UnsupportedSearchType(std::string_view type) {
    const std::string normalizedType = Upper(type);
    return normalizedType.find("WARRANT") != std::string::npos ||
           normalizedType.find("RIGHT") != std::string::npos ||
           normalizedType.find("UNIT") != std::string::npos ||
           normalizedType.find("CRYPTO") != std::string::npos ||
           normalizedType.find("FOREX") != std::string::npos ||
           normalizedType.find("CURRENCY") != std::string::npos ||
           normalizedType.find("INDEX") != std::string::npos ||
           normalizedType.find("OPTION") != std::string::npos;
}

std::optional<SymbolSearchService::Results> ParseYahooSearchPayload(
    const QueryInfo& query,
    std::string& body,
    const SymbolSearchService::Results& builtinResults) {
    auto document = ParseJsonInSitu(body);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* quoteArray = root && yyjson_is_obj(root) ? yyjson_obj_get(root, "quotes") : nullptr;
    if (!quoteArray || !yyjson_is_arr(quoteArray))
        return std::nullopt;

    std::vector<RankedResult> ranked;
    std::unordered_set<std::string> seen;
    SeedBuiltinResults(query, builtinResults, ranked, seen);

    size_t index = 0;
    size_t count = 0;
    yyjson_val* item = nullptr;
    yyjson_arr_foreach(quoteArray, index, count, item) {
        std::string symbol;
        std::string shortName;
        std::string longName;
        std::string quoteType;
        if (!JsonString(item, "symbol", symbol) || symbol.empty())
            continue;
        JsonString(item, "shortname", shortName);
        JsonString(item, "longname", longName);
        JsonString(item, "quoteType", quoteType);
        if (UnsupportedSearchType(quoteType))
            continue;

        const auto supportedSymbol = squarestar::market::MarketSymbol::Parse(symbol);
        if (!supportedSymbol || !supportedSymbol->HasSupportedUsClassSuffix())
            continue;
        symbol = supportedSymbol->Canonical();
        const std::string normalizedSymbol = Upper(symbol);
        if (!seen.insert(normalizedSymbol).second)
            continue;

        std::string description = !longName.empty() ? std::move(longName) : std::move(shortName);
        if (description.empty())
            description = symbol;
        if (IsPlaceholderSymbolDescription(symbol, description))
            continue;

        const int score =
            RankMatch(query, symbol, description, quoteType, index);
        if (score > 0)
            ranked.push_back({std::move(symbol), std::move(description), score});
    }

    return FinalizeRankedResults(query.text, std::move(ranked));
}

} // namespace

SymbolSearchService::SymbolSearchService(SymbolSearchDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

SymbolSearchService::Results SymbolSearchService::Lookup(std::string_view query,
                                                         bool bypassCache) {
    if (query.empty())
        return {};

    const QueryInfo parsed = ParseQuery(query);
    Results builtinResults = squarestar::market::SearchCommonYahooFutures(parsed);
    if (!dependencies_.urlEncode || !dependencies_.fetch)
        return builtinResults;

    const std::string normalizedQuery = Upper(query);
    const auto now = std::chrono::steady_clock::now();
    if (!bypassCache) {
        if (std::optional<Results> cached = LookupCached(normalizedQuery, now))
            return std::move(*cached);
    }

    const auto lookupYahoo = [&]() -> std::optional<Results> {
        std::string url =
            "https://query1.finance.yahoo.com/v1/finance/search?q=" +
            dependencies_.urlEncode(query) +
            "&quotesCount=20&newsCount=0&listsCount=0&enableFuzzyQuery=true"
            "&region=US&lang=en-US";
        SymbolSearchHttpResponse response;
        try {
            response = dependencies_.fetch(std::move(url));
        } catch (...) {
            squarestar::application::ReportBackgroundFailure("symbol search yahoo provider");
            return std::nullopt;
        }
        if (!response.success || response.body.empty())
            return std::nullopt;
        try {
            return ParseYahooSearchPayload(parsed, response.body, builtinResults);
        } catch (...) {
            squarestar::application::ReportBackgroundFailure("symbol search yahoo payload");
            return std::nullopt;
        }
    };

    const auto yahooFallback = [&]() -> Results {
        if (std::optional<Results> yahooResults = lookupYahoo()) {
            StoreCache(normalizedQuery, *yahooResults, std::chrono::steady_clock::now());
            return std::move(*yahooResults);
        }
        return builtinResults;
    };

    if (!dependencies_.getApiKey)
        return yahooFallback();

    std::string apiKey = dependencies_.getApiKey();
    const squarestar::secrets::ScopedSecureClear clearApiKey(apiKey);
    if (apiKey.empty())
        return yahooFallback();

    bool providerCooldownActive = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providerCooldownActive = now < providerCooldownUntil_;
    }
    if (providerCooldownActive)
        return yahooFallback();

    std::string encodedApiKey = dependencies_.urlEncode(apiKey);
    const squarestar::secrets::ScopedSecureClear clearEncodedApiKey(encodedApiKey);
    std::string url =
        "https://finnhub.io/api/v1/search?q=" + dependencies_.urlEncode(query) +
        "&exchange=US&token=" + encodedApiKey;
    const squarestar::secrets::ScopedSecureClear clearUrl(url);
    SymbolSearchHttpResponse response;
    try {
        response = dependencies_.fetch(url);
    } catch (...) {
        squarestar::application::ReportBackgroundFailure("symbol search provider");
        return yahooFallback();
    }
    if (!response.success || response.body.empty()) {
        if (response.statusCode == 429) {
            std::lock_guard<std::mutex> lock(mutex_);
            providerCooldownUntil_ = std::max(providerCooldownUntil_, now + kRateLimitCooldown);
        }
        return yahooFallback();
    }

    try {
        auto document = ParseJsonInSitu(response.body);
        yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
        yyjson_val* resultArray =
            root && yyjson_is_obj(root) ? yyjson_obj_get(root, "result") : nullptr;
        if (!resultArray || !yyjson_is_arr(resultArray))
            return yahooFallback();

        std::vector<RankedResult> ranked;
        std::unordered_set<std::string> seen;
        SeedBuiltinResults(parsed, builtinResults, ranked, seen);

        size_t index = 0;
        size_t count = 0;
        yyjson_val* item = nullptr;
        yyjson_arr_foreach(resultArray, index, count, item) {
            std::string symbol;
            std::string display;
            std::string description;
            std::string type;
            if (!JsonString(item, "symbol", symbol))
                continue;
            JsonString(item, "displaySymbol", display);
            JsonString(item, "description", description);
            JsonString(item, "type", type);
            // Eligibility is decided from the provider symbol before applying
            // its display alias. Otherwise an international row such as AAPL.L
            // can masquerade as the unsupported US-looking display value AAPL.
            const std::string providerSymbol = symbol;
            if (providerSymbol.empty() || providerSymbol.find(':') != std::string::npos)
                continue;
            const size_t dot = providerSymbol.find('.');
            if (dot != std::string::npos) {
                const std::string suffix = Upper(providerSymbol.substr(dot + 1));
                if (suffix != "A" && suffix != "B")
                    continue;
            }
            if (UnsupportedSearchType(type))
                continue;
            if (!display.empty())
                symbol = std::move(display);
            const auto supportedSymbol = squarestar::market::MarketSymbol::Parse(symbol);
            if (!supportedSymbol || !supportedSymbol->HasSupportedUsClassSuffix())
                continue;
            symbol = supportedSymbol->Canonical();
            const std::string normalizedSymbol = Upper(symbol);
            if (IsPlaceholderSymbolDescription(symbol, description))
                continue;
            if (!seen.insert(normalizedSymbol).second)
                continue;

            const int score =
                RankMatch(parsed, symbol, description, type, index);
            if (score > 0)
                ranked.push_back({std::move(symbol), std::move(description), score});
        }

        Results results = FinalizeRankedResults(parsed.text, std::move(ranked));
        StoreCache(normalizedQuery, results, std::chrono::steady_clock::now());
        return results;
    } catch (...) {
        squarestar::application::ReportBackgroundFailure("symbol search payload");
        // Search is a core navigation surface. If Finnhub returns malformed data,
        // keep recommendations useful through the keyless Yahoo lookup path.
        return yahooFallback();
    }
}

void SymbolSearchService::ClearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    cacheBytes_ = 0;
    providerCooldownUntil_ = {};
}

std::optional<SymbolSearchService::Results> SymbolSearchService::LookupCached(
    std::string_view normalizedQuery,
    std::chrono::steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    PruneExpired(now);
    const auto cached = cache_.find(std::string(normalizedQuery));
    if (cached == cache_.end())
        return std::nullopt;
    return cached->second.matches;
}

void SymbolSearchService::StoreCache(std::string normalizedQuery,
                                     Results results,
                                     std::chrono::steady_clock::time_point now) {
    CacheEntry entry;
    entry.matches = std::move(results);
    entry.savedAt = now;
    entry.heapBytes = EstimateResultsHeapBytes(entry.matches);
    if (entry.heapBytes > kMaxCacheHeapBytes)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    PruneExpired(now);
    if (const auto existing = cache_.find(normalizedQuery); existing != cache_.end()) {
        cacheBytes_ -= std::min(cacheBytes_, existing->second.heapBytes);
        cache_.erase(existing);
    }

    while (!cache_.empty() &&
           (cache_.size() >= kMaxCacheEntries ||
            cacheBytes_ > kMaxCacheHeapBytes - entry.heapBytes)) {
        const auto oldest = std::min_element(
            cache_.begin(), cache_.end(), [](const auto& left, const auto& right) {
                return left.second.savedAt < right.second.savedAt;
            });
        if (oldest == cache_.end())
            break;
        cacheBytes_ -= std::min(cacheBytes_, oldest->second.heapBytes);
        cache_.erase(oldest);
    }
    cacheBytes_ += entry.heapBytes;
    cache_.insert_or_assign(std::move(normalizedQuery), std::move(entry));
}

void SymbolSearchService::PruneExpired(std::chrono::steady_clock::time_point now) {
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (now - it->second.savedAt >= kCacheTtl) {
            cacheBytes_ -= std::min(cacheBytes_, it->second.heapBytes);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace squarestar::search
