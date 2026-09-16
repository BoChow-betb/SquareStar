#include "services/url_policy.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

}

int main() {
    using squarestar::http::IsAllowedApiUrl;
    using squarestar::http::IsSafeExternalHttpsUrl;
    using squarestar::http::IsYahooFinanceApiUrl;

    Check(IsAllowedApiUrl("https://finnhub.io/api/v1/quote?symbol=AAPL"),
          "Finnhub API URL should be allowed");
    Check(IsAllowedApiUrl("https://query1.finance.yahoo.com/v8/finance/chart/AAPL"),
          "Yahoo query1 API URL should be allowed");
    Check(IsAllowedApiUrl("https://query2.finance.yahoo.com/"),
          "Yahoo query2 API URL should be allowed");
    Check(!IsAllowedApiUrl("http://finnhub.io/api/v1/quote"),
          "plain HTTP must be rejected");
    Check(!IsAllowedApiUrl("https://finnhub.io.evil.example/api/v1/quote"),
          "lookalike host suffix must be rejected");
    Check(!IsAllowedApiUrl("https://user@finnhub.io/api/v1/quote"),
          "userinfo must be rejected");
    Check(!IsAllowedApiUrl("https://finnhub.io:443/api/v1/quote"),
          "non-canonical authority must be rejected");
    Check(!IsAllowedApiUrl("https://example.com/?next=finnhub.io"),
          "allowlisted hostname in a query must not bypass host validation");
    Check(!IsAllowedApiUrl(std::string("https://finnhub.io/") +
                           std::string(8200, 'a')),
          "oversized API URL must be rejected");

    Check(IsYahooFinanceApiUrl(
              "https://query1.finance.yahoo.com/v1/test/getcrumb"),
          "Yahoo query host should be recognized");
    Check(!IsYahooFinanceApiUrl("https://finnhub.io/api/v1/quote"),
          "non-Yahoo allowlisted host must not be treated as Yahoo");

    Check(IsSafeExternalHttpsUrl("https://example.com/news?id=1#story"),
          "normal external HTTPS URL should be allowed");
    Check(IsSafeExternalHttpsUrl("https://finnhub.io/"),
          "Finnhub notification link should be allowed");
    Check(!IsSafeExternalHttpsUrl("https:///missing-host"),
          "external URL with no authority must be rejected");
    Check(!IsSafeExternalHttpsUrl("https://user@example.com/news"),
          "external URL credentials must be rejected");
    Check(!IsSafeExternalHttpsUrl("https://example.com/line\nbreak"),
          "external URL control characters must be rejected");
    Check(!IsSafeExternalHttpsUrl("javascript:alert(1)"),
          "non-HTTPS external scheme must be rejected");
    Check(!IsSafeExternalHttpsUrl("https://localhost/news") &&
              !IsSafeExternalHttpsUrl("https://localhost./news") &&
              !IsSafeExternalHttpsUrl("https://127.0.0.1/news") &&
              !IsSafeExternalHttpsUrl("https://169.254.169.254/latest/meta-data") &&
              !IsSafeExternalHttpsUrl("https://[::1]/news"),
          "loopback, link-local and literal external-link destinations must fail closed");
    Check(!IsSafeExternalHttpsUrl("https://example.com:8443/news"),
          "external links with custom ports must fail closed");


    if (failures != 0)
        return 1;
    std::cout << "All URL policy tests passed\n";
    return 0;
}
