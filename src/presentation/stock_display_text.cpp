#include "presentation/stock_display_text.hpp"

#include "domain/text.hpp"

#include <array>

namespace squarestar::presentation {

std::string CleanExchangeLabel(std::string_view raw) {
    std::string upper(raw);
    squarestar::text::UppercaseInPlace(upper);
    if (upper.find("NASDAQ") != std::string::npos || upper == "NMS" || upper == "NGM" ||
        upper == "NCM" || upper == "NAS") {
        return "NASDAQ";
    }
    if (upper.find("NEW YORK") != std::string::npos || upper == "NYQ" || upper == "NYSE") {
        return "NYSE";
    }
    if (upper.find("AMEX") != std::string::npos || upper == "ASE") {
        return "NYSE AMERICAN";
    }
    return raw.empty() ? "US MARKET" : upper;
}

std::string CleanCompanyDisplayName(std::string_view raw) {
    std::string name(raw);
    static constexpr std::array<std::string_view, 8> suffixes = {
        ", Inc.", ", Inc", " Inc.", " Corporation", " Corp.", " Corp", " Ltd.", " plc"};
    for (const std::string_view suffix : suffixes) {
        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name.erase(name.size() - suffix.size());
            break;
        }
    }
    return name.empty() ? std::string(raw) : name;
}


}
