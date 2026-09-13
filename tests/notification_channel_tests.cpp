#include "application/notification_channel.hpp"
#include "application/notification_text.hpp"
#include "application/runtime_state.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

squarestar::application::StockMoveNotification Move(const char* ticker,
                                                    double before,
                                                    double after) {
    squarestar::application::StockMoveNotification row;
    row.ticker = ticker;
    row.before = before;
    row.after = after;
    row.currency = "USD";
    return row;
}

} // namespace

int main() {
    using squarestar::application::NativeNotificationChannel;
    using squarestar::application::NativeNotificationRequest;
    using squarestar::application::ApplicationRuntime;
    using squarestar::application::kNotificationMaxVisibleBlocks;

    NativeNotificationChannel liveGrouped;
    liveGrouped.QueuePriceMove(Move("FAST", 10.0, 10.5), true);
    auto firstLive = liveGrouped.TakeReadyPriceMoveNotifications(true);
    Require(firstLive.size() == 1 && firstLive.front().groupId != 0 &&
                firstLive.front().title == "[Market move] FAST",
            "the first background move should publish immediately instead of waiting 1.5 seconds");
    Require(firstLive.front().actionTicker == "FAST",
            "single-stock native moves should carry a ticker quick-open action");
    const std::uint64_t liveGroupId = firstLive.front().groupId;
    liveGrouped.QueuePriceMove(Move("NEXT", 20.0, 20.5), true);
    auto updatedLive = liveGrouped.TakeReadyPriceMoveNotifications(true);
    Require(updatedLive.size() == 1 && updatedLive.front().groupId == liveGroupId &&
                updatedLive.front().title == "[Market moves] 2 stocks" &&
                updatedLive.front().message.find("FAST:") != std::string::npos &&
                updatedLive.front().message.find("NEXT:") != std::string::npos,
            "nearby background moves should update the same visible group in place");
    Require(updatedLive.front().actionTicker.empty(),
            "grouped native move cards should not choose an ambiguous ticker action");

    for (int index = 0; index < 4; ++index) {
        const std::string ticker = "MORE" + std::to_string(index);
        liveGrouped.QueuePriceMove(Move(ticker.c_str(), 30.0 + index, 30.5 + index), true);
    }
    auto summarizedLive = liveGrouped.TakeReadyPriceMoveNotifications(true);
    Require(summarizedLive.size() == 1 && summarizedLive.front().groupId == liveGroupId &&
                summarizedLive.front().title == "[Market moves] 6 stocks" &&
                summarizedLive.front().message.find("+1 more") != std::string::npos,
            "a six-stock short burst should stay one block and summarize past five rows");

    NativeNotificationChannel grouped;
    grouped.QueuePriceMove(Move("AAA", 10.0, 10.5), true);
    grouped.QueuePriceMove(Move("BBB", 20.0, 20.5), true);
    grouped.QueuePriceMove(Move("CCC", 30.0, 30.5), true);
    grouped.QueuePriceMove(Move("DDD", 40.0, 40.5), true);
    grouped.QueuePriceMove(Move("EEE", 50.0, 50.5), true);
    auto groupedReady = grouped.TakeReadyPriceMoveNotifications(true);
    Require(groupedReady.size() == 1,
            "five nearby stock moves should flush as one grouped block");
    Require(groupedReady.front().title == "[Market moves] 5 stocks" &&
                groupedReady.front().message.find("AAA:") != std::string::npos &&
                groupedReady.front().message.find("EEE:") != std::string::npos &&
                groupedReady.front().accentText.empty() &&
                groupedReady.front().accentDirection == 0,
            "grouped background text should use the same market-move card format");
    Require(groupedReady.front().actionTicker.empty(),
            "grouped native cards should remain non-navigating as a whole");

    NativeNotificationChannel separate;
    separate.QueuePriceMove(Move("ONE", 100.0, 101.0), false);
    separate.QueuePriceMove(Move("TWO", 200.0, 202.0), false);
    auto separateReady = separate.TakeReadyPriceMoveNotifications(true);
    Require(separateReady.size() == 2 &&
                separateReady[0].title == "[Market move] ONE" &&
                separateReady[1].title == "[Market move] TWO" &&
                separateReady[0].message == "100.00 --> 101.00 USD" &&
                separateReady[0].accentText == "+1.00 (+1.00%)" &&
                separateReady[0].accentDirection == 1,
            "non-batched moves should remain separate blocks with foreground-style accents");
    Require(separateReady[0].actionTicker == "ONE" &&
                separateReady[1].actionTicker == "TWO",
            "separate native move cards should preserve their ticker quick-open actions");

    ApplicationRuntime().RequestNotificationStockOpen("AAPL");
    Require(ApplicationRuntime().ConsumeNotificationStockOpen() == "AAPL" &&
                ApplicationRuntime().ConsumeNotificationStockOpen().empty(),
            "native notification navigation should hand off once to the main thread");

    NativeNotificationChannel coalesced;
    coalesced.Enqueue(NativeNotificationRequest{"first", "old", {}, 0, 77, "C:/old.png", {}});
    coalesced.Enqueue(NativeNotificationRequest{"second", "new", {}, 0, 77, "C:/new.png", {}});
    const auto coalescedReady = coalesced.Take();
    Require(coalescedReady && coalescedReady->title == "second" &&
                coalescedReady->message == "new" &&
                coalescedReady->actionPath == "C:/new.png" && !coalesced.Take(),
            "queued updates for one visible group should coalesce the newest snapshot and action");

    NativeNotificationChannel bounded;
    for (int index = 0; index < 6; ++index) {
        const std::string ticker = "Q" + std::to_string(index);
        bounded.QueuePriceMove(Move(ticker.c_str(), 10.0 + index, 11.0 + index), false);
    }
    const auto boundedReady = bounded.TakeReadyPriceMoveNotifications(true);
    Require(boundedReady.size() == kNotificationMaxVisibleBlocks &&
                boundedReady.front().title == "[Market move] Q1" &&
                boundedReady.back().title == "[Market move] Q5",
            "background notification queue should evict the oldest block above five");

    NativeNotificationChannel disabled;
    disabled.QueuePriceMove(Move("DROP", 1.0, 2.0), false);
    Require(disabled.TakeReadyPriceMoveNotifications(false).empty() &&
                disabled.TakeReadyPriceMoveNotifications(true).empty(),
            "disabled background notifications should discard pending move blocks");

    std::cout << "All notification channel tests passed\n";
    return EXIT_SUCCESS;
}
