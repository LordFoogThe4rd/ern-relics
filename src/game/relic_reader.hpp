#pragma once

#include "game/memory.hpp"
#include "relics/types.hpp"
#include <cstdint>
#include <variant>

namespace ern {
struct RelicRoots {
    Address item_manager_global;
    Address game_data_global;
    Address world_chr_global;
};

enum class ReadFailure { NotLoaded, Unreadable, Changed };
struct RelicOwner {
    Address items;
    Address game;
    Address player;
    bool operator==(const RelicOwner &) const = default;
};
struct RelicSnapshot {
    RelicOwner owner;
    EquippedRelics relics;
};
using RelicReadResult = std::variant<RelicSnapshot, ReadFailure>;
using RelicOwnerResult = std::variant<RelicOwner, ReadFailure>;

// A readable null pointer confirms absence. A failed read cannot establish an unload.
RelicOwnerResult read_relic_owner(const MemoryRead &read, RelicRoots roots);
bool relic_owner_matches(const MemoryRead &read, RelicRoots roots, const RelicOwner &expected);
RelicReadResult read_equipped(const MemoryRead &read, RelicRoots roots,
                              std::uint32_t equipped_offset);
} // namespace ern
