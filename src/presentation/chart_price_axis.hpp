#pragma once

namespace squarestar::application {
struct StockContext;
}

namespace squarestar::presentation {

bool ShouldIncludeReferencePriceInVisibleAxis(double visibleMinPrice,
                                              double visibleMaxPrice,
                                              double referencePrice);

// Keep an off-screen reference line visible beside its edge-pinned label.
// In-range lines retain their exact projected position.
double ResolveVisibleReferenceLineY(double projectedY,
                                    double plotTop,
                                    double plotBottom,
                                    double labelHeight) noexcept;

void EnsureVisiblePriceTicks(application::StockContext& context,
                             bool candlestick,
                             double visibleMinX,
                             double visibleMaxX,
                             double requiredPrice,
                             double axisPaddingFraction = 0.06);

} // namespace squarestar::presentation
