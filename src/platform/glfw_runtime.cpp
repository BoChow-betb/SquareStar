#include "platform/glfw_runtime.hpp"

#include <GLFW/glfw3.h>

namespace squarestar::platform {
namespace {

bool g_GlfwRuntimeInitialized = false;
double g_GlfwClockSeconds = 0.0;

} // namespace

bool EnsureGlfwPlatformRuntimeInitialized() {
    if (g_GlfwRuntimeInitialized)
        return true;
    if (!glfwInit())
        return false;
    g_GlfwRuntimeInitialized = true;
    glfwSetTime(g_GlfwClockSeconds);
    return true;
}

void ShutdownGlfwPlatformRuntime() {
    if (!g_GlfwRuntimeInitialized)
        return;
    g_GlfwClockSeconds = glfwGetTime();
    glfwTerminate();
    g_GlfwRuntimeInitialized = false;
}

bool GlfwPlatformRuntimeInitialized() noexcept {
    return g_GlfwRuntimeInitialized;
}

} // namespace squarestar::platform
