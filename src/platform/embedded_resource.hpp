#pragma once

#include <cstddef>
#include <string_view>

namespace squarestar::platform {

struct EmbeddedResourceView {
    const unsigned char* data = nullptr;
    std::size_t size = 0;

    explicit operator bool() const noexcept { return data != nullptr && size != 0; }
};


EmbeddedResourceView FindEmbeddedResource(std::string_view name) noexcept;

}
