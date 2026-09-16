#pragma once

namespace squarestar::application {
struct StockContext;
}

namespace squarestar::presentation {

bool ShouldIncludeReferencePriceInVisibleAxis(double visibleMinPrice,
                                              double visibleMaxPrice,
                                              double referencePrice);


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

}
