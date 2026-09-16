#pragma once

namespace squarestar::application {

struct AppState;
struct StockContext;

bool LiteContextWorkFinished(StockContext& context);
void ReapRetiredLiteContexts(AppState& state);

}
