#pragma once

namespace squarestar::platform {

bool EnsureGlfwPlatformRuntimeInitialized();
void ShutdownGlfwPlatformRuntime();
bool GlfwPlatformRuntimeInitialized() noexcept;

}
