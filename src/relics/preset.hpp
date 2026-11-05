#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "relics/types.hpp"
#include "relics/relic_validator.hpp"

namespace ern {
// Effect recipes only. No owned-instance references or item-creation instructions.
struct Preset {
    std::array<std::optional<Effects>, relic_slot_count> slots;
    bool operator==(const Preset &) const = default;
};
struct PresetStage {
    Preset drafts;
    std::array<std::string, relic_slot_count> skipped;
};
std::string serialize_preset(const Preset &preset);
std::optional<Preset> parse_preset(std::string_view text);
bool valid_preset_name(std::string_view text);
Preset capture_preset(const EquippedRelics &equipped, const RelicValidator &validator,
                      const RuleContext &context);
PresetStage stage_preset(const Preset &preset, const EquippedRelics &equipped,
                         const RelicValidator &validator, const RuleContext &context);
} // namespace ern
