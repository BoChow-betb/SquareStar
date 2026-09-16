#pragma once

#include <string>
#include <string_view>

namespace squarestar::application {

void ConfigureGuiLayoutPersistence(bool enabled, std::string initialSnapshot = {});
void SetGuiLayoutPersistenceEnabled(bool enabled) noexcept;
bool GuiLayoutPersistenceEnabled() noexcept;
bool GuiLayoutInitialBaselinePending() noexcept;
bool PrimeGuiLayoutInitialBaseline(std::string snapshot);
std::string GuiLayoutSnapshotForConfig();
void StageGuiLayoutSnapshot(std::string snapshot);
bool GuiLayoutSnapshotNeedsPersist(std::string_view snapshot);
bool GuiLayoutPersistenceSaveRequested(bool imguiRequested);
void CompleteGuiLayoutPersistAttempt(std::string snapshot, bool succeeded);
void CommitGuiLayoutSnapshot(std::string snapshot);
void ResetGuiLayoutPersistence();

}
