#pragma once

#include "game/relic_writer.hpp"

namespace ern {
// Bootstrap calls this only for the supported offline game and default relic layout.
std::optional<std::string> install_native_relic_equip(MemoryRead read, MemoryWrite write);
std::optional<std::string> native_relic_equip_error();
EditResult commit_native_relic_edit(RelicEditRequest request);
} // namespace ern
