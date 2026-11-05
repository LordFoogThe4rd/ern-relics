#include "game/relic_equipment.hpp"
#include "game/game_layout.hpp"

namespace ern {
RelicEquipReadResult read_relic_equip_context(const MemoryRead &read, RelicRoots roots,
                                              Address module) {
    const auto observed = read_relic_owner(read, roots);
    const auto *owner = std::get_if<RelicOwner>(&observed);
    if (!owner) {
        const auto reason = std::get<ReadFailure>(observed);
        return RelicEquipFailure{reason == ReadFailure::NotLoaded ? "owner.not_loaded"
                                 : reason == ReadFailure::Changed ? "owner.changed"
                                                                  : "owner.unreadable",
                                 roots.game_data_global,
                                 {},
                                 {}};
    }
    if (!roots.world_chr_global)
        return RelicEquipFailure{"world.global", 0, Address(0), {}};
    RelicEquipFailure failure{};
    const auto pointer = [&](Address address, std::string_view check) {
        const auto value = read_value<Address>(read, address);
        if (!value || !*value) {
            failure = {check, address, value, {}};
            return std::optional<Address>{};
        }
        return value;
    };
    const auto matches = [&]<typename T>(Address address, T expected, std::string_view check) {
        const auto value = read_value<T>(read, address);
        if (value == expected)
            return true;
        failure = {check, address, value ? std::optional<Address>(*value) : std::nullopt,
                   static_cast<Address>(expected)};
        return false;
    };
    const auto world = pointer(roots.world_chr_global, "world.pointer");
    if (!world)
        return failure;
    const auto character = pointer(*world + game_layout::live_player, "character.pointer");
    if (!character)
        return failure;
    const auto controller =
        pointer(*character + game_layout::relic_effect_controller, "controller.pointer");
    if (!controller)
        return failure;
    const auto equipment =
        pointer(*character + game_layout::character_equipment, "equipment.pointer");
    if (!equipment)
        return failure;
    const auto effects = pointer(*character + game_layout::character_effects, "effects.pointer");
    if (!effects)
        return failure;
    if (!matches(*character + game_layout::character_data, owner->player,
                 "character.player_data") ||
        !matches(*controller, module + game_layout::relic_effect_vtable_rva, "controller.vtable") ||
        !matches(*controller + game_layout::effect_owner, *character, "controller.owner") ||
        !matches(*controller + game_layout::effect_equipment, *equipment, "controller.equipment") ||
        !matches(*controller + game_layout::effect_manager, *effects, "controller.effects") ||
        !matches(*controller + game_layout::effect_count, game_layout::effect_entry_count,
                 "controller.entry_count"))
        return failure;
    const auto cached =
        pointer(*controller + game_layout::effect_cache, "controller.cache_pointer");
    if (!cached)
        return failure;
    const auto mode = read_value<std::uint8_t>(read, *controller + game_layout::effect_mode);
    if (!mode)
        return RelicEquipFailure{
            "controller.mode_read", *controller + game_layout::effect_mode, {}, {}};
    RelicEquipContext context{*owner, *character, *controller, *equipment, {}, {}, *mode};
    if (!read(owner->player + game_layout::equipped_relics,
              std::as_writable_bytes(std::span(context.equipped))))
        return RelicEquipFailure{
            "equipped_handles.read", owner->player + game_layout::equipped_relics, {}, {}};
    if (!read(*cached, std::as_writable_bytes(std::span(context.cached))))
        return RelicEquipFailure{"cached_handles.read", *cached, {}, {}};
    if (!relic_owner_matches(read, roots, *owner))
        return RelicEquipFailure{"owner.changed", roots.game_data_global, {}, {}};
    if (!matches(*world + game_layout::live_player, *character, "character.changed") ||
        !matches(*character + game_layout::character_data, owner->player,
                 "character.player_data_changed") ||
        !matches(*character + game_layout::character_equipment, *equipment, "equipment.changed") ||
        !matches(*character + game_layout::character_effects, *effects, "effects.changed") ||
        !matches(*character + game_layout::relic_effect_controller, *controller,
                 "controller.changed"))
        return failure;
    return context;
}

EditResult replace_equipped_native(const MemoryRead &read, const MemoryWrite &write,
                                   const RelicEditRequest &request, Address module,
                                   Address character, const RelicEquipCalls &calls,
                                   const RelicEquipReport &report) {
    std::optional<RelicEquipContext> original;
    const auto equipment = request.owner.player + game_layout::player_equipment;
    const auto inventory = equipment + game_layout::equipment_inventory;
    const auto cycle = [&](RelicEquipStep step) -> RelicEquipResult {
        const auto reject = [&](RelicEquipFailure failure,
                                RelicEquipResult result = RelicEquipResult::Unavailable) {
            if (report)
                report(step, failure);
            return result;
        };
        const auto observed = read_relic_equip_context(read, request.roots, module);
        const auto *context = std::get_if<RelicEquipContext>(&observed);
        if (!context)
            return reject(std::get<RelicEquipFailure>(observed));
        if (context->character != character)
            return reject({"callback.character", 0, character, context->character});
        if (context->owner != request.owner)
            return reject({"request.owner", 0, context->owner.player, request.owner.player});
        if (context->mode != 0)
            return reject({"controller.mode", context->controller + game_layout::effect_mode,
                           context->mode, Address(0)});
        if (request.slot >= relic_slot_count)
            return reject({"request.slot", 0, request.slot, relic_slot_count - 1});
        const auto handles_match = [&](const auto &actual, const auto &expected,
                                       std::string_view check) -> std::optional<RelicEquipFailure> {
            for (std::size_t slot = 0; slot < relic_slot_count; ++slot)
                if (actual[slot] != expected[slot])
                    return RelicEquipFailure{check, 0, actual[slot], expected[slot], slot};
            return std::nullopt;
        };
        if (!original) {
            if (step != RelicEquipStep::Unequip)
                return reject({"cycle.order", 0, {}, {}});
            if (const auto failure =
                    handles_match(context->cached, context->equipped, "cached_handles"))
                return reject(*failure);
            if (context->equipped[request.slot] == 0 ||
                context->equipped[request.slot] != request.expected.handle)
                return reject({"equipped_handle", 0, context->equipped[request.slot],
                               request.expected.handle, request.slot});
            const auto found = calls.find_item(inventory, context->equipped[request.slot]);
            if (found < 0)
                return reject({"inventory.lookup", inventory, static_cast<Address>(found), {}});
            original = *context;
        }
        if (context->controller != original->controller)
            return reject(
                {"cycle.controller_changed", 0, context->controller, original->controller});
        if (context->equipment != original->equipment)
            return reject({"cycle.equipment_changed", 0, context->equipment, original->equipment});
        auto unequipped = original->equipped;
        unequipped[request.slot] = 0;
        if (context->equipped != original->equipped && context->equipped != unequipped)
            return reject({"cycle.loadout_changed", 0, {}, {}});
        if (step == RelicEquipStep::Unequip && context->equipped == unequipped &&
            context->cached == unequipped)
            return RelicEquipResult::Complete;
        if (step == RelicEquipStep::Equip) {
            if (const auto failure =
                    handles_match(context->equipped, unequipped, "cycle.equipped_handles"))
                return reject(*failure);
            if (const auto failure =
                    handles_match(context->cached, unequipped, "cycle.cached_handles"))
                return reject(*failure);
        }
        // A full refresh can change inventory positions while updating starting items.
        // Resolve the saved GaItemHandle again immediately before re-equipping it.
        const auto index = step == RelicEquipStep::Unequip
                               ? -1
                               : calls.find_item(inventory, original->equipped[request.slot]);
        if (step == RelicEquipStep::Equip && index < 0)
            return reject({"inventory.lookup", inventory, static_cast<Address>(index), {}},
                          RelicEquipResult::Failed);
        calls.set_slot(
            equipment,
            game_layout::first_relic_equip_slot + static_cast<std::uint32_t>(request.slot), index);
        calls.refresh(character);
        const auto result_read = read_relic_equip_context(read, request.roots, module);
        const auto *result = std::get_if<RelicEquipContext>(&result_read);
        if (!result)
            return reject(std::get<RelicEquipFailure>(result_read), RelicEquipResult::Failed);
        const auto expected = step == RelicEquipStep::Unequip ? unequipped : original->equipped;
        if (result->owner != request.owner || result->character != character ||
            result->controller != original->controller || result->equipment != original->equipment)
            return reject({"refresh.context_changed", 0, {}, {}}, RelicEquipResult::Failed);
        if (result->mode != 0)
            return reject({"refresh.mode", result->controller + game_layout::effect_mode,
                           result->mode, Address(0)},
                          RelicEquipResult::Failed);
        if (const auto failure =
                handles_match(result->equipped, expected, "refresh.equipped_handles"))
            return reject(*failure, RelicEquipResult::Failed);
        if (const auto failure = handles_match(result->cached, expected, "refresh.cached_handles"))
            return reject(*failure, RelicEquipResult::Failed);
        return RelicEquipResult::Complete;
    };
    return replace_equipped(read, write, request.roots, request.equipped_offset, request.owner,
                            request.slot, request.expected, request.replacement, *request.rules,
                            request.context, cycle);
}
} // namespace ern
