#include "services/symbol_search_service.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

constexpr const char* kYahooAppleResponse =
    R"({"quotes":[{"symbol":"AAPL","shortname":"Apple Inc.","longname":"Apple Inc.","quoteType":"EQUITY","exchange":"NMS"},{"symbol":"AAPL.L","shortname":"Apple London","quoteType":"EQUITY","exchange":"LSE"},{"symbol":"^GSPC","shortname":"S&P 500","quoteType":"INDEX","exchange":"SNP"}]})";

} // namespace

int main() {
    using squarestar::search::SymbolSearchDependencies;
    using squarestar::search::SymbolSearchHttpResponse;
    using squarestar::search::SymbolSearchService;

    int fetchCount = 0;
    std::string firstFinnhubUrl;
    SymbolSearchService service(SymbolSearchDependencies{
        [] { return std::string("test-key"); },
        [](std::string_view value) { return std::string(value); },
        [&](std::string url) {
            ++fetchCount;
            if (firstFinnhubUrl.empty())
                firstFinnhubUrl = url;
            return SymbolSearchHttpResponse{
                R"({"result":[{"symbol":"AAPL","displaySymbol":"AAPL","description":"Apple Inc","type":"Common Stock"},{"symbol":"AAP","displaySymbol":"AAP","description":"Advance Auto Parts Inc","type":"Common Stock"}]})",
                200,
                true};
        },
    });

    const auto first = service.Lookup("apple");
    Check(!first.empty() && first.front().first == "AAPL",
          "company-name search ranks the strongest provider match first");
    Check(firstFinnhubUrl.find("/api/v1/search?q=apple&exchange=US&token=") !=
              std::string::npos,
          "Finnhub symbol lookup is constrained to the US exchange");
    const auto second = service.Lookup("apple");
    Check(fetchCount == 1 && second == first,
          "successful symbol lookup is served from the bounded cache");

    service.ClearCache();
    (void)service.Lookup("apple", true);
    Check(fetchCount == 2,
          "bypassCache forces an explicit provider refresh");

    int rateLimitedFinnhubFetches = 0;
    int rateLimitedYahooFetches = 0;
    SymbolSearchService rateLimited(SymbolSearchDependencies{
        [] { return std::string("test-key"); },
        [](std::string_view value) { return std::string(value); },
        [&](std::string url) {
            if (url.find("finnhub.io") != std::string::npos) {
                ++rateLimitedFinnhubFetches;
                return SymbolSearchHttpResponse{{}, 429, false};
            }
            ++rateLimitedYahooFetches;
            return SymbolSearchHttpResponse{kYahooAppleResponse, 200, true};
        },
    });
    const auto rateLimitedFirst = rateLimited.Lookup("apple");
    const auto rateLimitedSecond = rateLimited.Lookup("apple", true);
    Check(!rateLimitedFirst.empty() && rateLimitedFirst.front().first == "AAPL" &&
              rateLimitedSecond == rateLimitedFirst,
          "Finnhub rate limits degrade to the Yahoo symbol lookup fallback");
    Check(rateLimitedFinnhubFetches == 1 && rateLimitedYahooFetches == 2,
          "Finnhub 429 starts a short provider cooldown without suppressing Yahoo search");

    SymbolSearchService supportedOnly(SymbolSearchDependencies{
        [] { return std::string("test-key"); },
        [](std::string_view value) { return std::string(value); },
        [](std::string) {
            return SymbolSearchHttpResponse{
                R"({"result":[
                  {"symbol":"AAPL.L","displaySymbol":"AAPL","description":"Apple London","type":"Common Stock"},
                  {"symbol":"AAPLW","displaySymbol":"AAPLW","description":"Apple Warrant","type":"Warrant"},
                  {"symbol":"ABCDEFG","displaySymbol":"ABCDEFG","description":"Unsupported Long Symbol","type":"Common Stock"},
                  {"symbol":"AAPL","displaySymbol":"AAPL","description":"Apple Inc","type":"Common Stock"}
                ]})",
                200,
                true};
        },
    });
    const auto supportedResults = supportedOnly.Lookup("apple");
    Check(supportedResults.size() == 1 && supportedResults.front().first == "AAPL",
          "search recommendations exclude disguised international and unsupported instruments");

    int emptyFetches = 0;
    SymbolSearchService emptyResults(SymbolSearchDependencies{
        [] { return std::string("test-key"); },
        [](std::string_view value) { return std::string(value); },
        [&](std::string) {
            ++emptyFetches;
            return SymbolSearchHttpResponse{R"({"result":[]})", 200, true};
        },
    });
    Check(emptyResults.Lookup("definitely-no-match").empty() &&
              emptyResults.Lookup("definitely-no-match").empty() && emptyFetches == 1,
          "successful empty results are cached instead of being mistaken for a cache miss");

    int throwingFinnhubFetches = 0;
    int throwingYahooFetches = 0;
    SymbolSearchService throwingProvider(SymbolSearchDependencies{
        [] { return std::string("test-key"); },
        [](std::string_view value) { return std::string(value); },
        [&](std::string url) -> SymbolSearchHttpResponse {
            if (url.find("finnhub.io") != std::string::npos) {
                ++throwingFinnhubFetches;
                throw 7;
            }
            ++throwingYahooFetches;
            return SymbolSearchHttpResponse{kYahooAppleResponse, 200, true};
        },
    });
    const auto providerFallback = throwingProvider.Lookup("apple");
    Check(!providerFallback.empty() && providerFallback.front().first == "AAPL" &&
              throwingFinnhubFetches == 1 && throwingYahooFetches == 1,
          "Finnhub provider exceptions degrade to the keyless Yahoo search surface");

    int noKeyFetches = 0;
    std::string noKeyUrl;
    SymbolSearchService noKey(SymbolSearchDependencies{
        [] { return std::string(); },
        [](std::string_view value) { return std::string(value); },
        [&](std::string url) {
            ++noKeyFetches;
            noKeyUrl = std::move(url);
            return SymbolSearchHttpResponse{kYahooAppleResponse, 200, true};
        },
    });
    const auto noKeyEquity = noKey.Lookup("apple");
    Check(noKeyFetches == 1 &&
              noKeyUrl.find("query1.finance.yahoo.com/v1/finance/search") != std::string::npos &&
              !noKeyEquity.empty() && noKeyEquity.front().first == "AAPL",
          "equity recommendations remain available without a Finnhub API key");
    Check(noKeyEquity.size() == 1,
          "Yahoo fallback filters international listings and unsupported asset types");

    SymbolSearchService noKeyNetworkDown(SymbolSearchDependencies{
        [] { return std::string(); },
        [](std::string_view value) { return std::string(value); },
        [](std::string) { return SymbolSearchHttpResponse{}; },
    });
    const auto futures = noKeyNetworkDown.Lookup("nasdaq futures");
    Check(!futures.empty(),
          "built-in futures search remains available when keyless Yahoo lookup is unavailable");

    if (failures != 0)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
