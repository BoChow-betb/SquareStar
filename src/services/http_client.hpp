#pragma once

#include <functional>
#include <string>

namespace squarestar::http {

enum class HttpError {
    None,
    InvalidUrl,
    RuntimeUnavailable,
    DnsFailure,
    ConnectFailure,
    Timeout,
    TlsFailure,
    ResponseTooLarge,
    Cancelled,
    ExecutorRejected,
    TransferFailure,
    HttpStatus
};

struct HttpResponse {
    std::string body;
    long statusCode = 0;
    HttpError error = HttpError::None;

    [[nodiscard]] bool IsSuccess() const noexcept {
        return error == HttpError::None && statusCode >= 200 && statusCode < 300;
    }
};

using HttpCancelCheck = std::function<bool()>;

HttpResponse PerformHttpRequest(const std::string& url,
                                const HttpCancelCheck& cancelled = {});
// Performs a Yahoo Finance GET with the cookie/crumb pair required by its
// protected list, quote, and spark endpoints. Public chart requests should
// continue to use PerformHttpRequest.
HttpResponse PerformYahooAuthenticatedGet(const std::string& url,
                                          const HttpCancelCheck& cancelled = {});
HttpResponse PerformYahooScreenerPost(const std::string& jsonBody,
                                     const HttpCancelCheck& cancelled = {});
const char* HttpErrorUserMessage(HttpError error) noexcept;
std::string UrlEncode(const std::string& value);
bool OpenExternalHttpsUrl(const std::string& url);
// Benchmark/diagnostic helper: initializes libcurl and one reusable easy handle
// without DNS, socket, TLS, or HTTP activity.
bool InitializeHttpRuntimeWithoutNetwork();
void ShutdownHttpClient();

} // namespace squarestar::http
