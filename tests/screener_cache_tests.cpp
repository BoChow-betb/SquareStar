#include "application/screener_cache.hpp"
#include "presentation/screener_sparkline.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    squarestar::application::ScreenerCache cache;
    std::vector<squarestar::application::ScreenerItem> completedTrends(2);
    completedTrends[0].symbol = "AAA";
    completedTrends[0].sparklineAttempted = true;
    squarestar::market::ReplaceScreenerSparkline(
        completedTrends[0], {10.0f, 11.0f, 12.0f});
    completedTrends[1].symbol = "BBB";
    completedTrends[1].sparklineAttempted = true;
    squarestar::market::ReplaceScreenerSparkline(
        completedTrends[1], {20.0f, 19.0f, 18.0f});

    std::vector<squarestar::application::ScreenerItem> refreshedRows(3);
    refreshedRows[0].symbol = "BBB";
    refreshedRows[1].symbol = "NEW";
    refreshedRows[2].symbol = "AAA";
    const std::size_t mergedTrends = squarestar::application::MergeScreenerTrendData(
        completedTrends, 0, completedTrends.size(), refreshedRows);
    Check(mergedTrends == 2 &&
              std::abs(refreshedRows[0].sparkline.front() - 20.0f) < 1e-5f &&
              refreshedRows[1].sparkline.empty() &&
              std::abs(refreshedRows[2].sparkline.front() - 10.0f) < 1e-5f,
          "completed trends merge by symbol without replacing the newer list");

    std::vector<squarestar::application::ScreenerItem> cachedLiveRows(1);
    cachedLiveRows[0].symbol = "LIVE";
    cachedLiveRows[0].price = 100.0;
    cachedLiveRows[0].hasPrice = true;
    cachedLiveRows[0].changePercent = 1.5;
    cachedLiveRows[0].hasChangePercent = true;
    cachedLiveRows[0].marketCap = 500.0;
    cachedLiveRows[0].hasMarketCap = true;
    squarestar::market::ReplaceScreenerSparkline(
        cachedLiveRows[0], {99.0f, 100.0f});
    std::vector<squarestar::application::ScreenerItem> partialLiveRows(1);
    partialLiveRows[0].symbol = "LIVE";
    partialLiveRows[0].price = 101.0;
    partialLiveRows[0].hasPrice = true;
    Check(squarestar::application::MergeScreenerRefreshData(
              cachedLiveRows, partialLiveRows) == 1 &&
              std::abs(partialLiveRows[0].price - 101.0) < 1e-9 &&
              partialLiveRows[0].hasChangePercent &&
              std::abs(partialLiveRows[0].changePercent - 1.5) < 1e-9 &&
              partialLiveRows[0].hasMarketCap &&
              partialLiveRows[0].sparkline.size() == 2,
          "partial live rows override fresh fields while cached gaps stay visible");

    std::vector<squarestar::application::ScreenerItem> renderedRows(1);
    renderedRows[0].symbol = "MEM";
    renderedRows[0].sparkline = {1.0f, 2.0f, 3.0f};
    renderedRows[0].sparklineUnitPoints = {{1.0f, 1.0f}, {2.0f, 2.0f}};
    Check(cache.Store("memory-isolation", renderedRows),
          "provider rows should fit within the bounded app-owned cache");
    std::vector<squarestar::application::ScreenerItem> cachedRows;
    Check(cache.LoadForRefresh("memory-isolation", cachedRows) &&
              cachedRows.size() == 1 && cachedRows[0].sparkline.size() == 3 &&
              cachedRows[0].sparklineUnitPoints.empty(),
          "screener cache retains provider data without duplicating render geometry");
    cache.Clear();

    const auto oldSnapshotTime =
        std::chrono::system_clock::now() - std::chrono::hours(2);
    Check(cache.Store("restored-age", renderedRows, oldSnapshotTime),
          "a validated disk snapshot should restore into the app-owned cache");
    Check(!cache.LoadForRefresh(
              "restored-age", cachedRows, std::chrono::minutes(10)) &&
              cache.LoadForRefresh(
                  "restored-age", cachedRows, std::chrono::hours(3)),
          "restored snapshots preserve their original age instead of becoming fresh");
    cache.Clear();

    const auto futureSnapshotTime =
        std::chrono::system_clock::now() + std::chrono::hours(2);
    Check(!cache.Store("future-age", renderedRows, futureSnapshotTime) &&
              !cache.LoadForRefresh(
                  "future-age", cachedRows, std::chrono::hours(3)),
          "future-dated snapshots must fail closed instead of becoming fresh");

    std::vector<ImVec2> unitGeometry;
    squarestar::presentation::BuildSparklineUnitGeometry(
        renderedRows[0].sparkline, unitGeometry);
    Check(unitGeometry.size() == renderedRows[0].sparkline.size() &&
              renderedRows[0].sparklineUnitPoints.size() == 2,
          "sparkline rendering builds private geometry without mutating shared rows");

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All screener cache tests passed\n";
    return EXIT_SUCCESS;
}
