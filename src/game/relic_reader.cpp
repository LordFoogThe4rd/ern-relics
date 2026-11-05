#include "game/relic_reader.hpp"
#include "game/game_layout.hpp"
#include "relics/effect_encoding.hpp"

namespace ern {
RelicOwnerResult read_relic_owner(const MemoryRead &read, RelicRoots roots) {
    const auto items = read_value<Address>(read, roots.item_manager_global);
    const auto game = read_value<Address>(read, roots.game_data_global);
    if (!items || !game)
        return ReadFailure::Unreadable;
    if (!*items || !*game)
        return ReadFailure::NotLoaded;
    const auto player = read_value<Address>(read, *game + game_layout::player_data);
    if (!player)
        return ReadFailure::Unreadable;
    if (!*player)
        return ReadFailure::NotLoaded;
    return RelicOwner{*items, *game, *player};
}

bool relic_owner_matches(const MemoryRead &read, RelicRoots roots, const RelicOwner &expected) {
    return read_relic_owner(read, roots) == RelicOwnerResult{expected};
}

RelicReadResult read_equipped(const MemoryRead &read, RelicRoots roots,
                              std::uint32_t equipped_offset) {
    const auto result = read_relic_owner(read, roots);
    const auto *owner = std::get_if<RelicOwner>(&result);
    if (!owner)
        return std::get<ReadFailure>(result);
    RelicSnapshot snapshot{*owner, {}};
    auto &relics = snapshot.relics;
    std::array<std::uint32_t, relic_slot_count> handles{};
    for (std::size_t i = 0; i < relics.slots.size(); ++i) {
        const auto handle =
            read_value<std::uint32_t>(read, owner->player + equipped_offset + i * 4);
        if (!handle)
            return ReadFailure::Unreadable;
        handles[i] = *handle;
        const auto index = static_cast<std::uint16_t>(*handle);
        // The game uses a zero handle for unequipped slots, including empty Deep slots.
        if (*handle == 0 || index == UINT16_MAX)
            continue;
        const auto entry =
            read_pointer(read, owner->items + game_layout::inventory_entries +
                                   static_cast<Address>(index) * game_layout::inventory_stride);
        if (!entry)
            return ReadFailure::Unreadable;
        const auto item_id = read_value<std::uint32_t>(read, *entry + game_layout::item_id);
        if (!item_id)
            return ReadFailure::Unreadable;
        Relic relic{*handle, *item_id, {}};
        EncodedEffects fields;
        if (!read(*entry + game_layout::item_effects,
                  std::as_writable_bytes(std::span(fields).first(effects_per_relic))) ||
            !read(*entry + game_layout::item_curses,
                  std::as_writable_bytes(std::span(fields).subspan(effects_per_relic))))
            return ReadFailure::Unreadable;
        relic.effects = decode_effects(fields);
        relics.slots[i] = relic;
    }
    // Loading screens and equipment changes can invalidate a snapshot during a read.
    if (!relic_owner_matches(read, roots, *owner))
        return ReadFailure::Changed;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (read_value<std::uint32_t>(read, owner->player + equipped_offset + i * 4) != handles[i])
            return ReadFailure::Changed;
    }
    return snapshot;
}
} // namespace ern
