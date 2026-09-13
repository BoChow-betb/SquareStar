#pragma once

namespace squarestar::platform {

// Best-effort release of unused Windows low-fragmentation-heap backing pages.
// Returns true when the platform accepted the optimization request.
bool TrimProcessPrivateMemory() noexcept;

} // namespace squarestar::platform
