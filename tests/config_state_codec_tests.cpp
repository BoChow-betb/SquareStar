#include "application/alert_service.hpp"
#include "application/app_config.hpp"
#include "application/app_navigation.hpp"
#include "application/persisted_state.hpp"
#include "application/theme_profiles.hpp"
#include "services/config_state_codec.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using namespace squarestar::application;
    constexpr std::int64_t nowEpoch = 1'900'000'000;
    AppConfig sourceConfig;
    AppNavigation sourceNavigation;
    squarestar::alerts::AlertService sourceAlerts;
    InitializeThemeProfiles(sourceConfig);
    sourceConfig.soundEnabled = false;
    sourceConfig.themeModeIndex = 1;
    sourceConfig.watchlist = {"AAPL", "BRK.B"};
    sourceConfig.searchHistory = {"MSFT", "AAPL"};
    sourceConfig.activeWorldClocks = {0, 2, 6};
    sourceConfig.saveSearchHistory = true;
    sourceConfig.displayCurrency = "HKD";
    sourceNavigation.lastActiveTab = "AAPL";
    sourceNavigation.activeSidebarTab = SidebarTab::Overview;
    sourceNavigation.guiWorkspace.stockTabs.push_back({"AAPL", 2, 1, false});
    Require(sourceAlerts.SetThreshold("AAPL", 250.0),
            "fixture alert threshold must be accepted");
    sourceAlerts.SilenceUntil("AAPL", nowEpoch + 600);

    const std::string privateState = squarestar::config::EncodePrivateConfigState(
        {sourceConfig, sourceNavigation, sourceAlerts}, nowEpoch);
    Require(privateState.find("AAPL") != std::string::npos &&
                privateState.find("MSFT") != std::string::npos,
            "private codec contains privacy-sensitive symbols before DPAPI wrapping");

    const std::string encoded = squarestar::config::EncodeConfigState(
        {sourceConfig, sourceNavigation, sourceAlerts},
        "protected-fixture",
        "protected-private-fixture");
    Require(encoded.find("\"version\": 1") != std::string::npos,
            "encoder writes the current config schema version");
    Require(encoded.find("\"apiKeyProtected\": \"protected-fixture\"") !=
                std::string::npos,
            "encoder persists protected credentials, not plaintext");
    Require(encoded.find("\"privateStateProtected\": \"protected-private-fixture\"") !=
                std::string::npos,
            "encoder persists DPAPI-wrapped private state");
    Require(encoded.find("\"watchlist\"") == std::string::npos &&
                encoded.find("\"searchHistory\"") == std::string::npos &&
                encoded.find("AAPL") == std::string::npos &&
                encoded.find("MSFT") == std::string::npos,
            "main state document does not expose private stock activity in plaintext");

    AppConfig decodedConfig;
    AppNavigation decodedNavigation;
    squarestar::alerts::AlertService decodedAlerts;
    InitializeThemeProfiles(decodedConfig);
    const auto decoded = squarestar::config::DecodeConfigState(
        encoded,
        {decodedConfig, decodedNavigation, decodedAlerts});
    Require(decoded.parsed && decoded.hasProtectedApiKey &&
                decoded.protectedApiKey == "protected-fixture",
            "decoder accepts the protected credential format");
    Require(decoded.hasProtectedPrivateState &&
                decoded.protectedPrivateState == "protected-private-fixture",
            "decoder exposes the protected private-state blob to persistence");
    Require(decodedConfig.watchlist.empty() && decodedConfig.searchHistory.empty(),
            "private collections are not populated before the DPAPI payload is decoded");
    Require(squarestar::config::DecodePrivateConfigState(
                privateState,
                {decodedConfig, decodedNavigation, decodedAlerts},
                nowEpoch),
            "private state payload decodes after DPAPI unwrapping");
    Require(decodedConfig.watchlist == sourceConfig.watchlist &&
                decodedConfig.searchHistory == sourceConfig.searchHistory &&
                decodedConfig.activeWorldClocks == sourceConfig.activeWorldClocks &&
                decodedConfig.displayCurrency == "HKD",
            "private symbols and public display settings round-trip");
    Require(decodedNavigation.lastActiveTab == "AAPL" &&
                decodedNavigation.activeSidebarTab == SidebarTab::Overview &&
                decodedNavigation.guiWorkspace.stockTabs.size() == 1,
            "workspace descriptors round-trip across public/private state");
    Require(decodedAlerts.HasThreshold("AAPL") &&
                decodedAlerts.IsMutedUntil("AAPL", nowEpoch + 1),
            "active alert thresholds and mutes round-trip in private state");

    sourceConfig.saveSearchHistory = false;
    const std::string historyDisabledPrivate =
        squarestar::config::EncodePrivateConfigState(
            {sourceConfig, sourceNavigation, sourceAlerts}, nowEpoch);
    Require(historyDisabledPrivate.find("\"searchHistory\": []") != std::string::npos &&
                historyDisabledPrivate.find("MSFT") == std::string::npos,
            "disabled search-history persistence does not serialize prior recent searches");

    AppConfig currentOnlyConfig;
    AppNavigation currentOnlyNavigation;
    squarestar::alerts::AlertService currentOnlyAlerts;
    InitializeThemeProfiles(currentOnlyConfig);

    const auto unversioned = squarestar::config::DecodeConfigState(
        R"json({"apiKeyProtected":"","privateStateProtected":"protected"})json",
        {currentOnlyConfig, currentOnlyNavigation, currentOnlyAlerts});
    Require(!unversioned.parsed,
            "unversioned configuration is rejected");

    const auto missingProtectedState = squarestar::config::DecodeConfigState(
        R"json({"version":1,"apiKeyProtected":""})json",
        {currentOnlyConfig, currentOnlyNavigation, currentOnlyAlerts});
    Require(!missingProtectedState.parsed,
            "the current configuration requires a protected private-state field");

    AppConfig plaintextConfig;
    AppNavigation plaintextNavigation;
    squarestar::alerts::AlertService plaintextAlerts;
    InitializeThemeProfiles(plaintextConfig);
    const auto plaintextFields = squarestar::config::DecodeConfigState(
        R"json({"version":1,"apiKeyProtected":"","privateStateProtected":"protected","watchlist":["AAPL"],"searchHistory":["MSFT"]})json",
        {plaintextConfig, plaintextNavigation, plaintextAlerts});
    Require(plaintextFields.parsed && plaintextConfig.watchlist.empty() &&
                plaintextConfig.searchHistory.empty(),
            "plaintext private fields are ignored by the current public-state codec");
    AppConfig invalidCurrencyConfig;
    AppNavigation invalidCurrencyNavigation;
    squarestar::alerts::AlertService invalidCurrencyAlerts;
    InitializeThemeProfiles(invalidCurrencyConfig);
    const auto invalidCurrency = squarestar::config::DecodeConfigState(
        R"json({"version":1,"apiKeyProtected":"","privateStateProtected":"protected","displayCurrency":"XYZ"})json",
        {invalidCurrencyConfig, invalidCurrencyNavigation, invalidCurrencyAlerts});
    Require(invalidCurrency.parsed && invalidCurrencyConfig.displayCurrency == "USD",
            "unsupported display currencies fall back to USD");

    const auto unsupportedVersion = squarestar::config::DecodeConfigState(
        R"json({"version":2,"apiKeyProtected":"","privateStateProtected":"protected"})json",
        {currentOnlyConfig, currentOnlyNavigation, currentOnlyAlerts});
    Require(!unsupportedVersion.parsed,
            "unsupported configuration versions are rejected");

    Require(!squarestar::config::DecodePrivateConfigState(
                R"json({"watchlist":["AAPL"]})json",
                {currentOnlyConfig, currentOnlyNavigation, currentOnlyAlerts},
                nowEpoch),
            "private state without the current schema version is rejected");

    Require(squarestar::config::DecodePrivateConfigState(
                R"json({"version":1,"watchlist":["AAPL"]})json",
                {currentOnlyConfig, currentOnlyNavigation, currentOnlyAlerts},
                nowEpoch),
            "private state does not depend on a GUI layout snapshot");

    const auto malformed = squarestar::config::DecodeConfigState(
        "not json",
        {decodedConfig, decodedNavigation, decodedAlerts});
    Require(!malformed.parsed, "malformed config input is rejected");
    return EXIT_SUCCESS;
}
