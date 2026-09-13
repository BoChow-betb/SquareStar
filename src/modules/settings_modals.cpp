#include "modules/settings_modals.hpp"

#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/runtime_state.hpp"
#include "modules/core.hpp"
#include "platform/embedded_resource.hpp"
#include "presentation/ui_metrics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::AppState;
using squarestar::application::WakeMainLoop;
using squarestar::presentation::kDialogButtonHeight;

void RenderCreditsModal(AppState& state, bool openCreditsModal) {
    if (openCreditsModal)
        ImGui::OpenPopup("Credits & Licenses");
    const ImGuiViewport* creditsViewport = ImGui::GetMainViewport();
    const ImVec2 creditsSize(
        std::max(1.0f, std::min(740.0f, creditsViewport->WorkSize.x - 40.0f)),
        std::max(1.0f, std::min(560.0f, creditsViewport->WorkSize.y - 40.0f)));
    ImGui::SetNextWindowPos(
        creditsViewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(creditsSize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg,
                          ThemeVec(state.config.theme.dimOverlay));
    if (ImGui::BeginPopupModal("Credits & Licenses",
                               nullptr,
                               ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse)) {
        const bool dismissCredits = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
        ImGui::TextUnformatted("Credits & licenses");
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::TextColored(
            ThemeVec(state.config.theme.textDisabled, 0.82f),
            "Copyright (c) 2026 Bo Blitz Chow");
        ImGui::TextColored(
            ThemeVec(state.config.theme.textDisabled, 0.82f),
            "Open-source project. Market data: Yahoo Finance; optional Finnhub with a user key.");
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        struct CreditsDocument {
            const char* label;
            const char* resourceName;
        };
        static constexpr std::array<CreditsDocument, 12> documents = {{
            {"Privacy", "PRIVACY.md"},
            {"Market-data providers", "DATA_PROVIDER_NOTICE.md"},
            {"Asset credits", "ASSET_CREDITS.md"},
            {"Third-party notices", "THIRD_PARTY_NOTICES.md"},
            {"SquareStar (MIT)", "LICENSE"},
            {"Dear ImGui (MIT)", "licenses/Dear-ImGui.txt"},
            {"ImPlot (MIT)", "licenses/ImPlot.txt"},
            {"yyjson (MIT)", "licenses/yyjson.txt"},
            {"GLFW", "licenses/GLFW.txt"},
            {"curl", "licenses/curl.txt"},
            {"Outfit (OFL 1.1)", "licenses/Outfit-OFL-1.1.txt"},
            {"CC BY 4.0", "licenses/CC-BY-4.0.txt"},
        }};
        static std::size_t selectedDocument = 0;
        if (openCreditsModal)
            selectedDocument = 0;

        const float creditsFooterReserve =
            kDialogButtonHeight + ImGui::GetStyle().ItemSpacing.y + 18.0f;
        const float creditsBodyHeight =
            std::max(1.0f, ImGui::GetContentRegionAvail().y - creditsFooterReserve);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 0.0f));
        if (ImGui::BeginTable("CreditsDocuments",
                              2,
                              ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_NoSavedSettings,
                              ImVec2(0.0f, creditsBodyHeight))) {
            ImGui::TableSetupColumn("Document", ImGuiTableColumnFlags_WidthFixed, 225.0f);
            ImGui::TableSetupColumn("Embedded text", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
            if (ImGui::BeginChild("CreditsDocumentList",
                                  ImVec2(0.0f, creditsBodyHeight),
                                  ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse)) {
                for (std::size_t index = 0; index < documents.size(); ++index) {
                    if (ImGui::Selectable(documents[index].label,
                                          selectedDocument == index)) {
                        selectedDocument = index;
                        PlayUISound("transition.wav", state);
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);

            ImGui::TableSetColumnIndex(1);
            const CreditsDocument& document = documents[selectedDocument];
            ImGui::TextDisabled("%s", document.resourceName);
            ImGui::Separator();
            const float documentTextHeight =
                std::max(1.0f,
                         creditsBodyHeight - ImGui::GetFrameHeightWithSpacing());
            if (ImGui::BeginChild("CreditsDocumentText",
                                  ImVec2(0.0f, documentTextHeight),
                                  ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
                const squarestar::platform::EmbeddedResourceView resource =
                    squarestar::platform::FindEmbeddedResource(document.resourceName);
                if (resource) {
                    const char* begin = reinterpret_cast<const char*>(resource.data);
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(begin, begin + resource.size);
                    ImGui::PopTextWrapPos();
                } else {
                    ImGui::TextColored(ThemeVec(state.config.theme.negative),
                                       "Embedded document is unavailable.");
                }
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        constexpr float closeCreditsWidth = 104.0f;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            std::max(0.0f, ImGui::GetContentRegionAvail().x - closeCreditsWidth));
        if (ImGui::Button("Close", ImVec2(closeCreditsWidth, kDialogButtonHeight)) ||
            dismissCredits) {
            PlayUISound("click.wav", state);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
}

void RenderClearDataModal(AppState& state, bool openSavedDataModal) {
    if (openSavedDataModal)
        ImGui::OpenPopup("Reset SquareStar");
    ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Reset SquareStar",
                               nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
        ImGui::TextUnformatted("Reset all SquareStar app data?");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 390.0f);
        ImGui::TextWrapped(
            "This removes the API key, settings, watchlist, alerts, keybindings, GUI layout, and cached Trending Now rows, then closes SquareStar. Exports and program assets are kept.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        constexpr float buttonWidth = 140.0f;
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, kDialogButtonHeight)))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ThemeVec(state.config.theme.danger));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ThemeVec(state.config.theme.dangerHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ThemeVec(state.config.theme.danger, 0.90f));
        if (ImGui::Button("Reset and exit",
                          ImVec2(buttonWidth, kDialogButtonHeight))) {
            ApplicationRuntime().RequestFactoryReset();
            ApplicationRuntime().RequestQuit();
            WakeMainLoop();
        }
        ImGui::PopStyleColor(3);
        ImGui::EndPopup();
    }
}

} // namespace squarestar::shell
