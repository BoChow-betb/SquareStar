#include "services/provider_payload_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t maximumPayloadBytes = 1024 * 1024;
    if (size > maximumPayloadBytes)
        return 0;
    const std::string_view view(reinterpret_cast<const char*>(data), size);
    (void)squarestar::providers::ParseFinnhubQuotePayload(view);
    const std::string payload(view);
    squarestar::market::StockData destination;
    (void)squarestar::providers::ApplyYahooChartPayload(payload, true, destination);
    return 0;
}
