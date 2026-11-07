#include "game/relic_equipment.hpp"
#include "game/game_layout.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace ern {
namespace {
template <std::size_t N>
std::optional<std::uint32_t> read_u32_operand(const MemoryRead &read, Address address,
                                              const std::array<std::uint8_t, N> &signature,
                                              std::size_t offset) {
    auto code = signature;
    if (!read(address, std::as_writable_bytes(std::span(code))))
        return {};
    std::uint32_t value{};
    std::memcpy(&value, code.data() + offset, sizeof(value));
    std::fill_n(code.begin() + offset, sizeof(value), std::uint8_t{});
    if (code != signature)
        return {};
    return value;
}

std::optional<RelicEquipFailure> check_cache_capacity(const MemoryRead &read, Address module,
                                                      Address controller) {
    const auto fail = [](std::string_view check, Address address,
                         std::optional<Address> actual = {}, std::optional<Address> expected = {}) {
        return RelicEquipFailure{check, address, actual, expected, {}, true};
    };
    // mov edx, imm32; xor bl, bl; call the native cache allocator.
    constexpr std::array<std::uint8_t, 12> initializer{0xBA, 0,    0,    0,    0, 0x32,
                                                       0xDB, 0xE8, 0xFE, 0xF8, 0, 0};
    const auto init_address = module + game_layout::relic_cache_initializer_rva;
    const auto allocation = read_u32_operand(read, init_address, initializer, 1);
    if (!allocation)
        return fail("controller.initializer_code", init_address);

    // cmp r12d, imm8; jge end. The loop advances through groups of six relics.
    constexpr std::array<std::uint8_t, 6> loop{0x41, 0x83, 0xFC, 0, 0x7D, 0x0B};
    const auto loop_address = module + game_layout::relic_cache_loop_limit_rva;
    auto loop_code = loop;
    if (!read(loop_address, std::as_writable_bytes(std::span(loop_code))))
        return fail("controller.loop_code", loop_address);
    const auto limit = loop_code[3];
    loop_code[3] = 0;
    if (loop_code != loop)
        return fail("controller.loop_code", loop_address);

    constexpr auto max_loop_count = std::uint32_t(std::numeric_limits<std::int8_t>::max());
    if (*allocation < relic_slot_count || *allocation > max_loop_count ||
        *allocation % relic_slot_count != 0)
        return fail("controller.allocation_count", init_address + 1, allocation);
    if (limit != *allocation)
        return fail("controller.loop_limit", loop_address + 3, limit, allocation);
    const auto count_address = controller + game_layout::effect_count;
    const auto count = read_value<std::uint32_t>(read, count_address);
    if (count != *allocation)
        return fail("controller.entry_count", count_address, count, allocation);

    // mov r9d, imm32; sub r9, [rbx+0x20]. Refresh copies the scratch bitmap
    // into the live vector<bool> using the scratch length as its copy bound.
    constexpr std::array<std::uint8_t, 10> scratch{0x41, 0xB9, 0, 0, 0, 0, 0x4C, 0x2B, 0x4B, 0x20};
    const auto scratch_address = module + game_layout::relic_scratch_bitmap_initializer_rva;
    const auto scratch_bits = read_u32_operand(read, scratch_address, scratch, 2);
    if (!scratch_bits)
        return fail("controller.scratch_bitmap_code", scratch_address);
    const auto expected_bits = *allocation * relic_slot_count;
    if (*scratch_bits != expected_bits)
        return fail("controller.scratch_bitmap_bits", scratch_address + 2, scratch_bits,
                    expected_bits);
    const auto live_bits_address = controller + game_layout::effect_bitmap_bits;
    const auto live_bits = read_value<Address>(read, live_bits_address);
    if (!live_bits || *live_bits < *scratch_bits)
        return fail("controller.live_bitmap_bits", live_bits_address, live_bits, scratch_bits);

    const auto words_address = controller + game_layout::effect_bitmap_words;
    const auto words = read_value<std::array<Address, 2>>(read, words_address);
    std::optional<Address> storage_bytes;
    if (words && (*words)[0] && (*words)[0] <= (*words)[1])
        storage_bytes = (*words)[1] - (*words)[0];
    constexpr auto word_bits = std::numeric_limits<std::uint32_t>::digits;
    const auto required_bytes =
        ((*scratch_bits + word_bits - 1) / word_bits) * sizeof(std::uint32_t);
    if (!storage_bytes || *storage_bytes < required_bytes)
        return fail("controller.live_bitmap_storage", words_address, storage_bytes, required_bytes);
    return {};
}
} // namespace

std::optional<std::string> relic_capacity_error(const RelicEquipFailure &failure) {
    if (!failure.capacity_check)
        return {};
    auto detail = "Relic controller check failed (" + std::string(failure.check) + ").";
    if (failure.actual)
        detail += " Found " + std::to_string(*failure.actual) + ".";
    if (failure.expected)
        detail += " Expected " + std::to_string(*failure.expected) + ".";
    return detail;
}

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
        !matches(*controller + game_layout::effect_manager, *effects, "controller.effects"))
        return failure;
    if (const auto capacity = check_cache_capacity(read, module, *controller))
        return *capacity;
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
    std::optional<std::string> capacity_error;
    const auto equipment = request.owner.player + game_layout::player_equipment;
    const auto inventory = equipment + game_layout::equipment_inventory;
    const auto cycle = [&](RelicEquipStep step) -> RelicEquipResult {
        const auto reject = [&](RelicEquipFailure failure,
                                RelicEquipResult result = RelicEquipResult::Unavailable) {
            if (report)
                report(step, failure);
            if (result == RelicEquipResult::Unavailable && !original && failure.capacity_check)
                capacity_error = relic_capacity_error(failure);
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
    auto result = replace_equipped(read, write, request.roots, request.equipped_offset,
                                   request.owner, request.slot, request.expected,
                                   request.replacement, *request.rules, request.context, cycle);
    if (auto *rejected = std::get_if<EditRejected>(&result); rejected && capacity_error)
        rejected->detail = std::move(*capacity_error);
    return result;
}
} // namespace ern
