#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "application/stock_data_merge.hpp"
#include "application/stock_refresh_policy.hpp"
#include "services/stock_data_service.hpp"
#include "services/stock_provider_merge.hpp"

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool NearlyEqual(double left, double right) {
    return std::abs(left - right) < 0.000001;
}

} // namespace

int main() {
    using squarestar::market::FetchKind;
    using squarestar::marketdata::StockFetchInputError;
    using squarestar::marketdata::ValidateStockFetchInputs;

    Require(ValidateStockFetchInputs("AAPL", 0, FetchKind::Full) ==
                StockFetchInputError::None,
            "valid stock request must pass validation");
    Require(ValidateStockFetchInputs("BAD!SYMBOL", 0, FetchKind::Full) ==
                StockFetchInputError::InvalidTickerOrRange,
            "invalid ticker must fail locally");
    Require(ValidateStockFetchInputs("AAPL", -1, FetchKind::Full) ==
                StockFetchInputError::InvalidTickerOrRange,
            "invalid range must fail locally");
    Require(ValidateStockFetchInputs("ABC.C", 0, FetchKind::Full) ==
                StockFetchInputError::UnsupportedMarket,
            "unsupported class suffix must remain rejected");

    Require(!squarestar::application::StockAutoRefreshSessionEligible("AAPL", false) &&
                squarestar::application::StockAutoRefreshSessionEligible("AAPL", true),
            "US equities must remain bound to the regular equity session");
    Require(squarestar::application::StockAutoRefreshSessionEligible("GC=F", false) &&
                squarestar::application::StockAutoRefreshSessionEligible("NQ=F", false),
            "Yahoo futures must not be blocked by the NYSE session clock");
    Require(!squarestar::application::StockAutoRefreshSessionEligible("BAD!", true),
            "invalid symbols are never auto-refresh eligible");
    squarestar::market::StockFetchResult envelope;
    envelope.marketData.success = false;
    envelope.marketData.errorMessage = "provider unavailable";
    envelope.FinalizeFromMarketData(true);
    Require(!envelope.success && envelope.errorMessage == "provider unavailable" &&
                envelope.rateLimited,
            "fetch envelope finalization must preserve the provider rate-limit hint");

    squarestar::market::StockFetchResult merged;
    merged.resolvedTimeRangeIndex = 0;
    squarestar::marketdata::StockProviderPayloads payloads;
    payloads.yahooQuote =
        R"json({"quoteResponse":{"result":[{"longName":"Acme","regularMarketPrice":101,"regularMarketPreviousClose":99,"regularMarketTime":1700000001,"marketCap":2000000000}]}})json";
    payloads.finnhubQuote = R"json({"c":102,"pc":98,"h":103,"l":97,"o":100,"t":1700001000})json";
    payloads.finnhubMetrics =
        R"json({"metric":{"marketCapitalization":2500,"beta":1.2}})json";
    payloads.finnhubProfile =
        R"json({"name":"Acme Corp","country":"US","finnhubIndustry":"Technology"})json";
    payloads.finnhubNews =
        R"json([{"headline":"Acme completes merger transaction","source":"Fixture","url":"https://example.com","summary":"Done","datetime":1700000000}])json";
    const auto mergeResult = squarestar::marketdata::ApplyStockProviderPayloads(
        std::move(payloads),
        {FetchKind::Full, 0, false, true, true, true},
        merged);
    Require(mergeResult.finnhubProfileExists &&
                merged.marketData.companyName == "Acme Corp" &&
                NearlyEqual(merged.marketData.currentPrice, 101.0) &&
                NearlyEqual(merged.marketData.previousClose, 99.0) &&
                NearlyEqual(merged.marketData.marketCap, 2500000000.0) &&
                merged.marketData.quoteTimestamp == 1700000001 &&
                merged.marketData.news.size() == 1,
            "provider-free merge applies source precedence while preserving the price timestamp");

    squarestar::market::StockFetchResult finnhubFallback;
    squarestar::marketdata::StockProviderPayloads fallbackPayloads;
    fallbackPayloads.finnhubQuote =
        R"json({"c":88.5,"pc":87,"h":89,"l":86,"o":87.5,"t":1700001234})json";
    squarestar::marketdata::ApplyStockProviderPayloads(
        std::move(fallbackPayloads),
        {FetchKind::LiveQuote, 0, false, false, false, false},
        finnhubFallback);
    Require(finnhubFallback.marketData.success &&
                NearlyEqual(finnhubFallback.marketData.currentPrice, 88.5) &&
                finnhubFallback.marketData.quoteTimestamp == 1700001234,
            "Finnhub fallback owns the timestamp only when it supplies current price");

    squarestar::market::StockFetchResult stalePriceGuard;
    stalePriceGuard.marketData.currentPrice = 77.0;
    stalePriceGuard.marketData.quoteTimestamp = 1700000000;
    stalePriceGuard.marketData.success = true;
    squarestar::marketdata::StockProviderPayloads timestampOnlyPayload;
    timestampOnlyPayload.yahooQuote =
        R"json({"quoteResponse":{"result":[{"regularMarketPrice":0,"regularMarketTime":1700009999,"regularMarketPreviousClose":76}]}})json";
    squarestar::marketdata::ApplyStockProviderPayloads(
        std::move(timestampOnlyPayload),
        {FetchKind::LiveQuote, 0, false, false, false, false},
        stalePriceGuard);
    Require(NearlyEqual(stalePriceGuard.marketData.currentPrice, 77.0) &&
                stalePriceGuard.marketData.quoteTimestamp == 1700000000,
            "timestamp-only Yahoo payload must not make an older displayed price look fresh");

    squarestar::market::StockData quotePatchTarget;
    quotePatchTarget.currentPrice = 55.0;
    quotePatchTarget.quoteTimestamp = 1700000100;
    quotePatchTarget.success = true;
    squarestar::market::StockData timestampOnlyPatch;
    timestampOnlyPatch.previousClose = 54.0;
    timestampOnlyPatch.quoteTimestamp = 1700000200;
    timestampOnlyPatch.success = true;
    Require(squarestar::application::ApplyStockQuotePatch(quotePatchTarget,
                                                          timestampOnlyPatch,
                                                          0) &&
                NearlyEqual(quotePatchTarget.currentPrice, 55.0) &&
                quotePatchTarget.quoteTimestamp == 1700000100,
            "quote patch timestamp must advance only with a replacement current price");

    return 0;
}
