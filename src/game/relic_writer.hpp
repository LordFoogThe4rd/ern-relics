#pragma once
#include "game/memory.hpp"
#include "game/relic_reader.hpp"
#include "relics/relic_validator.hpp"
#include "game/edit_location.hpp"
#include "relics/edit_result.hpp"
#include <memory>

namespace ern {
enum class RelicEquipStep { Unequip, Equip };
enum class RelicEquipResult { Complete, Unavailable, Failed };
// An unavailable initial Unequip leaves the loadout untouched; Failed may change native state.
using RelicEquipment = std::function<RelicEquipResult(RelicEquipStep)>;

struct RelicEditRequest {
    RelicRoots roots;
    std::uint32_t equipped_offset;
    RelicOwner owner;
    std::size_t slot;
    Relic expected;
    Effects replacement;
    std::shared_ptr<const RelicValidator> rules;
    RuleContext context;
};
using RelicCommit = std::function<EditResult(RelicEditRequest)>;

// Re-resolve the selected relic instead of retaining a UI-supplied record address.
EditResult replace_equipped(const MemoryRead &read, const MemoryWrite &write, RelicRoots roots,
                            std::uint32_t equipped_offset, const RelicOwner &owner,
                            std::size_t slot, const Relic &expected, const Effects &replacement,
                            const RelicValidator &validator, const RuleContext &context,
                            const RelicEquipment &equip = {});
} // namespace ern
