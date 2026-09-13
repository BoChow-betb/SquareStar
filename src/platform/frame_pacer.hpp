#pragma once

#include <chrono>

namespace squarestar::platform {

class GuiFramePacer {
  public:
    GuiFramePacer();
    GuiFramePacer(const GuiFramePacer&) = delete;
    GuiFramePacer& operator=(const GuiFramePacer&) = delete;
    ~GuiFramePacer();

    void WaitFor(std::chrono::steady_clock::duration duration) const;

  private:
    void* timer_ = nullptr;
};

} // namespace squarestar::platform
