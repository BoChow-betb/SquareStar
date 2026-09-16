#include "http_client.hpp"

#include "services/url_policy.hpp"
#include "services/token_bucket.hpp"

#include "platform/windows_headers.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

#include <shellapi.h>

#include "curl/curl.h"

namespace squarestar::http {
namespace {

constexpr size_t kDefaultMaxHttpResponseBytes = 2 * 1024 * 1024;

std::mutex g_CurlGlobalRuntimeMutex;
bool g_CurlGlobalInitSucceeded = false;

enum class ProviderCooldownSlot : std::size_t {
    Yahoo = 0,
    Finnhub = 1,
    Count = 2,
};

std::mutex g_ProviderCooldownMutex;
std::array<std::chrono::steady_clock::time_point,
           static_cast<std::size_t>(ProviderCooldownSlot::Count)>
    g_ProviderCooldownUntil{};

TokenBucket g_YahooQuoteBucket{12.0, 6.0};
TokenBucket g_FinnhubQuoteBucket{1.0, 2.0};

std::optional<ProviderCooldownSlot> ProviderForUrl(std::string_view url) noexcept {
    if (url.find("finance.yahoo.com") != std::string_view::npos ||
        url.find("fc.yahoo.com") != std::string_view::npos) {
        return ProviderCooldownSlot::Yahoo;
    }
    if (url.find("finnhub.io") != std::string_view::npos)
        return ProviderCooldownSlot::Finnhub;
    return std::nullopt;
}

bool IsRealtimeQuoteUrl(std::string_view url) noexcept {
    return (url.find("finance.yahoo.com/v7/finance/quote") != std::string_view::npos &&
            url.find("symbols=") != std::string_view::npos) ||
           (url.find("finnhub.io/api/v1/quote") != std::string_view::npos &&
            url.find("symbol=") != std::string_view::npos);
}

TokenBucket* QuoteBucketForUrl(std::string_view url) noexcept {
    if (!IsRealtimeQuoteUrl(url))
        return nullptr;
    const auto provider = ProviderForUrl(url);
    if (!provider)
        return nullptr;
    return *provider == ProviderCooldownSlot::Yahoo
               ? &g_YahooQuoteBucket
               : &g_FinnhubQuoteBucket;
}

bool WaitForQuoteProviderPermit(std::string_view url,
                                const HttpCancelCheck& cancelled) {
    TokenBucket* bucket = QuoteBucketForUrl(url);
    if (!bucket)
        return true;
    const auto startedAt = std::chrono::steady_clock::now();
    constexpr auto kMaximumAdmissionWait = std::chrono::milliseconds(1250);
    for (;;) {
        if (cancelled && cancelled())
            return false;
        const auto now = std::chrono::steady_clock::now();
        if (bucket->TryConsume(now))
            return true;
        if (now - startedAt >= kMaximumAdmissionWait)
            return false;
        auto wait = bucket->TimeUntilAvailable(now);
        wait = std::min(wait, std::chrono::milliseconds(50));
        if (wait <= std::chrono::milliseconds::zero())
            wait = std::chrono::milliseconds(1);
        std::this_thread::sleep_for(wait);
    }
}

bool ProviderCooldownActive(std::string_view url) {
    const auto provider = ProviderForUrl(url);
    if (!provider)
        return false;
    std::lock_guard<std::mutex> lock(g_ProviderCooldownMutex);
    return std::chrono::steady_clock::now() <
           g_ProviderCooldownUntil[static_cast<std::size_t>(*provider)];
}

void RecordProviderRateLimit(CURL* curl, std::string_view url) {
    const auto provider = ProviderForUrl(url);
    if (!provider)
        return;
    curl_off_t retryAfterSeconds = 0;
    if (!curl ||
        curl_easy_getinfo(curl, CURLINFO_RETRY_AFTER, &retryAfterSeconds) != CURLE_OK ||
        retryAfterSeconds <= 0) {
        retryAfterSeconds = 60;
    }
    retryAfterSeconds = std::clamp<curl_off_t>(retryAfterSeconds, 1, 300);
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::seconds(retryAfterSeconds);
    std::lock_guard<std::mutex> lock(g_ProviderCooldownMutex);
    auto& current = g_ProviderCooldownUntil[static_cast<std::size_t>(*provider)];
    if (until > current)
        current = until;
}

bool EnsureCurlGlobalRuntime() {
    std::lock_guard<std::mutex> lock(g_CurlGlobalRuntimeMutex);
    if (g_CurlGlobalInitSucceeded)
        return true;
    g_CurlGlobalInitSucceeded =
        curl_global_init(CURL_GLOBAL_WIN32 | CURL_GLOBAL_SSL) == CURLE_OK;
    return g_CurlGlobalInitSucceeded;
}

struct HttpWriteContext {
    std::string* body = nullptr;
    size_t maximumBytes = kDefaultMaxHttpResponseBytes;
    bool limitExceeded = false;
};

struct HttpProgressContext {
    const HttpCancelCheck* cancelled = nullptr;
};


size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* context = static_cast<HttpWriteContext*>(userp);
    if (!context || !context->body || (nmemb != 0 && size > SIZE_MAX / nmemb))
        return 0;
    const size_t byteCount = size * nmemb;
    if (byteCount > context->maximumBytes -
                        std::min(context->body->size(), context->maximumBytes)) {
        context->limitExceeded = true;
        return 0;
    }
    context->body->append(static_cast<const char*>(contents), byteCount);
    return byteCount;
}

int ProgressCallback(void* userp,
                     curl_off_t,
                     curl_off_t,
                     curl_off_t,
                     curl_off_t) {
    const auto* context = static_cast<const HttpProgressContext*>(userp);
    return context && context->cancelled && *context->cancelled &&
                   (*context->cancelled)()
               ? 1
               : 0;
}

const char* HttpUserAgentFor(const std::string& url) {
    if (url.find("finnhub.io/") != std::string::npos)
        return "SquareStar/1.0";
    return "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36";
}

size_t ExpectedHttpResponseBytes(const std::string& url) {
    if (url.find("/v8/finance/chart/") != std::string::npos &&
        url.find("interval=1d") != std::string::npos)
        return 16 * 1024;
    static constexpr std::array<std::pair<std::string_view, size_t>, 7> rules = {
        {{"/v8/finance/chart/", 256 * 1024},
         {"/finance/screener/", 128 * 1024},
         {"/company-news", 64 * 1024},
         {"/search?", 24 * 1024},
         {"/quote?", 4 * 1024},
         {"/profile2?", 4 * 1024},
         {"/metric?", 4 * 1024}}};
    for (const auto& [pattern, bytes] : rules)
        if (url.find(pattern) != std::string::npos)
            return bytes;
    return 16 * 1024;
}

size_t MaximumHttpResponseBytes(const std::string& url) {
    if (url.find("/v8/finance/chart/") != std::string::npos) {
        if (url.find("range=5d") != std::string::npos &&
            url.find("interval=1d") != std::string::npos)
            return 256 * 1024;
        return 4 * 1024 * 1024;
    }
    static constexpr std::array<std::pair<std::string_view, size_t>, 9> rules = {
        {{"/finance/screener/", 2 * 1024 * 1024},
         {"/finance/screener?", 2 * 1024 * 1024},
         {"/finance/trending/", 512 * 1024},
         {"/company-news", 1024 * 1024},
         {"/search?", 256 * 1024},
         {"/quote?", 1024 * 1024},
         {"/profile2?", 128 * 1024},
         {"/metric?", 256 * 1024},
         {"/getcrumb", 4 * 1024}}};
    for (const auto& [pattern, bytes] : rules)
        if (url.find(pattern) != std::string::npos)
            return bytes;
    return kDefaultMaxHttpResponseBytes;
}

constexpr auto kYahooSessionCrumbTtl = std::chrono::minutes(15);

}

std::string UrlEncode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.push_back(static_cast<char>(c));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[c >> 4]);
            encoded.push_back(hex[c & 0x0F]);
        }
    }
    return encoded;
}

bool OpenExternalHttpsUrl(const std::string& url) {
    if (!IsSafeExternalHttpsUrl(url))
        return false;
    const HINSTANCE result =
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

namespace {

struct ThreadCurlHandle {
    CURL* value = nullptr;

    ~ThreadCurlHandle() { Reset(); }

    void Reset() noexcept {
        if (!value)
            return;
        curl_easy_cleanup(value);
        value = nullptr;
    }
};

struct ThreadYahooSession {
    std::string crumb;
    std::chrono::steady_clock::time_point crumbSavedAt{};

    ~ThreadYahooSession() { Reset(); }

    void Reset() noexcept {
        std::fill(crumb.begin(), crumb.end(), '\0');
        crumb.clear();
        crumbSavedAt = {};
    }
};

ThreadYahooSession& ThreadYahooSessionStorage() {
    thread_local ThreadYahooSession session;
    return session;
}

void ResetCurrentThreadYahooSession() noexcept {
    ThreadYahooSessionStorage().Reset();
}

ThreadCurlHandle& ThreadCurlStorage() {
    thread_local ThreadCurlHandle holder;
    return holder;
}

CURL* ThreadCurl() {
    ThreadCurlHandle& holder = ThreadCurlStorage();
    if (!holder.value)
        holder.value = curl_easy_init();
    return holder.value;
}

void ResetCurrentThreadCurl() {
    ThreadCurlStorage().Reset();
}

HttpError ClassifyCurlError(CURLcode code, bool responseLimitExceeded) noexcept {
    if (responseLimitExceeded)
        return HttpError::ResponseTooLarge;
    switch (code) {
    case CURLE_OK:
        return HttpError::None;
    case CURLE_COULDNT_RESOLVE_HOST:
        return HttpError::DnsFailure;
    case CURLE_COULDNT_CONNECT:
        return HttpError::ConnectFailure;
    case CURLE_OPERATION_TIMEDOUT:
        return HttpError::Timeout;
    case CURLE_ABORTED_BY_CALLBACK:
        return HttpError::Cancelled;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
        return HttpError::TlsFailure;
    default:
        return HttpError::TransferFailure;
    }
}

void ConfigureReusableConnectionOptions(CURL* curl, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, HttpUserAgentFor(url));
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif


curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L);


curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 4L);
#if LIBCURL_VERSION_NUM >= 0x072F00
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
#if LIBCURL_VERSION_NUM >= 0x073E00
    curl_easy_setopt(curl, CURLOPT_MAXAGE_CONN, 300L);
#endif
}

}

bool InitializeHttpRuntimeWithoutNetwork() {
    return EnsureCurlGlobalRuntime() && ThreadCurl() != nullptr;
}

namespace {

HttpResponse PerformYahooHttpTransfer(CURL* curl,
                                      const std::string& url,
                                      const char* postBody,
                                      const HttpCancelCheck& cancelled) {
    if (ProviderCooldownActive(url))
        return HttpResponse{{}, 429, HttpError::HttpStatus};
    curl_easy_reset(curl);
    HttpResponse response;
    response.body.reserve(ExpectedHttpResponseBytes(url));
    const size_t maximumBytes = MaximumHttpResponseBytes(url);
    HttpWriteContext writeContext{&response.body, maximumBytes};
    HttpProgressContext progressContext{&cancelled};
    ConfigureReusableConnectionOptions(curl, url);


curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2200L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 6500L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 32L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeContext);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressContext);
    curl_easy_setopt(
        curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maximumBytes));

    curl_slist* headers = nullptr;
    if (postBody) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json,text/plain,*/*");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody);
        curl_easy_setopt(curl,
                         CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(std::char_traits<char>::length(postBody)));
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }

    const CURLcode result = curl_easy_perform(curl);
    if (headers)
        curl_slist_free_all(headers);
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
        if (response.statusCode == 429)
            RecordProviderRateLimit(curl, url);
        if (response.statusCode < 200 || response.statusCode >= 300)
            response.error = HttpError::HttpStatus;
    } else {
        response.error = ClassifyCurlError(result, writeContext.limitExceeded);
    }
    if (result != CURLE_OK)
        response.body.clear();


curl_easy_reset(curl);
    return response;
}

std::string CleanYahooCrumb(std::string crumb) {
    const auto notSpace = [](char raw) {
        return !std::isspace(static_cast<unsigned char>(raw));
    };
    const auto first = std::find_if(crumb.begin(), crumb.end(), notSpace);
    const auto last = std::find_if(crumb.rbegin(), crumb.rend(), notSpace).base();
    if (first >= last)
        return {};
    crumb = std::string(first, last);
    if (crumb.size() > 256 || crumb.find('<') != std::string::npos ||
        crumb.find("Too Many Requests") != std::string::npos) {
        return {};
    }
    return crumb;
}


bool AcquireYahooCrumb(CURL* curl,
                       std::string& crumb,
                       HttpResponse& lastResponse,
                       const HttpCancelCheck& cancelled) {
    if (cancelled && cancelled()) {
        lastResponse = HttpResponse{{}, 0, HttpError::Cancelled};
        return false;
    }

    ThreadYahooSession& session = ThreadYahooSessionStorage();
    const auto now = std::chrono::steady_clock::now();
    if (!session.crumb.empty() &&
        now - session.crumbSavedAt < kYahooSessionCrumbTtl) {
        crumb = session.crumb;
        return true;
    }

    HttpResponse crumbResponse{{}, 0, HttpError::TransferFailure};
    std::string refreshedCrumb;
    try {


(void)PerformYahooHttpTransfer(
            curl, "https://fc.yahoo.com", nullptr, cancelled);
        crumbResponse = PerformYahooHttpTransfer(
            curl,
            "https://query1.finance.yahoo.com/v1/test/getcrumb",
            nullptr,
            cancelled);
        if (crumbResponse.IsSuccess())
            refreshedCrumb = CleanYahooCrumb(crumbResponse.body);
    } catch (...) {
        crumbResponse = HttpResponse{{}, 0, HttpError::TransferFailure};
        refreshedCrumb.clear();
    }

    if (crumbResponse.IsSuccess() && !refreshedCrumb.empty()) {
        session.crumb = std::move(refreshedCrumb);
        session.crumbSavedAt = std::chrono::steady_clock::now();
        crumb = session.crumb;
        return true;
    }

    session.Reset();
    if (!crumbResponse.IsSuccess()) {
        lastResponse = std::move(crumbResponse);
    } else {
        lastResponse = HttpResponse{{},
                                    crumbResponse.statusCode,
                                    HttpError::TransferFailure};
    }
    return false;
}

void InvalidateYahooCrumbIfCurrent(const std::string& crumb) {
    ThreadYahooSession& session = ThreadYahooSessionStorage();
    if (session.crumb == crumb)
        session.Reset();
}

}


HttpResponse PerformYahooAuthenticatedGet(const std::string& url,
                                          const HttpCancelCheck& cancelled) {
    if (!IsYahooFinanceApiUrl(url))
        return HttpResponse{{}, 0, HttpError::InvalidUrl};
    if (!EnsureCurlGlobalRuntime())
        return HttpResponse{{}, 0, HttpError::RuntimeUnavailable};
    CURL* curl = ThreadCurl();
    if (!curl)
        return HttpResponse{{}, 0, HttpError::RuntimeUnavailable};

    HttpResponse lastResponse{{}, 0, HttpError::TransferFailure};
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (cancelled && cancelled())
            return HttpResponse{{}, 0, HttpError::Cancelled};
        std::string crumb;
        if (!AcquireYahooCrumb(curl, crumb, lastResponse, cancelled))
            continue;
        const char separator = url.find('?') == std::string::npos ? '?' : '&';
        const std::string authenticatedUrl =
            url + separator + "crumb=" + UrlEncode(crumb);
        lastResponse = PerformYahooHttpTransfer(curl, authenticatedUrl, nullptr, cancelled);
        if (lastResponse.IsSuccess())
            return lastResponse;
        InvalidateYahooCrumbIfCurrent(crumb);
    }
    return lastResponse;
}

HttpResponse PerformYahooScreenerPost(const std::string& jsonBody,
                                     const HttpCancelCheck& cancelled) {
    if (jsonBody.empty() || jsonBody.size() > 64 * 1024)
        return HttpResponse{{}, 0, HttpError::InvalidUrl};
    if (!EnsureCurlGlobalRuntime())
        return HttpResponse{{}, 0, HttpError::RuntimeUnavailable};
    CURL* curl = ThreadCurl();
    if (!curl)
        return HttpResponse{{}, 0, HttpError::RuntimeUnavailable};

    HttpResponse lastResponse{{}, 0, HttpError::TransferFailure};
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (cancelled && cancelled())
            return HttpResponse{{}, 0, HttpError::Cancelled};
        std::string crumb;
        if (!AcquireYahooCrumb(curl, crumb, lastResponse, cancelled))
            continue;

        const std::string url =
            "https://query1.finance.yahoo.com/v1/finance/screener?"
            "corsDomain=finance.yahoo.com&formatted=false&lang=en-US&region=US&crumb=" +
            UrlEncode(crumb);
        lastResponse = PerformYahooHttpTransfer(curl, url, jsonBody.c_str(), cancelled);
        if (lastResponse.IsSuccess())
            return lastResponse;


InvalidateYahooCrumbIfCurrent(crumb);
    }
    return lastResponse;
}

HttpResponse PerformHttpRequest(const std::string& url,
                                const HttpCancelCheck& cancelled) {
    if (!IsAllowedApiUrl(url))
        return HttpResponse{{}, 0, HttpError::InvalidUrl};
    if (ProviderCooldownActive(url))
        return HttpResponse{{}, 429, HttpError::HttpStatus};
    if (!WaitForQuoteProviderPermit(url, cancelled))
        return HttpResponse{{}, 0, cancelled && cancelled()
                                       ? HttpError::Cancelled
                                       : HttpError::ExecutorRejected};
    if (ProviderCooldownActive(url))
        return HttpResponse{{}, 429, HttpError::HttpStatus};
    if (!EnsureCurlGlobalRuntime())
        return HttpResponse{{}, 0, HttpError::RuntimeUnavailable};
    CURL* curl = ThreadCurl();
    if (!curl)
        return HttpResponse{{}, 0, HttpError::RuntimeUnavailable};
    if (cancelled && cancelled())
        return HttpResponse{{}, 0, HttpError::Cancelled};
    curl_easy_reset(curl);
    HttpResponse response;
    response.body.reserve(ExpectedHttpResponseBytes(url));
    const size_t maximumBytes = MaximumHttpResponseBytes(url);
    HttpWriteContext writeContext{&response.body, maximumBytes};
    HttpProgressContext progressContext{&cancelled};
    ConfigureReusableConnectionOptions(curl, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    const bool optionalFinnhubDetail =
        url.find("finnhub.io/api/v1/stock/profile2") != std::string::npos ||
        url.find("finnhub.io/api/v1/stock/metric") != std::string::npos ||
        url.find("finnhub.io/api/v1/company-news") != std::string::npos;
    const bool lightweightScreenerTrend =
        url.find("/v8/finance/chart/") != std::string::npos &&
        url.find("range=5d") != std::string::npos &&
        url.find("interval=1d") != std::string::npos;
    curl_easy_setopt(curl,
                     CURLOPT_CONNECTTIMEOUT_MS,
                     optionalFinnhubDetail ? 1000L : lightweightScreenerTrend ? 1400L : 1800L);
    curl_easy_setopt(curl,
                     CURLOPT_TIMEOUT_MS,
                     optionalFinnhubDetail ? 2500L : lightweightScreenerTrend ? 3500L : 5500L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 32L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeContext);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressContext);
    curl_easy_setopt(
        curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maximumBytes));
    const CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
        if (response.statusCode == 429)
            RecordProviderRateLimit(curl, url);
        if (response.statusCode < 200 || response.statusCode >= 300)
            response.error = HttpError::HttpStatus;
    } else {
        response.error = ClassifyCurlError(result, writeContext.limitExceeded);
    }
    if (result != CURLE_OK)
        response.body.clear();


    curl_easy_reset(curl);
    return response;
}


const char* HttpErrorUserMessage(HttpError error) noexcept {
    switch (error) {
    case HttpError::None:
        return "";
    case HttpError::DnsFailure:
    case HttpError::ConnectFailure:
        return "Network unavailable";
    case HttpError::Timeout:
        return "Network request timed out";
    case HttpError::TlsFailure:
        return "Secure connection failed";
    case HttpError::ResponseTooLarge:
        return "Provider response exceeded the safety limit";
    case HttpError::Cancelled:
        return "Market-data request cancelled";
    case HttpError::ExecutorRejected:
        return "Market-data request queue is busy; retry shortly";
    case HttpError::HttpStatus:
        return "Market-data provider returned an HTTP error";
    case HttpError::InvalidUrl:
    case HttpError::RuntimeUnavailable:
    case HttpError::TransferFailure:
        return "Market-data request failed";
    }
    return "Market-data request failed";
}

void ShutdownHttpClient() {
    ResetCurrentThreadYahooSession();
    {
        std::lock_guard<std::mutex> lock(g_ProviderCooldownMutex);
        g_ProviderCooldownUntil = {};
    }


ResetCurrentThreadCurl();
    std::lock_guard<std::mutex> lock(g_CurlGlobalRuntimeMutex);
    if (!g_CurlGlobalInitSucceeded)
        return;
    curl_global_cleanup();
    g_CurlGlobalInitSucceeded = false;
}

}
