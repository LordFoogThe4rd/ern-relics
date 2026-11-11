#include "game/game_layout.hpp"
#include "game/relic_writer.hpp"
#include "relics/effect_encoding.hpp"

namespace ern {
EditResult replace_equipped(const MemoryRead &read, const MemoryWrite &write, RelicRoots roots,
                            std::uint32_t equipped_offset, const RelicOwner &owner,
                            std::size_t slot, const Relic &expected, const Effects &replacement,
                            const RelicValidator &validator, const RuleContext &context,
                            const RelicEquipment &equip, bool invalid) {
    if (slot >= relic_slot_count)
        return EditRejected{EditRejectReason::Stale, {}};
    if (read_edit_location(read, roots) != EditLocation::RoundtableHold)
        return EditRejected{EditRejectReason::NotAtRoundtable, {}};
    const auto snapshot = read_equipped(read, roots, equipped_offset);
    const auto *loaded = std::get_if<RelicSnapshot>(&snapshot);
    if (!loaded)
        return EditRejected{EditRejectReason::Unreadable, {}};
    if (loaded->owner != owner)
        return EditRejected{EditRejectReason::Stale, {}};
    const auto *current = &loaded->relics;
    if (!current->slots[slot] || *current->slots[slot] != expected)
        return EditRejected{EditRejectReason::Stale, {}};
    if (relic_lock_reason(validator.items().find(current->slots[slot]->item_id)))
        return EditRejected{EditRejectReason::Locked, {}};
    Effects ordered = replacement;
    if (!invalid) {
        const auto validation = validator.validate(expected.item_id, replacement, context);
        const auto *valid = std::get_if<Obtainable>(&validation);
        if (!valid)
            return EditRejected{EditRejectReason::Invalid, {}};
        ordered = valid->effects;
    }
    const auto record = read_value<Address>(
        read, owner.items + game_layout::inventory_entries +
                  static_cast<Address>(expected.inventory_index()) * game_layout::inventory_stride);
    if (!record || !*record)
        return EditRejected{EditRejectReason::Unreadable, {}};
    if (read_value<std::uint32_t>(read, *record + game_layout::item_id) != expected.item_id)
        return EditRejected{EditRejectReason::Stale, {}};
    const auto effects_address = *record + game_layout::item_effects;
    const auto curses_address = *record + game_layout::item_curses;
    const auto get_effects = [&]() -> std::optional<Effects> {
        EncodedEffects fields;
        if (!read(effects_address,
                  std::as_writable_bytes(std::span(fields).first(effects_per_relic))) ||
            !read(curses_address,
                  std::as_writable_bytes(std::span(fields).subspan(effects_per_relic))))
            return std::nullopt;
        return decode_effects(fields);
    };
    const auto actual = get_effects();
    if (!actual || *actual != expected.effects)
        return EditRejected{EditRejectReason::Stale, {}};
    if (read_edit_location(read, roots) != EditLocation::RoundtableHold)
        return EditRejected{EditRejectReason::NotAtRoundtable, {}};
    if (!relic_owner_matches(read, roots, owner))
        return EditRejected{EditRejectReason::Stale, {}};
    const bool write_curses =
        invalid || validator.items().find(expected.item_id)->family == RelicFamily::Deep;
    const auto put_effects = [&](const Effects &effects, bool restoring) {
        const auto fields = encode_effects(effects);
        const bool positive =
            write(effects_address, std::as_bytes(std::span(fields).first(effects_per_relic)));
        if ((!positive && !restoring) || !relic_owner_matches(read, roots, owner))
            return false;
        // Restoration tries both ranges, including after a failed positive write.
        const bool curses =
            !write_curses ||
            write(curses_address, std::as_bytes(std::span(fields).subspan(effects_per_relic)));
        return positive && curses;
    };
    const auto restore = [&] {
        if (!relic_owner_matches(read, roots, owner))
            return Restoration::Unconfirmed;
        // A failed re-equip may have applied replacements. Unequip before restoring IDs.
        if (equip && equip(RelicEquipStep::Unequip) != RelicEquipResult::Complete)
            return Restoration::Unconfirmed;
        put_effects(expected.effects, true);
        if (!relic_owner_matches(read, roots, owner))
            return Restoration::Unconfirmed;
        const auto restored = get_effects();
        if (!restored || *restored != expected.effects)
            return Restoration::Unconfirmed;
        if (equip && equip(RelicEquipStep::Equip) != RelicEquipResult::Complete)
            return Restoration::Unconfirmed;
        const auto final = equip ? get_effects() : restored;
        return final && *final == expected.effects && relic_owner_matches(read, roots, owner)
                   ? Restoration::Verified
                   : Restoration::Unconfirmed;
    };
    if (equip) {
        const auto detached = equip(RelicEquipStep::Unequip);
        if (detached == RelicEquipResult::Unavailable)
            return EditRejected{EditRejectReason::EquipUnavailable, {}};
        if (detached == RelicEquipResult::Failed)
            return WriteFailure{WriteStage::Equipment, restore()};
    }
    if (!put_effects(ordered, false)) {
        return WriteFailure{WriteStage::Write, restore()};
    }
    const auto verified = get_effects();
    if (!verified || *verified != ordered || !relic_owner_matches(read, roots, owner)) {
        return WriteFailure{WriteStage::Verification, restore()};
    }
    if (equip && equip(RelicEquipStep::Equip) != RelicEquipResult::Complete)
        return WriteFailure{WriteStage::Equipment, restore()};
    const auto final = equip ? get_effects() : verified;
    if (!final || *final != ordered || !relic_owner_matches(read, roots, owner))
        return WriteFailure{WriteStage::Verification, restore()};
    return Relic{expected.handle, expected.item_id, ordered};
}
} // namespace ern
