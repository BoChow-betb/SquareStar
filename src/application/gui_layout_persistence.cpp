#include "application/gui_layout_persistence.hpp"

#include <mutex>
#include <utility>

namespace squarestar::application {
namespace {

std::mutex g_GuiLayoutPersistenceMutex;
bool g_GuiLayoutPersistenceEnabled = false;
bool g_GuiLayoutPersistRetryRequested = false;
bool g_GuiLayoutInitialBaselinePending = false;
std::string g_CurrentImGuiIni;
std::string g_LastPersistedImGuiIni;

} // namespace

void ConfigureGuiLayoutPersistence(bool enabled, std::string initialSnapshot) {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    g_GuiLayoutPersistenceEnabled = enabled;
    g_GuiLayoutPersistRetryRequested = false;
    // With no saved layout, ImGui will mark the first rendered window geometry
    // as dirty even when the user has not interacted with the application. Use
    // that first rendered layout as the in-memory baseline instead of treating
    // application-created defaults as a reason to create portable storage.
    g_GuiLayoutInitialBaselinePending = enabled && initialSnapshot.empty();
    g_CurrentImGuiIni = initialSnapshot;
    g_LastPersistedImGuiIni = std::move(initialSnapshot);
}

void SetGuiLayoutPersistenceEnabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    g_GuiLayoutPersistenceEnabled = enabled;
}

bool GuiLayoutPersistenceEnabled() noexcept {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    return g_GuiLayoutPersistenceEnabled;
}

bool GuiLayoutInitialBaselinePending() noexcept {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    return g_GuiLayoutPersistenceEnabled && g_GuiLayoutInitialBaselinePending;
}

bool PrimeGuiLayoutInitialBaseline(std::string snapshot) {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    if (!g_GuiLayoutPersistenceEnabled || !g_GuiLayoutInitialBaselinePending)
        return false;
    g_GuiLayoutInitialBaselinePending = false;
    g_GuiLayoutPersistRetryRequested = false;
    g_CurrentImGuiIni = snapshot;
    g_LastPersistedImGuiIni = std::move(snapshot);
    return true;
}

std::string GuiLayoutSnapshotForConfig() {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    return g_CurrentImGuiIni;
}

void StageGuiLayoutSnapshot(std::string snapshot) {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    g_CurrentImGuiIni = std::move(snapshot);
}

bool GuiLayoutSnapshotNeedsPersist(std::string_view snapshot) {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    return g_GuiLayoutPersistenceEnabled && snapshot != g_LastPersistedImGuiIni;
}

bool GuiLayoutPersistenceSaveRequested(bool imguiRequested) {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    const bool requested = imguiRequested || g_GuiLayoutPersistRetryRequested;
    g_GuiLayoutPersistRetryRequested = false;
    return g_GuiLayoutPersistenceEnabled && requested;
}

void CompleteGuiLayoutPersistAttempt(std::string snapshot, bool succeeded) {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    if (succeeded)
        g_LastPersistedImGuiIni = std::move(snapshot);
    else
        g_GuiLayoutPersistRetryRequested = true;
}

void CommitGuiLayoutSnapshot(std::string snapshot) {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    g_GuiLayoutInitialBaselinePending = false;
    g_CurrentImGuiIni = snapshot;
    g_LastPersistedImGuiIni = std::move(snapshot);
}

void ResetGuiLayoutPersistence() {
    std::lock_guard<std::mutex> lock(g_GuiLayoutPersistenceMutex);
    g_GuiLayoutPersistenceEnabled = false;
    g_GuiLayoutPersistRetryRequested = false;
    g_GuiLayoutInitialBaselinePending = false;
    std::string().swap(g_CurrentImGuiIni);
    std::string().swap(g_LastPersistedImGuiIni);
}

} // namespace squarestar::application
