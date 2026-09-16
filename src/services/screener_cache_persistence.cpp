#include "services/screener_cache_persistence.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "domain/json_text.hpp"
#include "domain/screener_data.hpp"
#include "platform/application_paths.hpp"
#include "platform/windows_path.hpp"
#include "services/config_persistence.hpp"
#include "services/json_access.hpp"
#include "services/screener_payload.hpp"

namespace squarestar::providers {
namespace {

using squarestar::application::ApproximateScreenerItemsHeapBytes;
using squarestar::application::ScreenerItem;
using squarestar::json::JsonBool;
using squarestar::json::JsonInt;
using squarestar::json::JsonNumber;
using squarestar::json::JsonString;
using squarestar::json::ParseJsonInSitu;
using squarestar::text::EscapeJsonStringValue;

constexpr std::size_t kMaximumPersistentRows = 128;
constexpr std::size_t kMaximumPersistentBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaximumPersistentHeapBytes = 2 * 1024 * 1024;
constexpr std::string_view kPersistentRoute = "trending_now";

double FiniteOrZero(double value) noexcept {
    return std::isfinite(value) ? value : 0.0;
}

bool IsSafeCachedSymbol(std::string_view symbol) noexcept {
    if (symbol.empty() || symbol.size() > 32 || symbol.ends_with("-USD"))
        return false;
    return std::all_of(symbol.begin(), symbol.end(), [](unsigned char value) {
        return value >= 0x21 && value <= 0x7e;
    });
}

bool ReadOptionalNumber(yyjson_val* object,
                        const char* valueKey,
                        const char* flagKey,
                        double& value,
                        bool& hasValue) {
    bool persistedFlag = false;
    double persistedValue = 0.0;
    if (!JsonBool(object, flagKey, persistedFlag) || !persistedFlag) {
        value = 0.0;
        hasValue = false;
        return true;
    }
    if (!JsonNumber(object, valueKey, persistedValue) ||
        !std::isfinite(persistedValue)) {
        return false;
    }
    value = persistedValue;
    hasValue = true;
    return true;
}

bool DecodeRow(yyjson_val* object, ScreenerItem& item) {
    if (!object || !yyjson_is_obj(object) ||
        !JsonString(object, "symbol", item.symbol, 32) ||
        !IsSafeCachedSymbol(item.symbol)) {
        return false;
    }
    (void)JsonString(object, "name", item.name, 512);
    if (!ReadOptionalNumber(object, "price", "has_price", item.price, item.hasPrice) ||
        !ReadOptionalNumber(object, "change", "has_change", item.change, item.hasChange) ||
        !ReadOptionalNumber(object,
                            "change_percent",
                            "has_change_percent",
                            item.changePercent,
                            item.hasChangePercent) ||
        !ReadOptionalNumber(object, "volume", "has_volume", item.volume, item.hasVolume) ||
        !ReadOptionalNumber(object,
                            "average_volume_3m",
                            "has_average_volume_3m",
                            item.avgVol3M,
                            item.hasAvgVol3M) ||
        !ReadOptionalNumber(object,
                            "market_cap",
                            "has_market_cap",
                            item.marketCap,
                            item.hasMarketCap) ||
        !ReadOptionalNumber(object,
                            "pe_ratio",
                            "has_pe_ratio",
                            item.peRatio,
                            item.hasPeRatio) ||
        !ReadOptionalNumber(object,
                            "fifty_two_week_change",
                            "has_fifty_two_week_change",
                            item.fiftyTwoWkChange,
                            item.hasFiftyTwoWkChange)) {
        return false;
    }

    std::int64_t lastMarketTime = 0;
    if (JsonInt(object, "last_market_time", lastMarketTime) && lastMarketTime > 0)
        item.lastMarketTime = static_cast<std::time_t>(lastMarketTime);
    (void)JsonBool(object, "resolved", item.resolved);
    (void)JsonBool(object, "suspected_inactive", item.suspectedInactive);
    (void)JsonBool(object, "sparkline_attempted", item.sparklineAttempted);

    yyjson_val* sparkline = yyjson_obj_get(object, "sparkline");
    if (sparkline) {
        if (!yyjson_is_arr(sparkline) ||
            yyjson_arr_size(sparkline) > kMaxScreenerSparklineSamples) {
            return false;
        }
        std::vector<float> points;
        points.reserve(yyjson_arr_size(sparkline));
        std::size_t index = 0;
        std::size_t count = 0;
        yyjson_val* point = nullptr;
        yyjson_arr_foreach(sparkline, index, count, point) {
            if (!point || !yyjson_is_num(point))
                return false;
            const double value = yyjson_get_num(point);
            const float narrowed = static_cast<float>(value);
            if (!std::isfinite(value) || !std::isfinite(narrowed) || value <= 0.0)
                return false;
            points.push_back(narrowed);
        }
        if (!points.empty())
            squarestar::market::ReplaceScreenerSparkline(item, std::move(points));
    }
    return true;
}

}

std::string EncodePersistentScreenerCache(
    const std::string& cacheKey,
    std::chrono::system_clock::time_point fetchedAt,
    const std::vector<ScreenerItem>& items) {
    const auto fetchedAtSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        fetchedAt.time_since_epoch()).count();
    if (cacheKey.empty() || cacheKey.size() > 256 || fetchedAtSeconds <= 0 ||
        items.empty() || items.size() > kMaximumPersistentRows ||
        ApproximateScreenerItemsHeapBytes(items) > kMaximumPersistentHeapBytes) {
        return {};
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\n  \"version\": " << kPersistentScreenerCacheVersion
        << ",\n  \"cache_key\": \"" << EscapeJsonStringValue(cacheKey)
        << "\",\n  \"fetched_at\": " << fetchedAtSeconds << ",\n  \"items\": [\n";
    for (std::size_t index = 0; index < items.size(); ++index) {
        const ScreenerItem& item = items[index];
        if (!IsSafeCachedSymbol(item.symbol))
            return {};
        out << "    {\"symbol\":\"" << EscapeJsonStringValue(item.symbol)
            << "\",\"name\":\"" << EscapeJsonStringValue(item.name)
            << "\",\"price\":" << FiniteOrZero(item.price)
            << ",\"change\":" << FiniteOrZero(item.change)
            << ",\"change_percent\":" << FiniteOrZero(item.changePercent)
            << ",\"volume\":" << FiniteOrZero(item.volume)
            << ",\"average_volume_3m\":" << FiniteOrZero(item.avgVol3M)
            << ",\"market_cap\":" << FiniteOrZero(item.marketCap)
            << ",\"pe_ratio\":" << FiniteOrZero(item.peRatio)
            << ",\"fifty_two_week_change\":"
            << FiniteOrZero(item.fiftyTwoWkChange)
            << ",\"has_price\":" << (item.hasPrice ? "true" : "false")
            << ",\"has_change\":" << (item.hasChange ? "true" : "false")
            << ",\"has_change_percent\":"
            << (item.hasChangePercent ? "true" : "false")
            << ",\"has_volume\":" << (item.hasVolume ? "true" : "false")
            << ",\"has_average_volume_3m\":"
            << (item.hasAvgVol3M ? "true" : "false")
            << ",\"has_market_cap\":" << (item.hasMarketCap ? "true" : "false")
            << ",\"has_pe_ratio\":" << (item.hasPeRatio ? "true" : "false")
            << ",\"has_fifty_two_week_change\":"
            << (item.hasFiftyTwoWkChange ? "true" : "false")
            << ",\"last_market_time\":"
            << static_cast<std::int64_t>(item.lastMarketTime)
            << ",\"resolved\":" << (item.resolved ? "true" : "false")
            << ",\"suspected_inactive\":"
            << (item.suspectedInactive ? "true" : "false")
            << ",\"sparkline_attempted\":"
            << (item.sparklineAttempted ? "true" : "false")
            << ",\"sparkline\":[";
        for (std::size_t point = 0; point < item.sparkline.size(); ++point) {
            const float value = item.sparkline[point];
            if (!std::isfinite(value) || value <= 0.0f)
                return {};
            if (point != 0)
                out << ',';
            out << value;
        }
        out << "]}";
        if (index + 1 != items.size())
            out << ',';
        out << '\n';
    }
    out << "  ]\n}\n";
    std::string payload = out.str();
    return payload.size() <= kMaximumPersistentBytes ? std::move(payload)
                                                     : std::string{};
}

bool DecodePersistentScreenerCache(
    std::string payload,
    const std::string& expectedCacheKey,
    std::chrono::system_clock::time_point& fetchedAt,
    std::vector<ScreenerItem>& items) {
    if (payload.empty() || payload.size() > kMaximumPersistentBytes)
        return false;
    auto document = ParseJsonInSitu(payload);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    std::int64_t version = 0;
    std::int64_t fetchedAtSeconds = 0;
    std::string cacheKey;
    if (!root || !yyjson_is_obj(root) ||
        !JsonInt(root, "version", version) ||
        version != kPersistentScreenerCacheVersion ||
        !JsonString(root, "cache_key", cacheKey, 256) ||
        cacheKey != expectedCacheKey ||
        !JsonInt(root, "fetched_at", fetchedAtSeconds) || fetchedAtSeconds <= 0) {
        return false;
    }
    yyjson_val* rows = yyjson_obj_get(root, "items");
    if (!rows || !yyjson_is_arr(rows) || yyjson_arr_size(rows) == 0 ||
        yyjson_arr_size(rows) > kMaximumPersistentRows) {
        return false;
    }

    std::vector<ScreenerItem> decoded;
    decoded.reserve(yyjson_arr_size(rows));
    std::unordered_set<std::string> symbols;
    symbols.reserve(decoded.capacity());
    std::size_t index = 0;
    std::size_t count = 0;
    yyjson_val* row = nullptr;
    yyjson_arr_foreach(rows, index, count, row) {
        ScreenerItem item;
        if (!DecodeRow(row, item) || !symbols.insert(item.symbol).second)
            return false;
        decoded.push_back(std::move(item));
    }
    if (ApproximateScreenerItemsHeapBytes(decoded) > kMaximumPersistentHeapBytes)
        return false;
    fetchedAt = std::chrono::system_clock::time_point(
        std::chrono::seconds(fetchedAtSeconds));
    items = std::move(decoded);
    return true;
}

bool LoadPersistentScreenerCacheFile(
    const std::string& path,
    const std::string& expectedCacheKey,
    std::chrono::system_clock::time_point& fetchedAt,
    std::vector<ScreenerItem>& items) {
    if (path.empty())
        return false;
    std::ifstream input(squarestar::platform::Utf8FilesystemPath(path),
                        std::ios::binary);
    if (!input)
        return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(kMaximumPersistentBytes))
        return false;
    input.seekg(0, std::ios::beg);
    std::string payload(static_cast<std::size_t>(size), '\0');
    input.read(payload.data(), size);
    if (!input)
        return false;
    return DecodePersistentScreenerCache(
        std::move(payload), expectedCacheKey, fetchedAt, items);
}

bool QueuePersistentScreenerCacheFile(
    const std::string& path,
    const std::string& cacheKey,
    std::chrono::system_clock::time_point fetchedAt,
    const std::vector<ScreenerItem>& items) {
    if (path.empty())
        return false;
    std::string payload = EncodePersistentScreenerCache(cacheKey, fetchedAt, items);
    return !payload.empty() && squarestar::config::SubmitConfigWrite(
                                   path, std::move(payload));
}

bool LoadPersistentScreenerCache(
    const std::string& cacheKey,
    std::chrono::system_clock::time_point& fetchedAt,
    std::vector<ScreenerItem>& items) {
    return cacheKey == kPersistentRoute &&
           LoadPersistentScreenerCacheFile(
               squarestar::platform::GetScreenerCachePath(),
               cacheKey,
               fetchedAt,
               items);
}

bool QueuePersistentScreenerCache(
    const std::string& cacheKey,
    std::chrono::system_clock::time_point fetchedAt,
    const std::vector<ScreenerItem>& items) {
    return cacheKey == kPersistentRoute &&
           QueuePersistentScreenerCacheFile(
               squarestar::platform::GetScreenerCachePath(),
               cacheKey,
               fetchedAt,
               items);
}

}
