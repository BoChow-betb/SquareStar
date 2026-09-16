#include "application/lite_context_lifecycle.hpp"

#include "application/app_state.hpp"
#include "application/stock_context.hpp"

#include <chrono>

namespace squarestar::application {

bool LiteContextWorkFinished(StockContext& context) {
    const auto ready = [](auto& future) {
        return !future.valid() ||
               future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };
    return ready(context.requests.pendingRequest) && ready(context.requests.pendingDetailsRequest) &&
           ready(context.navigation.headerSearch.searchFuture);
}

void ReapRetiredLiteContexts(AppState& state) {
    std::erase_if(state.marketData.retiredLiteContexts, [](const auto& context) {
        return !context || LiteContextWorkFinished(*context);
    });
}

}
