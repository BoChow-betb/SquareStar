#pragma once

namespace squarestar::application {
struct AppRenderCache;
struct AppState;
struct StockContext;
} // namespace squarestar::application

namespace squarestar::presentation {

void ClearApplicationFontPointers(squarestar::application::AppRenderCache& state);
void RebuildApplicationFonts(squarestar::application::AppState& state);
void ReleaseContextRenderMemory(squarestar::application::StockContext& context);
void ReleaseGuiStateRenderMemory(squarestar::application::AppState& state);
void CompactGuiTransientMemory();

} // namespace squarestar::presentation
