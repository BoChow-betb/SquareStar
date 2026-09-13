#include "services/url_policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace squarestar::http {
namespace {

constexpr std::string_view kHttpsPrefix = "https://";

std::string_view HttpsAuthority(std::string_view url,
                                std::size_t maximumLength) noexcept {
    if (url.size() <= kHttpsPrefix.size() || url.size() > maximumLength ||
        !url.starts_with(kHttpsPrefix)) {
        return {};
    }
    const std::size_t authorityEnd =
        url.find_first_of("/?#", kHttpsPrefix.size());
    return url.substr(
        kHttpsPrefix.size(),
        authorityEnd == std::string_view::npos
            ? std::string_view::npos
            : authorityEnd - kHttpsPrefix.size());
}


} // namespace

bool IsAllowedApiUrl(std::string_view url) noexcept {
    const std::string_view host = HttpsAuthority(url, 8192);
    static constexpr std::array allowedHosts = {
        std::string_view("query1.finance.yahoo.com"),
        std::string_view("query2.finance.yahoo.com"),
        std::string_view("finnhub.io")};
    return !host.empty() &&
           std::find(allowedHosts.begin(), allowedHosts.end(), host) !=
               allowedHosts.end();
}

bool IsYahooFinanceApiUrl(std::string_view url) noexcept {
    if (!IsAllowedApiUrl(url))
        return false;
    const std::string_view host = HttpsAuthority(url, 8192);
    return host == "query1.finance.yahoo.com" ||
           host == "query2.finance.yahoo.com";
}

bool IsSafeExternalHttpsUrl(std::string_view url) noexcept {
    const std::string_view authority = HttpsAuthority(url, 4096);
    if (authority.empty() || authority.find('@') != std::string_view::npos)
        return false;
    if (std::any_of(url.begin(), url.end(), [](unsigned char character) {
        return character <= 0x20 || character == 0x7F;
    })) {
        return false;
    }

    // System-browser links use ordinary DNS hostnames only. Reject literal
    // addresses, credentials, and custom ports so notification/news URLs cannot
    // smuggle surprising authority syntax into ShellExecute.
    if (authority.front() == '[' || authority.find(':') != std::string_view::npos)
        return false;
    std::string host(authority);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    while (!host.empty() && host.back() == '.')
        host.pop_back();
    if (host.empty() || host.find('.') == std::string::npos ||
        host == "localhost" || host.ends_with(".localhost")) {
        return false;
    }
    const bool looksLikeIpv4 = std::all_of(host.begin(), host.end(), [](unsigned char value) {
        return std::isdigit(value) != 0 || value == '.';
    });
    return !looksLikeIpv4;
}

} // namespace squarestar::http
