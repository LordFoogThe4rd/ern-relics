#pragma once

#include "relics/types.hpp"

namespace ern {
// File and memory formats store positive IDs first, followed by paired curse IDs.
// Domain operations use EffectRow values and never depend on this field order.
using EncodedEffects = std::array<std::uint32_t, effects_per_relic * 2>;
inline constexpr EncodedEffects encode_effects(const Effects &effects) {
    EncodedEffects fields{};
    for (std::size_t row = 0; row < effects_per_relic; ++row) {
        fields[row] = effects.rows[row].effect;
        fields[row + effects_per_relic] = effects.rows[row].curse;
    }
    return fields;
}
inline constexpr Effects decode_effects(const EncodedEffects &fields) {
    Effects effects;
    for (std::size_t row = 0; row < effects_per_relic; ++row)
        effects.rows[row] = {fields[row], fields[row + effects_per_relic]};
    return effects;
}
} // namespace ern
