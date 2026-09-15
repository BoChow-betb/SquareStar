#include "modules/stock_details.hpp"
#include "modules/core.hpp"
#include "modules/currency_display.hpp"
#include "modules/ui_focus.hpp"
#include "modules/market_data.hpp"
#include "application/stock_data_merge.hpp"
#include "platform/world_clock_runtime.hpp"
#include "domain/stock_data.hpp"
#include "services/http_client.hpp"
#include "services/url_policy.hpp"


namespace squarestar::shell {

using squarestar::format::FormatLargeNumber;
using squarestar::format::FormatDouble;
using squarestar::application::AppState;
using squarestar::application::StockContext;
using squarestar::application::StockFetchMetrics;
using squarestar::application::StockFetchProfile;
using squarestar::application::StockFetchNews;
using squarestar::http::OpenExternalHttpsUrl;
using squarestar::http::IsSafeExternalHttpsUrl;

namespace {

void RenderNewsList(AppState& state, StockContext& ctx) {
    ImGui::BeginChild("NewsSection", ImVec2(0, 0), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ImGui::IsWindowHovered() && !ImGui::GetIO().WantTextInput) {
        constexpr float scrollStep = 50.0f;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            ImGui::SetScrollY(ImGui::GetScrollY() - scrollStep);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            ImGui::SetScrollY(ImGui::GetScrollY() + scrollStep);
    }
    constexpr float hPad = 18.0f;
    const auto& news = ctx.RawData().news;
    for (std::size_t articleIndex = 0; articleIndex < news.size(); ++articleIndex) {
        const auto& article = news[articleIndex];
        ImGui::PushID(static_cast<int>(articleIndex));
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::SetCursorPosX(hPad);
        std::time_t articleTimestamp = article.datetime;
        const std::tm articleTime =
            squarestar::platform::SafeTimeTm(articleTimestamp, false);
        char timeBuf[32]{};
        std::strftime(timeBuf, sizeof(timeBuf), "%b %d, %H:%M", &articleTime);
        ImGui::TextDisabled("%s | %s", article.source.c_str(), timeBuf);
        ImGui::SetCursorPosX(hPad);
        ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
        const float linkWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - hPad);
        if (IsSafeExternalHttpsUrl(article.url)) {
            if (RenderUnifiedLink(
                    state, article.headline.c_str(), "##NewsHeadline", linkWidth, 0.42f)) {
                PlayUISound("key.wav", state);
                OpenExternalHttpsUrl(article.url);
            }
        } else {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + linkWidth);
            ImGui::TextUnformatted(article.headline.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::PopFont();
        if (!article.summary.empty()) {
            ImGui::SetCursorPosX(hPad);
            ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.textDisabled));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + linkWidth);
            ImGui::TextWrapped("%s", article.summary.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

} // namespace

void RenderStockNews(AppState& state, StockContext& ctx) {
    ImGui::BeginChild("NewsRegion", ImVec2(0, 0), false);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (ctx.RawData().news.empty()) {
        constexpr float hPad = 24.0f;
        ImGui::SetCursorPosX(hPad);
        const bool loadingNews =
            ctx.requests.pendingDetailsRequest.valid() &&
            (ctx.requests.requestedDetailMask & StockFetchNews) != 0 &&
            (ctx.RawData().resolvedDetailMask & StockFetchNews) == 0;
        ImGui::TextDisabled(
            loadingNews
                ? "Loading recent business articles..."
                : "No business articles discovered for this symbol over the past 7 days.");
    } else {
        RenderNewsList(state, ctx);
    }
    ImGui::EndChild();
}
void RenderStockMetrics(AppState& state, StockContext& ctx, double pC) {
    const bool liteMetrics = state.navigation.liteGuiActive;
    ImGui::BeginChild("MetricsRegion",
                      ImVec2(0, 0),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // Lite metrics are a fixed, non-scrollable three-column surface. Child
    // windows normally discard WindowPadding when borderless, so explicitly
    // opt into it to keep equal breathing room at both horizontal edges.
    if (liteMetrics) {
        // Four-direction margins: the 4 px lead-in plus 16 px child padding
        // moves the table upward while leaving a visibly larger bottom margin.
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 16.0f));
        ImGui::BeginChild("MetricsSection",
                          ImVec2(0, 0),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
    } else {
        ImGui::SetCursorPos(ImVec2(24.0f, 20.0f));
        ImGui::BeginChild("MetricsBody",
                          ImVec2(-24.0f, -20.0f),
                          false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }
    const bool loadingMetrics =
        ctx.requests.pendingDetailsRequest.valid() &&
        (ctx.requests.requestedDetailMask & (StockFetchMetrics | StockFetchProfile)) != 0 &&
        (ctx.RawData().resolvedDetailMask & (StockFetchMetrics | StockFetchProfile)) == 0;
    if (loadingMetrics)
        ImGui::TextDisabled("Loading optional company metrics...");
    const bool hasIndustry = !ctx.RawData().industry.empty() && ctx.RawData().industry != "N/A";
    const bool hasWebsite = !ctx.RawData().weburl.empty() && ctx.RawData().weburl != "N/A";
    const int profileItems = (hasIndustry ? 1 : 0) + (hasWebsite ? 1 : 0);
    if (profileItems > 0) {
        const float profileWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const int profileColumns =
            profileItems == 2 && profileWidth >= 620.0f ? 2 : 1;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 8.0f));
        if (ImGui::BeginTable("MetricsProfileGrid",
                              profileColumns,
                              ImGuiTableFlags_SizingStretchSame |
                                  ImGuiTableFlags_NoSavedSettings,
                              ImVec2(profileWidth, 0.0f))) {
            int profileColumn = 0;
            ImGui::TableNextRow();
            if (hasIndustry) {
                ImGui::TableSetColumnIndex(profileColumn++);
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.text, 0.68f));
                ImGui::TextUnformatted("INDUSTRY");
                ImGui::PopStyleColor();
                ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("%s", ctx.RawData().industry.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
                ImGui::EndGroup();
                DrawHoveredLastItemFocusOutline(state, 2);
            }
            if (hasWebsite) {
                if (profileColumns == 1 && profileColumn > 0) {
                    ImGui::TableNextRow();
                    profileColumn = 0;
                }
                ImGui::TableSetColumnIndex(profileColumn);
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.text, 0.68f));
                ImGui::TextUnformatted("WEBSITE");
                ImGui::PopStyleColor();
                ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
                const std::string& websiteText = ctx.RawData().weburl;
                const float websiteWrapWidth = ImGui::GetContentRegionAvail().x;
                if (RenderUnifiedLink(
                        state, websiteText.c_str(), "##CompanyWebsite", websiteWrapWidth)) {
                    PlayUISound("key.wav", state);
                    OpenExternalHttpsUrl(ctx.RawData().weburl);
                }
                ImGui::PopFont();
                ImGui::EndGroup();
                DrawHoveredLastItemFocusOutline(state, 2);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }
    char changeBuf[48], changePctBuf[48], wkChangeBuf[48];
    const double metricChange = ctx.RawData().currentPrice - ctx.RawData().previousClose;
    const double metricPct = ctx.RawData().previousClose > 0.0
                                 ? (ctx.RawData().currentPrice - ctx.RawData().previousClose) /
                                       ctx.RawData().previousClose * 100.0
                                 : 0.0;
    double displayMetricChange = 0.0;
    const bool displayMetricChangeReady =
        TryConvertUsdForDisplay(state, metricChange, displayMetricChange);
    if (displayMetricChangeReady)
        snprintf(changeBuf, sizeof(changeBuf), "%+.2f", displayMetricChange);
    else
        snprintf(changeBuf, sizeof(changeBuf), "...");
    snprintf(changePctBuf, sizeof(changePctBuf), "%+.2f%%", metricPct);
    if (ctx.RawData().hasFiftyTwoWkChangePercent)
        snprintf(
            wkChangeBuf, sizeof(wkChangeBuf), "%+.2f%%", ctx.RawData().fiftyTwoWkChangePercent);
    const double metricEpsilon = std::max(1e-8, std::abs(pC) * 1e-8);
    const ImVec4 dayMoveColor = metricChange < -metricEpsilon  ? ThemeVec(state.config.theme.negative)
                                : metricChange > metricEpsilon ? ThemeVec(state.config.theme.positive)
                                                               : ThemeVec(state.config.theme.textDisabled);
    const ImVec4 weekMoveColor =
        ctx.RawData().fiftyTwoWkChangePercent < 0.0   ? ThemeVec(state.config.theme.negative)
        : ctx.RawData().fiftyTwoWkChangePercent > 0.0 ? ThemeVec(state.config.theme.positive)
                                                        : ThemeVec(state.config.theme.textDisabled);
    const float metricsWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    // The Lite window has a fixed 900 px width. All three sections comfortably
    // fit across that surface, and keeping them on one row prevents FUNDAMENTALS
    // from being pushed into a partially visible second row. Normal GUI mode
    // remains responsive at its existing breakpoints.
    const int metricColumns =
        liteMetrics ? 3 : (metricsWidth >= 900.0f ? 3 : (metricsWidth >= 620.0f ? 2 : 1));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(liteMetrics ? 10.0f : 14.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    if (ImGui::BeginTable("MetricsGlanceGrid",
                          metricColumns,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings,
                          ImVec2(metricsWidth, 0.0f))) {
        int metricSection = 0;
        auto DrawMetricSection = [&](const char* title, auto&& drawRows) {
            if (metricSection % metricColumns == 0)
                ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(metricSection % metricColumns);
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.text, 0.68f));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
            drawRows();
            ImGui::PopFont();
            ImGui::EndGroup();
            DrawHoveredLastItemFocusOutline(state, 2);
            ++metricSection;
        };
        auto PriceOrNA = [&](double value) {
            if (value <= 0.0)
                return std::string("N/A");
            double displayValue = 0.0;
            return TryConvertUsdForDisplay(state, value, displayValue)
                       ? FormatDouble(displayValue)
                       : std::string("...");
        };
        auto MarketValueOrNA = [&](double value) {
            if (value <= 0.0)
                return std::string("N/A");
            double displayValue = 0.0;
            return TryConvertUsdForDisplay(state, value, displayValue)
                       ? FormatLargeNumber(displayValue)
                       : std::string("...");
        };
        DrawMetricSection("SESSION", [&]() {
            DrawFinancialRow("Open", PriceOrNA(ctx.RawData().openPrice));
            DrawFinancialRow("High", PriceOrNA(ctx.RawData().dayHigh));
            DrawFinancialRow("Low", PriceOrNA(ctx.RawData().dayLow));
            DrawFinancialRow("Prev Close", PriceOrNA(pC));
            DrawFinancialRow("Change",
                             pC > 0.0 && displayMetricChangeReady
                                 ? std::string(changeBuf)
                                 : (pC > 0.0 ? std::string("...")
                                             : std::string("N/A")),
                             true,
                             pC > 0.0 ? &dayMoveColor : nullptr);
            DrawFinancialRow("Change %",
                             pC > 0.0 ? std::string(changePctBuf) : std::string("N/A"),
                             false,
                             pC > 0.0 ? &dayMoveColor : nullptr);
        });
        DrawMetricSection("TRADING / 52 WEEK", [&]() {
            DrawFinancialRow("Volume",
                             ctx.RawData().hasRegularMarketVolume
                                 ? FormatLargeNumber(ctx.RawData().regularMarketVolume)
                                 : std::string("N/A"));
            DrawFinancialRow("Avg Vol (3M)",
                             ctx.RawData().avgVolume > 0.0
                                 ? FormatLargeNumber(ctx.RawData().avgVolume)
                                 : std::string("N/A"));
            DrawFinancialRow("52wk High", PriceOrNA(ctx.RawData().fiftyTwoWeekHigh));
            DrawFinancialRow("52wk Low", PriceOrNA(ctx.RawData().fiftyTwoWeekLow));
            DrawFinancialRow("52wk Change",
                             ctx.RawData().hasFiftyTwoWkChangePercent ? std::string(wkChangeBuf)
                                                                        : std::string("N/A"),
                             false,
                             ctx.RawData().hasFiftyTwoWkChangePercent ? &weekMoveColor : nullptr);
        });
        DrawMetricSection("FUNDAMENTALS", [&]() {
            DrawFinancialRow("Market Cap",
                             ctx.RawData().marketCap > 0.0
                                 ? MarketValueOrNA(ctx.RawData().marketCap)
                                 : std::string("N/A"));
            DrawFinancialRow("P/E Ratio",
                             ctx.RawData().peRatio > 0.0 ? FormatDouble(ctx.RawData().peRatio)
                                                           : std::string("N/A"));
            DrawFinancialRow("Dividend Yield",
                             ctx.RawData().dividendYield > 0.0
                                 ? FormatDouble(ctx.RawData().dividendYield) + "%"
                                 : std::string("N/A"));
            DrawFinancialRow("Beta",
                             (ctx.RawData().beta < 0.0 || ctx.RawData().beta > 0.0)
                                 ? FormatDouble(ctx.RawData().beta)
                                                         : std::string("N/A"),
                             false);
        });
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    if (liteMetrics) {
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else {
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

} // namespace squarestar::shell
