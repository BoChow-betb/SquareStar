#include "services/config_persistence.hpp"
#include "services/screener_cache_persistence.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using squarestar::application::ScreenerItem;

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

ScreenerItem TestRow() {
    ScreenerItem item;
    item.symbol = "BRK-B";
    item.name = "Berkshire Hathaway";
    item.price = 502.25;
    item.hasPrice = true;
    item.changePercent = 1.75;
    item.hasChangePercent = true;
    item.marketCap = 9.5e11;
    item.hasMarketCap = true;
    item.lastMarketTime = 1'788'000'000;
    item.resolved = true;
    item.sparklineAttempted = true;
    squarestar::market::ReplaceScreenerSparkline(
        item, {495.0f, 499.5f, 502.25f});
    item.sparklineUnitPoints = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    return item;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using squarestar::config::FlushConfigWrites;
    using squarestar::providers::DecodePersistentScreenerCache;
    using squarestar::providers::EncodePersistentScreenerCache;
    using squarestar::providers::LoadPersistentScreenerCacheFile;
    using squarestar::providers::QueuePersistentScreenerCacheFile;

    const auto fetchedAt = std::chrono::system_clock::now() -
                           std::chrono::minutes(7);
    const std::vector<ScreenerItem> source{TestRow()};
    const std::string encoded = EncodePersistentScreenerCache(
        "trending_now", fetchedAt, source);
    Require(!encoded.empty(), "a valid filtered snapshot should encode");
    Require(encoded.find("sparklineUnitPoints") == std::string::npos,
            "renderer geometry must not enter the persistent cache");

    std::chrono::system_clock::time_point decodedTime;
    std::vector<ScreenerItem> decoded;
    Require(DecodePersistentScreenerCache(
                encoded, "trending_now", decodedTime, decoded),
            "the versioned cache should round-trip");
    Require(decoded.size() == 1 && decoded.front().symbol == "BRK-B" &&
                decoded.front().name == "Berkshire Hathaway" &&
                decoded.front().hasPrice &&
                std::abs(decoded.front().price - 502.25) < 1e-9 &&
                decoded.front().sparkline.size() == 3 &&
                decoded.front().sparklineUnitPoints.empty(),
            "round-trip should preserve provider data but not UI state");
    const auto timestampError = std::chrono::duration_cast<std::chrono::seconds>(
        decodedTime - fetchedAt);
    Require(std::abs(timestampError.count()) <= 1,
            "the original fetch timestamp should survive restart");

    Require(!DecodePersistentScreenerCache(
                encoded, "day_gainers", decodedTime, decoded),
            "a snapshot must not be replayed under another screener route");
    std::string wrongVersion = encoded;
    const std::size_t version = wrongVersion.find("\"version\": 1");
    Require(version != std::string::npos,
            "the test fixture should contain the current version");
    wrongVersion.replace(version, std::string("\"version\": 1").size(),
                         "\"version\": 99");
    Require(!DecodePersistentScreenerCache(
                std::move(wrongVersion), "trending_now", decodedTime, decoded),
            "unknown cache versions should fail closed");

    ScreenerItem crypto = TestRow();
    crypto.symbol = "BTC-USD";
    Require(EncodePersistentScreenerCache(
                "trending_now", fetchedAt, {crypto}).empty(),
            "non-equity USD pairs must never be persisted for display");

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    std::error_code error;
    fs::path temporaryRoot = fs::temp_directory_path(error);
    if (error) {
        error.clear();
        temporaryRoot = fs::current_path(error);
    }
    Require(!error, "a cache test root should be available");
    const fs::path root = temporaryRoot /
        ("squarestar-screener-cache-" + std::to_string(suffix));
    const fs::path path = root / "nested" / "screener-cache.json";
    fs::create_directories(root, error);
    Require(!error, "the cache test directory should be available");
    Require(QueuePersistentScreenerCacheFile(
                path.string(), "trending_now", fetchedAt, source) &&
                FlushConfigWrites(),
            "the cache should use the atomic coalescing writer");
    decoded.clear();
    Require(LoadPersistentScreenerCacheFile(
                path.string(), "trending_now", decodedTime, decoded) &&
                decoded.size() == 1 && decoded.front().symbol == "BRK-B",
            "a persisted cache should load after a simulated restart");

    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "{not-json";
    }
    Require(!LoadPersistentScreenerCacheFile(
                path.string(), "trending_now", decodedTime, decoded),
            "a corrupt cache should be ignored without partial rows");

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
