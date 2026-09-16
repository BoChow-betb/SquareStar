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


HttpResponse PerformYahooAuthenticatedGet(const std::string& url,
                                          const HttpCancelCheck& cancelled = {});
HttpResponse PerformYahooScreenerPost(const std::string& jsonBody,
                                     const HttpCancelCheck& cancelled = {});
const char* HttpErrorUserMessage(HttpError error) noexcept;
std::string UrlEncode(const std::string& value);
bool OpenExternalHttpsUrl(const std::string& url);


bool InitializeHttpRuntimeWithoutNetwork();
void ShutdownHttpClient();

}
