#pragma once

#include <cstddef>
#include <string_view>

namespace squarestar::platform {

struct EmbeddedResourceView {
    const unsigned char* data = nullptr;
    std::size_t size = 0;

    explicit operator bool() const noexcept { return data != nullptr && size != 0; }
};

// Returns a process-lifetime view of an RCDATA item embedded in SquareStar.exe.
// Resource names match asset filenames or document names.
EmbeddedResourceView FindEmbeddedResource(std::string_view name) noexcept;

} // namespace squarestar::platform
