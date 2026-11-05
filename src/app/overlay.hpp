#pragma once

#include <cstdint>

namespace ern {
struct OverlayStats {
    std::uint64_t frames;
    std::uint64_t toggles;
    std::uint64_t resizes;
    bool visible;
    bool relics_readable;
};
bool install_overlay();
bool overlay_ready();
OverlayStats overlay_stats();
} // namespace ern
