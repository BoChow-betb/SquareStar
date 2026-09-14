#include "domain/trading_status.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>

namespace squarestar::market {
namespace {

std::string LowerAscii(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

bool ContainsAny(const std::string& text,
                 std::initializer_list<std::string_view> needles) {
    return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
        return text.find(needle) != std::string::npos;
    });
}

int EvidenceStrength(CorporateActionStatus status) {
    switch (status) {
    case CorporateActionStatus::Acquired:
        return 4;
    case CorporateActionStatus::Delisted:
        return 3;
    case CorporateActionStatus::Suspended:
        return 2;
    case CorporateActionStatus::SuspectedStopped:
        return 1;
    case CorporateActionStatus::None:
        return 0;
    }
    return 0;
}

} // namespace

CorporateActionStatus ClassifyCorporateActionNews(
    const std::vector<NewsItem>& news,
    std::string* evidenceHeadline) {
    CorporateActionStatus best = CorporateActionStatus::None;
    for (const NewsItem& item : news) {
        const std::string text = LowerAscii(item.headline + " " + item.summary);
        CorporateActionStatus candidate = CorporateActionStatus::None;
        if (ContainsAny(text, {"completes acquisition", "completed acquisition",
                               "completion of acquisition", "acquisition completed",
                               "acquisition closes", "acquisition closed", "acquired by",
                               "completion of merger", "merger completed",
                               "merger completion", "merger closes", "merger closed"})) {
            candidate = CorporateActionStatus::Acquired;
        } else if (ContainsAny(text, {"form 25", "delisted", "delisting",
                                      "removed from listing", "no longer listed",
                                      "ceased trading", "ceases trading"})) {
            candidate = CorporateActionStatus::Delisted;
        } else if (ContainsAny(text, {"trading suspended", "suspends trading",
                                      "halted indefinitely"})) {
            candidate = CorporateActionStatus::Suspended;
        }
        if (EvidenceStrength(candidate) > EvidenceStrength(best)) {
            best = candidate;
            if (evidenceHeadline)
                *evidenceHeadline = item.headline;
        }
    }
    return best;
}

std::string TradingStatusExchangeLabel(std::string_view exchange,
                                       const TradingStatus& status) {
    std::string label;
    switch (status.corporateAction) {
    case CorporateActionStatus::Acquired:
        label = "Acquired / merger completed";
        if (!exchange.empty()) {
            label.append(" \xE2\x80\xA2 ");
            label.append(exchange);
        }
        return label;
    case CorporateActionStatus::Delisted:
        label = "Delisted";
        break;
    case CorporateActionStatus::Suspended:
        label = "Suspended";
        break;
    case CorporateActionStatus::SuspectedStopped:
        label = "Delisted / suspended";
        break;
    case CorporateActionStatus::None:
        return std::string(exchange);
    }
    if (!exchange.empty()) {
        label.append(" from ");
        label.append(exchange);
    }
    return label;
}

} // namespace squarestar::market
