#pragma once

#include "game/relic_reader.hpp"
#include "game/relic_writer.hpp"
#include <string_view>

namespace ern {
struct RelicEquipContext {
    RelicOwner owner;
    Address character;
    Address controller;
    Address equipment;
    std::array<std::uint32_t, relic_slot_count> equipped;
    std::array<std::uint32_t, relic_slot_count> cached;
    std::uint8_t mode;
};

struct RelicEquipFailure {
    std::string_view check;
    Address address{};
    std::optional<Address> actual;
    std::optional<Address> expected;
    std::optional<std::size_t> slot = {};
    bool capacity_check = false;
};
using RelicEquipReadResult = std::variant<RelicEquipContext, RelicEquipFailure>;

// A capacity mismatch blocks an edit even if the equipment callback cannot claim it.
std::optional<std::string> relic_capacity_error(const RelicEquipFailure &failure);

// Reads the local player's native controller; never invokes a game function.
RelicEquipReadResult read_relic_equip_context(const MemoryRead &read, RelicRoots roots,
                                              Address module);

// Only the native adapter knows the game's function addresses and ABI flags.
struct RelicEquipCalls {
    std::function<std::int32_t(Address inventory, std::uint32_t handle)> find_item;
    std::function<void(Address equipment, std::uint32_t slot, std::int32_t index)> set_slot;
    std::function<void(Address character)> refresh;
};
using RelicEquipReport = std::function<void(RelicEquipStep, const RelicEquipFailure &)>;

// Runs on the game's equipment-update thread. One call owns the full cycle,
// including the context snapshot used to verify detachment and restoration.
EditResult replace_equipped_native(const MemoryRead &read, const MemoryWrite &write,
                                   const RelicEditRequest &request, Address module,
                                   Address character, const RelicEquipCalls &calls,
                                   const RelicEquipReport &report = {});
} // namespace ern
