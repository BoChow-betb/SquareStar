#include "domain/chart_ranges.hpp"

namespace squarestar::market {

const TimeRangeConfig TIME_RANGES[TIME_RANGE_COUNT] = {{"1D", "1d", "2m"},
                                                       {"5D", "5d", "15m"},
                                                       {"1M", "1mo", "1d"},
                                                       {"YTD", "ytd", "1d"},
                                                       {"1Y", "1y", "1d"},
                                                       {"5Y", "5y", "1wk"},
                                                       {"All", "max", "1mo"}};

}
