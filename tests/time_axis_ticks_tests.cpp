#include "domain/market_calendar.hpp"
#include "domain/time_axis_ticks.hpp"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::time_t UtcTime(int year, int month, int day, int hour = 12) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    return squarestar::market::UtcTmToTimeT(tm);
}

squarestar::market::TimeAxisCalendarDate ResolveUtc(std::time_t value) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_yday, true};
}

bool Near(double actual, double expected, double tolerance = 0.5) {
    return std::abs(actual - expected) <= tolerance;
}

}

int main() {
    using squarestar::market::BuildSparseTimeAxisTicks;

    const std::vector<double> juneStart = {
        (double)UtcTime(2026, 6, 12),
        (double)UtcTime(2026, 6, 18),
        (double)UtcTime(2026, 6, 30),
        (double)UtcTime(2026, 7, 1),
        (double)UtcTime(2026, 7, 15),
        (double)UtcTime(2026, 8, 3),
    };
    const auto ytd = BuildSparseTimeAxisTicks(juneStart, 3, 0.0, 1.0, ResolveUtc);
    Check(ytd.size() == 3, "YTD emits one sparse tick per visible month when fewer than five months exist");
    Check(!ytd.empty() && ytd.front().label == "Jun" &&
              Near(ytd.front().value, (double)UtcTime(2026, 6, 12)),
          "YTD anchors June at the first visible June sample instead of a late-June sample");
    Check(ytd.size() >= 3 && ytd[1].label == "Jul" && ytd[2].label == "Aug",
          "YTD calendar labels advance by visible month buckets");

    const auto oneYear = BuildSparseTimeAxisTicks(juneStart, 4, 0.0, 1.0, ResolveUtc);
    Check(oneYear.size() == ytd.size() && oneYear.front().label == "Jun" &&
              Near(oneYear.front().value, ytd.front().value),
          "1Y uses the same first-visible-month anchoring as YTD");

    const double compressedOrigin = (double)UtcTime(2026, 6, 1, 0);
    std::vector<double> compressedDays;
    compressedDays.reserve(juneStart.size());
    for (double timestamp : juneStart)
        compressedDays.push_back((timestamp - compressedOrigin) / 86400.0);
    const auto compressedYtd =
        BuildSparseTimeAxisTicks(compressedDays, 3, compressedOrigin, 86400.0, ResolveUtc);
    Check(!compressedYtd.empty() && compressedYtd.front().label == "Jun" &&
              Near(compressedYtd.front().value, compressedDays.front(), 0.000001),
          "calendar bucketing stays correct on the comparison chart's elapsed-day axis");

    const std::vector<double> manyMonths = {
        (double)UtcTime(2026, 1, 5), (double)UtcTime(2026, 2, 2),
        (double)UtcTime(2026, 3, 2), (double)UtcTime(2026, 4, 1),
        (double)UtcTime(2026, 5, 1), (double)UtcTime(2026, 6, 1),
        (double)UtcTime(2026, 7, 1), (double)UtcTime(2026, 8, 3),
    };
    const auto sampledMonths = BuildSparseTimeAxisTicks(manyMonths, 4, 0.0, 1.0, ResolveUtc);
    Check(sampledMonths.size() == 5, "long monthly ranges stay capped at five labels");
    Check(sampledMonths.front().label == "Jan" && sampledMonths.back().label == "Aug",
          "monthly sampling keeps both visible calendar endpoints");

    const std::vector<double> years = {
        (double)UtcTime(2022, 6, 15), (double)UtcTime(2023, 1, 3),
        (double)UtcTime(2024, 1, 2),  (double)UtcTime(2025, 1, 2),
        (double)UtcTime(2026, 1, 2),
    };
    const auto fiveYear = BuildSparseTimeAxisTicks(years, 5, 0.0, 1.0, ResolveUtc);
    Check(fiveYear.size() == 5 && fiveYear.front().label == "2022" &&
              fiveYear.back().label == "2026",
          "5Y labels use real visible year buckets");
    Check(Near(fiveYear.front().value, (double)UtcTime(2022, 6, 15)),
          "5Y anchors a partial first year at its first visible sample");

    const std::vector<double> oneMonth = {
        (double)UtcTime(2026, 6, 12), (double)UtcTime(2026, 6, 18),
        (double)UtcTime(2026, 6, 24), (double)UtcTime(2026, 6, 30),
    };
    const auto oneMonthTicks = BuildSparseTimeAxisTicks(oneMonth, 2, 0.0, 1.0, ResolveUtc);
    Check(oneMonthTicks.size() == 5 && oneMonthTicks.front().label == "Jun 12" &&
              oneMonthTicks.back().label == "Jun 30",
          "1M keeps the existing five day-oriented labels and visible endpoints");

    const std::vector<double> invalid = {2.0, 1.0};
    Check(BuildSparseTimeAxisTicks(invalid, 4, 0.0, 1.0, ResolveUtc).empty(),
          "invalid reversed axis data produces no sparse ticks");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All SquareStar time-axis regression tests passed\n";
    return EXIT_SUCCESS;
}
