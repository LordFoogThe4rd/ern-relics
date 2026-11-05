#pragma once

#include "game/relic_reader.hpp"

namespace ern {
enum class EditLocation { Unavailable, NotLoaded, RoundtableHold, OutsideRoundtable };

// Live player presence is required; saved character data also exists at the title screen.
// NotLoaded confirms absence; Unavailable means a read failed or changed during inspection.
EditLocation read_edit_location(const MemoryRead &read, RelicRoots roots);
const char *location_lock_reason(EditLocation location);
} // namespace ern
