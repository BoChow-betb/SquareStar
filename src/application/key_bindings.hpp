#pragma once

#include <string>

#include "application/app_config.hpp"

namespace squarestar::application {

void InitializeDefaultKeybinds(AppConfig& config);
const char* GetActionName(TerminalAction action) noexcept;
std::string FormatKeyBind(const KeyBind& binding);

} // namespace squarestar::application
