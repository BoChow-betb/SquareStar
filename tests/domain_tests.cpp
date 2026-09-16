#include "test_fixtures.hpp"
#include "application/app_limits.hpp"
#include "application/app_state.hpp"
#include "application/frame_rate.hpp"
#include "application/runtime_decisions.hpp"
#include "application/stock_market_data.hpp"
#include "domain/futures_catalog.hpp"
#include "domain/market_calendar.hpp"
#include "domain/market_symbol.hpp"
#include "domain/number_format.hpp"
#include "domain/screener_data.hpp"
#include "domain/stock_data.hpp"
#include "domain/symbol_search.hpp"
#include "domain/text.hpp"
#include "presentation/screener_sparkline.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

bool ContainsSymbol(const std::vector<std::pair<std::string, std::string>>& results,
                    std::string_view symbol) {
    const std::string wanted = squarestar::search::NormalizeSymbolSearchKey(symbol);
    return std::any_of(results.begin(), results.end(), [&](const auto& item) {
        return squarestar::search::NormalizeSymbolSearchKey(item.first) == wanted;
    });
}

void CheckSymbol(std::string raw,
                 const char* canonical,
                 const char* yahoo,
                 const char* finnhub) {
    const auto symbol = squarestar::market::MarketSymbol::Parse(raw);
    Check(symbol.has_value(), ("parse " + raw).c_str());
    if (!symbol)
        return;
    Check(symbol->Canonical() == canonical, ("canonicalize " + raw).c_str());
    Check(symbol->Yahoo() == yahoo, ("Yahoo mapping " + raw).c_str());
    Check(symbol->Finnhub() == finnhub, ("Finnhub mapping " + raw).c_str());
}

std::time_t UtcTime(int year, int month, int day, int hour, int minute = 0) {
    std::tm value{};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    return squarestar::market::UtcTmToTimeT(value);
}

}

int main() {
    squarestar::application::AppState modeState;
    modeState.config.animEnabled = true;
    modeState.config.zeroGraphics = false;
    modeState.config.objectFocus = true;
    modeState.config.activeWorldClocks = {0, 1, 2, 3};
    Check(modeState.UiAnimationsEnabled() && !modeState.ZeroGraphicsEnabled() &&
              modeState.ObjectFocusEnabled(),
          "normal GUI behavior is derived from persistent preferences");
    modeState.navigation.liteGuiActive = true;
    modeState.navigation.liteWorldClocks = {0, 1};
    modeState.navigation.liteHiddenWorldClocks = {2, 3};
    Check(!modeState.UiAnimationsEnabled() && modeState.ZeroGraphicsEnabled() &&
              !modeState.ObjectFocusEnabled() &&
              modeState.config.animEnabled && !modeState.config.zeroGraphics &&
              modeState.config.objectFocus,
          "LiteGUI overrides behavior without rewriting persistent preferences");
    modeState.SetWorldClockEnabled(0, false);
    modeState.SetWorldClockEnabled(4, true);
    Check(modeState.VisibleWorldClocks() == std::vector<int>({1, 4}) &&
              modeState.config.activeWorldClocks == std::vector<int>({1, 4, 2, 3}),
          "LiteGUI clock edits preserve hidden desktop clock selections");

    using squarestar::application::GuiFrameRateMode;
    using squarestar::application::ComputeMonitorTileRect;

    using squarestar::application::GuiRenderDecisionInputs;
    using squarestar::application::GuiWindowGeometry;
    using squarestar::application::ShouldRenderGuiFrame;
    using squarestar::application::GuiFrameRateUsesVSync;
    using squarestar::application::GuiFrameTargetSeconds;
    using squarestar::application::GuiPassiveFrameTargetSeconds;
    using squarestar::application::NormalizeGuiFrameRateMode;
    using squarestar::application::SecondsUntilNextWallClockSecond;
    using squarestar::market::MarketSymbol;

    const auto testStock = squarestar::test::BuildTestStockData();
    Check(testStock.success && testStock.timestamps.size() == 900 &&
              squarestar::market::StockDataInvariantsHold(testStock),
          "synthetic stock fixture is internally valid");

    squarestar::application::StockMarketDataState quoteState;
    quoteState.PublishRawData(testStock);
    const double* chartStorageBeforeQuote = quoteState.RawData().timestamps.data();
    squarestar::market::StockData quotePatch;
    quotePatch.success = true;
    quotePatch.currentPrice = testStock.currentPrice + 1.0;
    quotePatch.previousClose = testStock.previousClose;
    const double chartPreviousBeforeQuote = quoteState.RawData().chartPreviousClose;
    Check(quoteState.ApplyQuotePatchAtRevision(quotePatch, 0, 1),
          "live quote patch applies successfully");
    Check(quoteState.RawData().timestamps.data() == chartStorageBeforeQuote,
          "live quote patch does not copy chart storage when no snapshot is retained");
    Check(std::abs(quoteState.RawData().chartPreviousClose - chartPreviousBeforeQuote) < 1e-12,
          "live quote patch preserves the selected range previous close");

    const auto retainedSnapshot = quoteState.RawDataSnapshot();
    const double retainedPrice = retainedSnapshot->currentPrice;
    const double* retainedChartStorage = retainedSnapshot->timestamps.data();
    quotePatch.currentPrice += 1.0;
    Check(quoteState.ApplyQuotePatchAtRevision(quotePatch, 0, 2),
          "live quote patch applies with a retained immutable snapshot");
    Check(std::abs(retainedSnapshot->currentPrice - retainedPrice) < 1e-12 &&
              retainedSnapshot->timestamps.data() == retainedChartStorage,
          "retained market-data snapshots stay immutable across quote publication");
    Check(std::abs(quoteState.RawData().currentPrice - quotePatch.currentPrice) < 1e-12,
          "new quote publication is visible after copy-on-write");
    const auto testScreeners = squarestar::test::BuildTestScreenerData();
    Check(testScreeners.size() == 24 && testScreeners.front().resolved &&
              testScreeners.front().sparkline.size() == 5,
          "synthetic screener fixture covers a populated overview page");
    const std::vector<float> fallingFiveDay = {105.0f, 103.0f, 101.0f};
    const std::vector<float> risingFiveDay = {95.0f, 97.0f, 99.0f};
    Check(squarestar::presentation::ScreenerRowRevealProgress(0.0f, 0) == 0.0f,
          "first screener row starts hidden");
    Check(squarestar::presentation::ScreenerRowRevealProgress(0.12f, 0) >
              squarestar::presentation::ScreenerRowRevealProgress(0.12f, 1),
          "screener rows reveal in top-to-bottom order");
    Check(squarestar::presentation::ScreenerRowRevealProgress(1.0f, 5) == 1.0f,
          "screener row reveal eventually completes");

    Check(squarestar::presentation::ScreenerRowPerformanceDirection(
              true, 4.0, fallingFiveDay) > 0.0,
          "positive displayed change stays positive even when the 5-day trace falls");
    Check(squarestar::presentation::ScreenerRowPerformanceDirection(
              true, -0.94, risingFiveDay) < 0.0,
          "negative displayed change stays negative even when the 5-day trace rises");
    Check(squarestar::presentation::ScreenerRowPerformanceDirection(
              false, 0.0, fallingFiveDay) < 0.0 &&
              squarestar::presentation::ScreenerRowPerformanceDirection(
                  false, 0.0, risingFiveDay) > 0.0,
          "5-day direction remains the fallback when change percentage is unavailable");

    Check(squarestar::market::GregorianWeekday(2026, 8, 17) == 1,
          "calculate Gregorian weekdays without platform APIs");
    Check(squarestar::market::GoodFridayDate(2026) ==
              squarestar::market::CalendarDate{2026, 4, 3},
          "derive Good Friday rather than Easter Sunday");
    Check(squarestar::market::GetMarketClosedReason(2026, 4, 3) == "Good Friday",
          "close the market on Good Friday");
    Check(squarestar::market::GetMarketClosedReason(2022, 6, 20) ==
              "Juneteenth National Independence Day",
          "observe Juneteenth after the exchange adopted it");
    Check(squarestar::market::GetMarketClosedReason(2021, 6, 18).empty(),
          "do not apply Juneteenth before exchange adoption");
    Check(!squarestar::market::IsMarketTradingDay(2001, 9, 11) &&
              !squarestar::market::IsMarketTradingDay(2001, 9, 12) &&
              !squarestar::market::IsMarketTradingDay(2001, 9, 13) &&
              !squarestar::market::IsMarketTradingDay(2001, 9, 14),
          "honor all four September 11 market-closure days");
    Check(squarestar::market::GetMarketClosedReason(2004, 6, 11) ==
              "National Day of Mourning (Ronald Reagan)",
          "honor the 2004 one-off national day of mourning");
    Check(squarestar::market::GetMarketClosedReason(2007, 1, 2) ==
              "National Day of Mourning (Gerald Ford)",
          "honor the 2007 one-off national day of mourning");
    Check(squarestar::market::GetMarketClosedReason(2012, 10, 29) ==
              "Hurricane Sandy" &&
              squarestar::market::GetMarketClosedReason(2012, 10, 30) ==
                  "Hurricane Sandy",
          "honor both Hurricane Sandy market-closure days");
    Check(squarestar::market::GetMarketClosedReason(2018, 12, 5) ==
              "National Day of Mourning (George H. W. Bush)",
          "honor the 2018 one-off U.S. equity-market closure");
    Check(squarestar::market::GetMarketClosedReason(2027, 12, 31).empty(),
          "do not observe a Saturday New Year's Day on the preceding Friday");
    Check(squarestar::market::MarketCloseMinutesForDate(2027, 12, 31) == 16 * 60,
          "keep the 2027-12-31 core session open through its regular close");

    Check(squarestar::market::NewYorkUtcOffsetSeconds(UtcTime(2026, 3, 8, 6, 59)) ==
              -5 * 60 * 60,
          "use EST immediately before the spring DST transition");
    Check(squarestar::market::NewYorkUtcOffsetSeconds(UtcTime(2026, 3, 8, 7, 0)) ==
              -4 * 60 * 60,
          "use EDT at the spring DST transition");
    Check(squarestar::market::NewYorkUtcOffsetSeconds(UtcTime(2026, 11, 1, 5, 59)) ==
              -4 * 60 * 60,
          "use EDT immediately before the fall DST transition");
    Check(squarestar::market::NewYorkUtcOffsetSeconds(UtcTime(2026, 11, 1, 6, 0)) ==
              -5 * 60 * 60,
          "use EST at the fall DST transition");

    Check(!squarestar::market::IsMarketOpenAt(UtcTime(2026, 8, 17, 13, 29)),
          "market is closed before 09:30 New York time");
    Check(squarestar::market::IsMarketOpenAt(UtcTime(2026, 8, 17, 13, 30)),
          "market opens at 09:30 New York time");
    Check(!squarestar::market::IsMarketOpenAt(UtcTime(2026, 8, 17, 20, 0)),
          "market closes at 16:00 New York time");
    Check(squarestar::market::MarketCloseMinutesForDate(2026, 11, 27) == 13 * 60,
          "day after Thanksgiving closes at 13:00 New York time");
    Check(squarestar::market::IsMarketOpenAt(UtcTime(2026, 11, 27, 17, 59)),
          "early-close session remains open before 13:00 New York time");
    Check(!squarestar::market::IsMarketOpenAt(UtcTime(2026, 11, 27, 18, 0)),
          "early-close session closes at 13:00 New York time");
    Check(squarestar::market::MarketCloseMinutesForDate(2026, 12, 24) == 13 * 60,
          "Christmas Eve trading session closes early");
    Check(squarestar::market::MarketCloseMinutesForDate(2026, 7, 3) == 0,
          "a fully observed Independence Day holiday overrides the July 3 early close");
    Check(squarestar::market::MarketCloseMinutesForDate(2028, 7, 3) == 13 * 60,
          "July 3 closes early when it is a trading day");
    Check(squarestar::market::NextMarketSettlementAt(UtcTime(2026, 11, 27, 15, 0)) ==
              UtcTime(2026, 11, 27, 18, 0),
          "next settlement uses the 13:00 early close");
    Check(squarestar::market::MarketSettlementTimeLabel(
              UtcTime(2026, 11, 27, 15, 0)) == "1:00 PM ET",
          "early-close user-facing label matches the calendar settlement");
    Check(squarestar::market::MarketSettlementTimeLabel(
              UtcTime(2026, 8, 17, 15, 0)) == "4:00 PM ET",
          "regular-session user-facing label shows the normal close");
    Check(squarestar::market::IsMarketOpeningWindowAt(UtcTime(2026, 8, 17, 14, 29)),
          "opening window includes the first trading hour");
    Check(!squarestar::market::IsMarketOpeningWindowAt(UtcTime(2026, 8, 17, 14, 30)),
          "opening window ends at 10:30 New York time");
    Check(squarestar::market::NextMarketOpenAt(UtcTime(2026, 12, 24, 21, 0)) ==
              UtcTime(2026, 12, 28, 14, 30),
          "next open skips Christmas and the weekend");
    Check(squarestar::market::NextMarketSettlementAt(UtcTime(2026, 12, 24, 21, 0)) ==
              UtcTime(2026, 12, 28, 21, 0),
          "next settlement skips Christmas and the weekend");

    squarestar::market::StockData stockData;
    stockData.companyName = "SquareStar";
    stockData.timestamps.reserve(8);
    Check(squarestar::market::ApproximateStockDataHeapBytes(stockData) >=
              stockData.timestamps.capacity() * sizeof(double),
          "market-data heap accounting is independent from GUI state");
    Check(squarestar::market::StockDataInvariantsHold(stockData),
          "quote-only StockData without a chart satisfies the shared invariant");
    stockData.timestamps = {100.0, 200.0};
    stockData.opens = {10.0, 11.0};
    stockData.highs = {11.0, 12.0};
    stockData.lows = {9.0, 10.0};
    stockData.closes = {10.5, 11.5};
    stockData.volumes = {1000.0, 1200.0};
    Check(squarestar::market::StockDataInvariantsHold(stockData),
          "normalized OHLCV data satisfies the shared StockData invariant");
    stockData.closes.pop_back();
    Check(squarestar::market::ValidateStockDataInvariants(stockData) ==
              squarestar::market::StockDataInvariantError::ChartVectorSizeMismatch,
          "StockData invariant detects parallel-vector length drift");
    stockData.closes.push_back(11.5);
    stockData.timestamps[1] = 100.0;
    Check(squarestar::market::ValidateStockDataInvariants(stockData) ==
              squarestar::market::StockDataInvariantError::NonIncreasingTimestamp,
          "StockData invariant detects duplicate or reversed timestamps");

    squarestar::market::ScreenerData screenerData;
    screenerData.symbol = "AAPL";
    Check(screenerData.symbol == "AAPL", "screener provider data is GUI-independent");
    const uint64_t initialSparklineRevision = screenerData.sparklineRevision;
    squarestar::market::ReplaceScreenerSparkline(screenerData, {1.0f, 2.0f, 3.0f});
    const uint64_t firstSparklineRevision = screenerData.sparklineRevision;
    squarestar::market::ReplaceScreenerSparkline(screenerData, {3.0f, 2.0f, 1.0f});
    Check(firstSparklineRevision != initialSparklineRevision &&
              screenerData.sparklineRevision != firstSparklineRevision,
          "same-length sparkline replacement advances the cache revision");

    Check(NormalizeGuiFrameRateMode(-1) == static_cast<int>(GuiFrameRateMode::VSync),
          "default invalid frame-rate mode to VSync");
    Check(NormalizeGuiFrameRateMode(static_cast<int>(GuiFrameRateMode::Eco30)) ==
              static_cast<int>(GuiFrameRateMode::Eco30),
          "preserve the explicit low-power frame-rate mode");
    Check(GuiFrameRateUsesVSync(static_cast<int>(GuiFrameRateMode::VSync)),
          "enable swap synchronization for VSync mode");
    Check(!GuiFrameRateUsesVSync(static_cast<int>(GuiFrameRateMode::Cap48)),
          "disable swap synchronization for capped modes");
    Check(std::abs(GuiFrameTargetSeconds(static_cast<int>(GuiFrameRateMode::VSync)) -
                   1.0 / 60.0) < 0.000000000001,
          "pace VSync mode at a 60 FPS ceiling");
    Check(std::abs(GuiPassiveFrameTargetSeconds(static_cast<int>(GuiFrameRateMode::VSync)) -
                   1.0 / 60.0) < 0.000000000001,
          "keep passive VSync animations at the configured 60 FPS cadence");
    Check(std::abs(GuiPassiveFrameTargetSeconds(static_cast<int>(GuiFrameRateMode::Cap48)) -
                   1.0 / 48.0) < 0.000000000001,
          "keep passive 48 FPS animations at the configured cadence");
    Check(std::abs(GuiPassiveFrameTargetSeconds(static_cast<int>(GuiFrameRateMode::Cap240)) -
                   1.0 / 60.0) < 0.000000000001,
          "cap passive 240 FPS animations at 60 FPS to limit background render cost");
    Check(std::abs(GuiPassiveFrameTargetSeconds(static_cast<int>(GuiFrameRateMode::Eco30)) -
                   1.0 / 30.0) < 0.000000000001,
          "do not accelerate explicit Eco30 mode for passive animations");
    const auto quarterPast = std::chrono::system_clock::time_point{} +
                             std::chrono::milliseconds(250);
    Check(std::abs(SecondsUntilNextWallClockSecond(quarterPast) - 0.752) < 0.000001,
          "align idle clock refreshes to the next wall-clock second");
    CheckSymbol("aapl", "AAPL", "AAPL", "AAPL");
    CheckSymbol("brk.b", "BRK.B", "BRK-B", "BRK.B");
    CheckSymbol("brk-b", "BRK-B", "BRK-B", "BRK.B");
    CheckSymbol("AXIA", "AXIAY", "AXIAY", "AXIAY");
    CheckSymbol("gc=f", "GC=F", "GC=F", "GC=F");
    CheckSymbol("6e=f", "6E=F", "6E=F", "6E=F");
    CheckSymbol("m2k=f", "M2K=F", "M2K=F", "M2K=F");
    const auto goldFuture = MarketSymbol::Parse("GC=F");
    const auto euroFxFuture = MarketSymbol::Parse("6E=F");
    Check(goldFuture && goldFuture->IsYahooFutures() && goldFuture->HasSupportedUsClassSuffix(),
          "accept Yahoo futures symbols such as gold GC=F");
    Check(euroFxFuture && euroFxFuture->IsYahooFutures(),
          "accept digit-leading Yahoo futures symbols such as Euro FX 6E=F");
    const auto goldMatches = squarestar::market::SearchCommonYahooFutures("gold");
    Check(!goldMatches.empty() && goldMatches.front().first == "GC=F",
          "find gold futures by commodity name");
    const auto indexMatches = squarestar::market::SearchCommonYahooFutures("nasdaq futures");
    Check(!indexMatches.empty() &&
              (indexMatches.front().first == "NQ=F" || indexMatches.front().first == "MNQ=F"),
          "find Nasdaq futures by index name");
    const auto fxMatches = squarestar::market::SearchCommonYahooFutures("euro fx");
    Check(!fxMatches.empty() && fxMatches.front().first == "6E=F",
          "find currency futures by descriptive name");

    Check(!MarketSymbol::Parse("").has_value(), "reject empty symbol");
    Check(!MarketSymbol::Parse("1A").has_value(), "reject a leading digit");
    Check(!MarketSymbol::Parse("BRK.AA").has_value(), "reject a multi-character class suffix");
    Check(!MarketSymbol::Parse("A..B").has_value(), "reject multiple separators");
    Check(!MarketSymbol::Parse("ABCDEFG").has_value(), "reject symbols over six characters");
    Check(!MarketSymbol::Parse("A/B").has_value(), "reject unsupported punctuation");
    Check(!MarketSymbol::Parse("GC=FX").has_value(), "reject malformed Yahoo futures suffix");
    Check(!MarketSymbol::Parse("12=F").has_value(), "reject all-numeric futures roots");

    const auto supportedClass = MarketSymbol::Parse("BRK.A");
    const auto unsupportedClass = MarketSymbol::Parse("ABC.X");
    Check(supportedClass && supportedClass->HasSupportedUsClassSuffix(),
          "accept supported class suffix");
    Check(unsupportedClass && !unsupportedClass->HasSupportedUsClassSuffix(),
          "identify unsupported class suffix");

    std::string mixedCase = "SqUaReStAr-1";
    squarestar::text::UppercaseInPlace(mixedCase);
    Check(mixedCase == "SQUARESTAR-1", "uppercase ASCII text");
    squarestar::text::LowercaseInPlace(mixedCase);
    Check(mixedCase == "squarestar-1", "lowercase ASCII text");
    Check(squarestar::text::ParseIntOr("42", -1) == 42, "parse integer");
    Check(squarestar::text::ParseIntOr("+42", -1) == 42, "parse explicit positive integer");
    Check(squarestar::text::ParseIntOr("-42", 0) == -42, "parse negative integer");
    Check(squarestar::text::ParseIntOr("nope", -1) == -1, "integer fallback");
    Check(squarestar::text::ParseIntOr("42px", -1) == -1, "reject trailing integer text");
    Check(squarestar::text::ParseIntOr(" 42", -1) == -1, "reject leading whitespace");
    Check(squarestar::text::ParseIntOr("", -1) == -1, "reject an empty integer");
    Check(squarestar::text::ParseIntOr("999999999999999999999", -1) == -1,
          "reject integer overflow");

    Check(squarestar::search::BoundedEditDistance("AAPL", "AAPL", 1) == 0 &&
              squarestar::search::BoundedEditDistance("APPL", "AAPL", 1) == 1 &&
              squarestar::search::BoundedEditDistance("AAPL", "APL", 1) == 1 &&
              squarestar::search::BoundedEditDistance("APL", "AAPL", 1) == 1 &&
              squarestar::search::BoundedEditDistance("AAPL", "MSFT", 1) == 2 &&
              squarestar::search::BoundedEditDistance("ABCD", "ABXY", 2) == 2,
          "bounded edit distance handles exact, insert, delete, replace, and fallback cases");

    Check(squarestar::search::ScoreSymbolSearchResult(
              "AAPL", "AAPL", "Apple Inc", "Common Stock") >
              squarestar::search::ScoreSymbolSearchResult(
                  "AAP", "AAPL", "Apple Inc", "Common Stock"),
          "symbol search ranks an exact ticker above a ticker prefix");
    Check(squarestar::search::ScoreSymbolSearchResult(
              "apple", "AAPL", "Apple Inc", "Common Stock") > 0,
          "symbol search resolves a company-name prefix");
    Check(squarestar::search::ScoreSymbolSearchResult(
              "bank america", "BAC", "Bank of America Corporation", "Common Stock") > 0,
          "symbol search matches ordered company-name words across filler words");
    Check(squarestar::search::ScoreSymbolSearchResult(
              "APPL", "AAPL", "Apple Inc", "Common Stock") > 0,
          "symbol search tolerates a one-character ticker typo");
    Check(squarestar::search::ScoreSymbolSearchResult(
              "apple", "ZXZZ", "Example Mining Holdings", "Common Stock") == 0,
          "symbol search rejects unrelated provider filler");
    Check(squarestar::search::ScoreSymbolSearchResult(
              "ABC", "ABC", "ABC Holdings", "Common Stock") >
              squarestar::search::ScoreSymbolSearchResult(
                  "ABC", "ABC", "ABC Holdings Warrant", "Warrant"),
          "symbol search prefers a primary common stock over a warrant");

    std::vector<std::pair<std::string, std::string>> appleResults = {
        {"APPLE", "APPLE"},
        {"AAPL", "Apple Inc"},
        {"APLE", "Apple Hospitality REIT Inc"},
    };
    squarestar::search::RemoveAmbiguousPlaceholderMatches("APPLE", appleResults);
    Check(!ContainsSymbol(appleResults, "APPLE") &&
              !appleResults.empty() && appleResults.front().first == "AAPL",
          "company-name search removes a symbol-only placeholder such as APPLE - APPLE");

    std::vector<std::pair<std::string, std::string>> placeholderOnlyAppleResults = {
        {"APPLE", "APPLE"},
    };
    squarestar::search::RemoveAmbiguousPlaceholderMatches(
        "APPLE", placeholderOnlyAppleResults);
    Check(placeholderOnlyAppleResults.empty(),
          "query-echo placeholder is removed even when it is the only provider result");

    std::vector<std::pair<std::string, std::string>> personalizedAppleResults = {
        {"APPLE", "APPLE"},
        {"AAPL", "Apple Inc"},
        {"ZXZZ", "Example Mining Holdings"},
    };
    squarestar::search::PersonalizeSymbolSearchResults(
        "APPLE", personalizedAppleResults, {}, {}, {});
    Check(personalizedAppleResults.size() == 1 &&
              personalizedAppleResults.front().first == "AAPL",
          "personalized search keeps only explainable company-name recommendations");

    std::vector<std::pair<std::string, std::string>> mixedPersonalizedResults = {
        {"ZXZZ", "Example Mining Holdings"},
        {"AAPL", "Apple Inc"},
        {"WXYZ", "World Example Unit"},
        {"APLE", "Apple Hospitality REIT Inc"},
    };
    squarestar::search::PersonalizeSymbolSearchResults(
        "apple", mixedPersonalizedResults, {"APLE"}, {}, {"AAPL"});
    Check(mixedPersonalizedResults.size() == 2 &&
              mixedPersonalizedResults[0].first == "APLE" &&
              mixedPersonalizedResults[1].first == "AAPL",
          "personalized search filters irrelevant rows and preserves recent/watch boosts");

    std::vector<std::pair<std::string, std::string>> normalizedDuplicateResults = {
        {"BRK-A", "Berkshire Hathaway Class A"},
    };
    squarestar::search::PersonalizeSymbolSearchResults(
        "BRK.A", normalizedDuplicateResults, {"BRK.A"}, {}, {});
    Check(normalizedDuplicateResults.size() == 1,
          "personalized search deduplicates provider and recent symbols by normalized key");

    Check(squarestar::format::FormatCompactNumber(999.0) == "999", "format plain number");
    Check(squarestar::format::FormatCompactNumber(1.25, 100) == "1.25",
          "clamp caller-provided decimal precision safely");
    Check(squarestar::format::FormatCompactNumber(1'250.0) == "1.25K", "format thousands");
    Check(squarestar::format::FormatCompactNumber(12'500'000.0) == "12.5M",
          "format millions");
    Check(squarestar::format::FormatCompactNumber(-2'000'000'000.0) == "-2B",
          "format negative billions");
    Check(squarestar::format::FormatCompactNumber(1.0e308) != "N/A",
          "format the largest finite magnitudes from bounded stack storage");
    Check(squarestar::format::FormatLargeNumber(0.0) == "N/A",
          "reject non-positive large number");
    Check(squarestar::format::FormatCompactNumber(std::numeric_limits<double>::infinity()) ==
              "N/A",
          "reject non-finite number");
    Check(!squarestar::format::PriceChangedAtDisplayPrecision(100.001, 100.004),
          "ignore a sub-cent quote delta that displays unchanged");
    Check(squarestar::format::PriceChangedAtDisplayPrecision(100.001, 100.006),
          "detect a quote delta that changes the displayed cents");
    Check(!squarestar::format::PriceChangedAtDisplayPrecision(
              100.0, std::numeric_limits<double>::infinity()),
          "reject an invalid live quote in display comparison");

    GuiRenderDecisionInputs idleRenderInputs{};
    Check(!ShouldRenderGuiFrame(idleRenderInputs),
          "skip a settled GUI pass when no input, animation, revision, clock, or resize is pending");
    idleRenderInputs.revisionChanged = true;
    Check(ShouldRenderGuiFrame(idleRenderInputs),
          "render immediately when application state revision changes");
    idleRenderInputs = {};
    idleRenderInputs.windowGeometryChanged = true;
    Check(ShouldRenderGuiFrame(idleRenderInputs),
          "render immediately after a logical or framebuffer resize");
    idleRenderInputs = {};
    idleRenderInputs.inputSettleFramePending = true;
    Check(ShouldRenderGuiFrame(idleRenderInputs),
          "allow one bounded settle frame after queued input");
    Check(!squarestar::application::ShouldPollGuiEvents(false, false, false) &&
              squarestar::application::ShouldPollGuiEvents(true, false, false) &&
              squarestar::application::ShouldPollGuiEvents(false, true, false) &&
              squarestar::application::ShouldPollGuiEvents(false, false, true),
          "event pump blocks when settled and polls only for active GUI work");
    Check(GuiWindowGeometry{1200, 800, 2400, 1600} ==
              GuiWindowGeometry{1200, 800, 2400, 1600},
          "GUI geometry value object compares logical and framebuffer dimensions together");
    const auto firstOfThree = ComputeMonitorTileRect(0.0f, 0.0f, 900.0f, 600.0f, 0, 3);
    const auto thirdOfThree = ComputeMonitorTileRect(0.0f, 0.0f, 900.0f, 600.0f, 2, 3);
    const auto nearFloat = [](float lhs, float rhs) {
        return std::abs(lhs - rhs) < 0.001f;
    };
    Check(nearFloat(firstOfThree.minX, 0.0f) && nearFloat(firstOfThree.maxX, 450.0f) &&
              nearFloat(firstOfThree.minY, 0.0f) && nearFloat(firstOfThree.maxY, 300.0f),
          "monitor grid keeps the original ceil-sqrt first-tile geometry");
    Check(nearFloat(thirdOfThree.minX, 0.0f) && nearFloat(thirdOfThree.maxX, 450.0f) &&
              nearFloat(thirdOfThree.minY, 300.0f) && nearFloat(thirdOfThree.maxY, 600.0f),
          "monitor grid places the third of three stocks on the second row without seams");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All SquareStar domain tests passed\n";
    return EXIT_SUCCESS;
}
