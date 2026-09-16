#include "domain/market_runtime.hpp"

#include "domain/market_calendar.hpp"

#include <chrono>
#include <ctime>

namespace squarestar::market {

bool CachedMarketOpen() {
    thread_local bool open = false;
    thread_local auto lastCheck = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (lastCheck == std::chrono::steady_clock::time_point{} ||
        now - lastCheck >= std::chrono::seconds(1)) {
        open = IsMarketOpenAt(std::time(nullptr));
        lastCheck = now;
    }
    return open;
}

bool IsMarketOpeningWindow() {
    return IsMarketOpeningWindowAt(std::time(nullptr));
}

}
