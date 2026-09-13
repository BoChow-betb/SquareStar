#include "application/gui_layout_persistence.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    using squarestar::application::CommitGuiLayoutSnapshot;
    using squarestar::application::CompleteGuiLayoutPersistAttempt;
    using squarestar::application::ConfigureGuiLayoutPersistence;
    using squarestar::application::GuiLayoutPersistenceSaveRequested;
    using squarestar::application::GuiLayoutInitialBaselinePending;
    using squarestar::application::GuiLayoutSnapshotForConfig;
    using squarestar::application::GuiLayoutSnapshotNeedsPersist;
    using squarestar::application::PrimeGuiLayoutInitialBaseline;
    using squarestar::application::ResetGuiLayoutPersistence;
    using squarestar::application::StageGuiLayoutSnapshot;

    CommitGuiLayoutSnapshot("saved-layout");
    if (GuiLayoutSnapshotForConfig() != "saved-layout") {
        std::cerr << "loaded layout was not staged for unified config persistence\n";
        return EXIT_FAILURE;
    }

    ConfigureGuiLayoutPersistence(true, "initial-layout");
    if (GuiLayoutSnapshotNeedsPersist("initial-layout")) {
        std::cerr << "initial layout was incorrectly marked dirty\n";
        return EXIT_FAILURE;
    }

    StageGuiLayoutSnapshot("changed-layout");
    if (GuiLayoutSnapshotForConfig() != "changed-layout" ||
        !GuiLayoutSnapshotNeedsPersist("changed-layout")) {
        std::cerr << "changed layout was not staged independently of disk commit\n";
        return EXIT_FAILURE;
    }
    CompleteGuiLayoutPersistAttempt("changed-layout", true);
    if (GuiLayoutSnapshotNeedsPersist("changed-layout")) {
        std::cerr << "successful layout commit remained dirty\n";
        return EXIT_FAILURE;
    }

    ConfigureGuiLayoutPersistence(true, {});
    if (!GuiLayoutInitialBaselinePending() ||
        !PrimeGuiLayoutInitialBaseline("first-render-layout") ||
        GuiLayoutInitialBaselinePending() ||
        GuiLayoutSnapshotForConfig() != "first-render-layout" ||
        GuiLayoutSnapshotNeedsPersist("first-render-layout")) {
        std::cerr << "first rendered layout was not absorbed as a clean baseline\n";
        return EXIT_FAILURE;
    }
    if (PrimeGuiLayoutInitialBaseline("unexpected-second-baseline")) {
        std::cerr << "initial layout baseline could be primed more than once\n";
        return EXIT_FAILURE;
    }

    StageGuiLayoutSnapshot("retry-layout");
    CompleteGuiLayoutPersistAttempt("retry-layout", false);
    if (!GuiLayoutPersistenceSaveRequested(false)) {
        std::cerr << "failed layout write did not request a retry\n";
        return EXIT_FAILURE;
    }

    ResetGuiLayoutPersistence();
    if (!GuiLayoutSnapshotForConfig().empty()) {
        std::cerr << "layout persistence reset retained runtime state\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
