#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ern {
inline constexpr std::size_t effects_per_relic = 3;
inline constexpr std::size_t relic_slot_count = 6;
struct EffectRow {
    std::uint32_t effect = UINT32_MAX;
    std::uint32_t curse = UINT32_MAX;
    bool operator==(const EffectRow &) const = default;
};
struct Effects {
    std::array<EffectRow, effects_per_relic> rows{};
    constexpr Effects() = default;
    constexpr Effects(EffectRow first, EffectRow second, EffectRow third)
        : rows{first, second, third} {}
    bool operator==(const Effects &) const = default;
};
inline constexpr Effects empty_effects{};

enum class EffectField { Effect, Curse };
struct EffectPosition {
    std::size_t row = 0;
    EffectField field = EffectField::Effect;
    bool operator==(const EffectPosition &) const = default;
};
inline constexpr std::uint32_t &effect_at(Effects &effects, EffectPosition position) {
    auto &row = effects.rows[position.row];
    return position.field == EffectField::Effect ? row.effect : row.curse;
}
inline constexpr const std::uint32_t &effect_at(const Effects &effects, EffectPosition position) {
    const auto &row = effects.rows[position.row];
    return position.field == EffectField::Effect ? row.effect : row.curse;
}

struct Relic {
    // Keep the full equipment handle so a reused inventory index changes identity.
    std::uint32_t handle;
    std::uint32_t item_id;
    Effects effects;
    std::uint16_t inventory_index() const { return static_cast<std::uint16_t>(handle); }
    bool operator==(const Relic &) const = default;
};

struct EquippedRelics {
    // Normal slots 0..2, Deep of Night slots 3..5, in game equipment order.
    std::array<std::optional<Relic>, relic_slot_count> slots;
    bool operator==(const EquippedRelics &) const = default;
};
} // namespace ern
