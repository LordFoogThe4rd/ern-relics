#include "game/game_layout.hpp"
#include "game/relic_equipment.hpp"
#include "game/relic_writer.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include <tuple>

using test::require;
namespace {
struct NativeFixture {
    static constexpr ern::Address module = 0x140000000;
    static constexpr auto character = test::Fixture::base + 0x5000;
    static constexpr auto controller = test::Fixture::base + 0x6000;
    static constexpr auto cache = test::Fixture::base + 0x7000;
    static constexpr auto equipment = test::Fixture::base + 0x7800;
    static constexpr auto effects = test::Fixture::base + 0x7900;
    test::Fixture memory;
    std::size_t slot;
    ern::RelicSnapshot before;
    std::shared_ptr<const ern::RelicValidator> rules =
        std::make_shared<const ern::RelicValidator>(synthetic_rules());
    std::optional<ern::Effects> active;
    std::vector<std::int32_t> lookup_positions, assigned_positions;
    std::vector<std::string_view> rejections;
    std::int32_t inventory_position = 7;
    unsigned int refreshes = 0, writes = 0;
    std::function<void(unsigned int)> after_refresh;

    explicit NativeFixture(bool deep = false) : slot(deep ? 3 : 0) {
        if (deep)
            memory.equip_deep(slot);
        memory.put(character + ern::game_layout::relic_effect_controller, controller);
        memory.put(character + ern::game_layout::character_data, test::Fixture::base + 0x1000);
        memory.put(character + ern::game_layout::character_equipment, equipment);
        memory.put(character + ern::game_layout::character_effects, effects);
        memory.put(controller, module + ern::game_layout::relic_effect_vtable_rva);
        memory.put(controller + ern::game_layout::effect_owner, character);
        memory.put(controller + ern::game_layout::effect_manager, effects);
        memory.put(controller + ern::game_layout::effect_equipment, equipment);
        memory.put(controller + ern::game_layout::effect_cache, cache);
        memory.put(controller + ern::game_layout::effect_count,
                   ern::game_layout::effect_entry_count);
        for (std::size_t i = 0; i < ern::relic_slot_count; ++i) {
            const auto handle = i < 3 || i == slot ? std::uint32_t(0xC0800001 + i) : 0u;
            memory.put(test::Fixture::base + 0x1000 + ern::game_layout::equipped_relics + i * 4,
                       handle);
            memory.put(cache + i * 4, handle);
        }
        before = std::get<ern::RelicSnapshot>(
            ern::read_equipped(memory.reader(), memory.roots, ern::game_layout::equipped_relics));
        active = before.relics.slots[slot]->effects;
    }

    ern::Effects inventory() {
        const auto record = test::Fixture::base + 0x3000 + slot * 0x100;
        ern::EncodedEffects result;
        require(memory.read(record + ern::game_layout::item_effects,
                            std::as_writable_bytes(std::span(result).first(3))) &&
                    memory.read(record + ern::game_layout::item_curses,
                                std::as_writable_bytes(std::span(result).subspan(3))),
                "Cannot inspect native sequence inventory");
        return ern::decode_effects(result);
    }

    ern::RelicEquipCalls calls() {
        return {
            [this](ern::Address inventory_address, std::uint32_t handle) {
                require(inventory_address == before.owner.player +
                                                 ern::game_layout::player_equipment +
                                                 ern::game_layout::equipment_inventory &&
                            handle == before.relics.slots[slot]->handle,
                        "Native lookup lost the inventory address or full saved handle");
                lookup_positions.push_back(inventory_position);
                return inventory_position;
            },
            [this](ern::Address equipment_address, std::uint32_t equipment_slot,
                   std::int32_t index) {
                require(equipment_address ==
                                before.owner.player + ern::game_layout::player_equipment &&
                            equipment_slot == ern::game_layout::first_relic_equip_slot + slot &&
                            (index == -1 || index == inventory_position),
                        "Native setter received the wrong equipment, slot, or inventory position");
                assigned_positions.push_back(index);
                memory.put(before.owner.player + ern::game_layout::equipped_relics + slot * 4,
                           index == -1 ? 0u : before.relics.slots[slot]->handle);
            },
            [this](ern::Address observed_character) {
                require(observed_character == character, "Refreshed the wrong character");
                const auto handles =
                    ern::read_value<std::array<std::uint32_t, ern::relic_slot_count>>(
                        memory.reader(), before.owner.player + ern::game_layout::equipped_relics);
                require(handles.has_value(), "Cannot read the synthetic equipped handles");
                for (std::size_t i = 0; i < handles->size(); ++i)
                    memory.put(cache + i * 4, (*handles)[i]);
                if ((*handles)[slot])
                    active = inventory();
                else {
                    active.reset();
                    // Full refreshes can move inventory positions while retaining item handles.
                    inventory_position = 19;
                }
                ++refreshes;
                if (after_refresh)
                    after_refresh(refreshes);
            }};
    }

    ern::EditResult commit(ern::Address callback_character = character) {
        const auto &original = *before.relics.slots[slot];
        const auto replacement =
            slot >= 3
                ? ern::Effects{{7000400, UINT32_MAX}, {7000601, UINT32_MAX}, {7001801, 900002}}
                : ern::Effects{{7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        const ern::RelicEditRequest request{memory.roots, ern::game_layout::equipped_relics,
                                            before.owner, slot,
                                            original,     replacement,
                                            rules,        ern::DlcState::Unknown};
        const auto write = [this](ern::Address address, std::span<const std::byte> bytes) {
            require(!active, "Native sequence wrote IDs before detaching live effects");
            ++writes;
            std::memcpy(memory.bytes.data() + address - test::Fixture::base, bytes.data(),
                        bytes.size());
            return true;
        };
        return ern::replace_equipped_native(
            memory.reader(), write, request, module, callback_character, calls(),
            [this](ern::RelicEquipStep, const ern::RelicEquipFailure &failure) {
                rejections.push_back(failure.check);
            });
    }
};

void native_sequence_success() {
    for (const bool deep : {false, true}) {
        NativeFixture fixture(deep);
        const auto result = fixture.commit();
        require(std::holds_alternative<ern::Relic>(result) && fixture.rejections.empty(),
                "Native sequence rejected the supported context");
        require(fixture.active == std::get<ern::Relic>(result).effects &&
                    fixture.inventory() == *fixture.active,
                "Native sequence left inventory and active effects inconsistent");
        require(fixture.lookup_positions == std::vector<std::int32_t>{7, 19} &&
                    fixture.assigned_positions == std::vector<std::int32_t>{-1, 19} &&
                    fixture.refreshes == 2 && fixture.writes == (deep ? 2u : 1u),
                "Native sequence lost the re-resolved inventory position or equip/write ordering");
    }
}

void native_sequence_initial_rejections() {
    for (const auto failure :
         {"cached_handles", "callback.character", "controller.mode", "inventory.lookup"}) {
        NativeFixture fixture;
        auto callback_character = NativeFixture::character;
        if (std::string_view(failure) == "cached_handles")
            fixture.memory.put(NativeFixture::cache + 4, std::uint32_t(0));
        else if (std::string_view(failure) == "callback.character")
            ++callback_character;
        else if (std::string_view(failure) == "controller.mode")
            fixture.memory.put(NativeFixture::controller + ern::game_layout::effect_mode,
                               std::uint8_t(1));
        else
            fixture.inventory_position = -1;
        const auto original_memory = fixture.memory.bytes;
        const auto result = fixture.commit(callback_character);
        require(std::get<ern::EditRejected>(result).reason ==
                        ern::EditRejectReason::EquipUnavailable &&
                    fixture.rejections == std::vector<std::string_view>{failure} &&
                    fixture.assigned_positions.empty() && fixture.refreshes == 0 &&
                    fixture.writes == 0 && fixture.memory.bytes == original_memory,
                "A failed native precondition changed inventory or active equipment");
    }
}

void native_sequence_context_drift() {
    for (const bool owner_changed : {false, true}) {
        NativeFixture fixture;
        fixture.after_refresh = [&](unsigned int count) {
            if (count != 1)
                return;
            if (owner_changed) {
                std::memcpy(fixture.memory.bytes.data() + 0x2000,
                            fixture.memory.bytes.data() + 0x1000, 0x400);
                fixture.memory.put(test::Fixture::base + 0x308, test::Fixture::base + 0x2000);
                fixture.memory.put(NativeFixture::character + ern::game_layout::character_data,
                                   test::Fixture::base + 0x2000);
            } else {
                std::memcpy(fixture.memory.bytes.data() + 0x6100,
                            fixture.memory.bytes.data() + 0x6000, 0x100);
                fixture.memory.put(NativeFixture::character +
                                       ern::game_layout::relic_effect_controller,
                                   NativeFixture::controller + 0x100);
            }
        };
        const auto result = fixture.commit();
        require(std::get<ern::WriteFailure>(result).stage == ern::WriteStage::Equipment &&
                    std::get<ern::WriteFailure>(result).restoration ==
                        ern::Restoration::Unconfirmed &&
                    fixture.assigned_positions == std::vector<std::int32_t>{-1} &&
                    fixture.writes == 0 && fixture.refreshes == 1 && !fixture.rejections.empty() &&
                    fixture.rejections.front() == "refresh.context_changed",
                "Native sequence continued after the owner or controller changed");
    }
}

void native_sequence_detach_verification() {
    for (const bool cached : {false, true}) {
        NativeFixture fixture(true);
        const auto original = fixture.before.relics.slots[fixture.slot]->effects;
        fixture.after_refresh = [&](unsigned int count) {
            if (count == 1) {
                const auto address =
                    cached ? NativeFixture::cache
                           : fixture.before.owner.player + ern::game_layout::equipped_relics;
                fixture.memory.put(address + fixture.slot * 4,
                                   fixture.before.relics.slots[fixture.slot]->handle);
            }
        };
        const auto result = fixture.commit();
        const auto failure = std::get<ern::WriteFailure>(result);
        require(failure.stage == ern::WriteStage::Equipment &&
                    failure.restoration == ern::Restoration::Verified &&
                    fixture.inventory() == original && fixture.active == original &&
                    fixture.assigned_positions == std::vector<std::int32_t>{-1, -1, 19} &&
                    fixture.refreshes == 3 && fixture.writes == 2 &&
                    fixture.rejections ==
                        std::vector<std::string_view>{cached ? "refresh.cached_handles"
                                                             : "refresh.equipped_handles"},
                "A failed detachment verification bypassed recovery or wrote replacement effects");
    }
}

void native_sequence_missing_item() {
    NativeFixture fixture(true);
    const auto original = fixture.before.relics.slots[fixture.slot]->effects;
    fixture.after_refresh = [&](unsigned int count) {
        if (count == 1)
            fixture.inventory_position = -1;
    };
    const auto result = fixture.commit();
    const auto failure = std::get<ern::WriteFailure>(result);
    require(failure.stage == ern::WriteStage::Equipment &&
                failure.restoration == ern::Restoration::Unconfirmed &&
                fixture.inventory() == original && !fixture.active &&
                fixture.assigned_positions == std::vector<std::int32_t>{-1} &&
                fixture.lookup_positions == std::vector<std::int32_t>{7, -1, -1},
            "A missing inventory item reused a stale position during Equip or recovery");
}

void native_sequence_recovery() {
    for (const bool recovery_fails : {false, true}) {
        NativeFixture fixture(true);
        const auto original = fixture.before.relics.slots[fixture.slot]->effects;
        fixture.after_refresh = [&](unsigned int count) {
            if (count == 2 || (recovery_fails && count == 4))
                fixture.memory.put(NativeFixture::cache + fixture.slot * 4, std::uint32_t(0));
        };
        const auto result = fixture.commit();
        const auto failure = std::get<ern::WriteFailure>(result);
        require(failure.stage == ern::WriteStage::Equipment &&
                    failure.restoration == (recovery_fails ? ern::Restoration::Unconfirmed
                                                           : ern::Restoration::Verified) &&
                    fixture.inventory() == original && fixture.active == original &&
                    fixture.assigned_positions == std::vector<std::int32_t>{-1, 19, -1, 19} &&
                    fixture.refreshes == 4 && fixture.writes == 4,
                "Native recovery failed to detach, restore IDs, and verify the final refresh");
    }
}

void controller_contract() {
    test::Fixture memory;
    constexpr ern::Address module = 0x140000000;
    constexpr auto character = test::Fixture::base + 0x5000;
    constexpr auto controller = test::Fixture::base + 0x6000;
    constexpr auto cache = test::Fixture::base + 0x7000;
    constexpr auto equipment = test::Fixture::base + 0x7800;
    constexpr auto effects = test::Fixture::base + 0x7900;
    memory.put(character + ern::game_layout::relic_effect_controller, controller);
    memory.put(character + ern::game_layout::character_data, test::Fixture::base + 0x1000);
    memory.put(character + ern::game_layout::character_equipment, equipment);
    memory.put(character + ern::game_layout::character_effects, effects);
    memory.put(controller, module + ern::game_layout::relic_effect_vtable_rva);
    memory.put(controller + 8, character);
    memory.put(controller + 0x10, effects);
    memory.put(controller + 0x28, equipment);
    memory.put(controller + 0x18, cache);
    memory.put(controller + 0x20, std::uint32_t(24));
    for (std::size_t slot = 0; slot < ern::relic_slot_count; ++slot) {
        const auto handle = slot < 3 ? std::uint32_t(0xC0800001 + slot) : 0u;
        memory.put(test::Fixture::base + 0x1000 + 0x2F4 + slot * 4, handle);
        memory.put(cache + slot * 4, handle);
    }
    const auto observed = ern::read_relic_equip_context(memory.reader(), memory.roots, module);
    const auto *context = std::get_if<ern::RelicEquipContext>(&observed);
    require(context && context->controller == controller && context->character == character &&
                context->equipment == equipment && context->cached == context->equipped &&
                context->equipped[0] == 0xC0800001,
            "Native controller lost ownership or full equipped handles");
    const auto original = memory.bytes;
    for (const auto &[address, value, check] :
         {std::tuple{controller, ern::Address(0), "controller.vtable"},
          std::tuple{controller + 8, character + 0x100, "controller.owner"},
          std::tuple{controller + 0x18, ern::Address(0), "controller.cache_pointer"},
          std::tuple{controller + 0x10, effects + 0x100, "controller.effects"},
          std::tuple{controller + 0x28, equipment + 0x100, "controller.equipment"},
          std::tuple{character + 0x5A0, test::Fixture::base + 0x1100, "character.player_data"},
          std::tuple{character + 0x670, ern::Address(0), "equipment.pointer"},
          std::tuple{character + 0x650, ern::Address(0), "controller.pointer"},
          std::tuple{character + 0x190, ern::Address(0), "effects.pointer"},
          std::tuple{memory.roots.world_chr_global, ern::Address(0), "world.pointer"},
          std::tuple{test::Fixture::base + 0x8000 + 0x174E8, ern::Address(0),
                     "character.pointer"}}) {
        memory.put(address, value);
        const auto failed = ern::read_relic_equip_context(memory.reader(), memory.roots, module);
        const auto &failure = std::get<ern::RelicEquipFailure>(failed);
        require(failure.check == check && failure.address == address && failure.actual == value,
                "Native rejection did not identify the failed field and actual value");
        memory.bytes = original;
    }
    memory.put(controller + 0x20, std::uint32_t(6));
    const auto count = std::get<ern::RelicEquipFailure>(
        ern::read_relic_equip_context(memory.reader(), memory.roots, module));
    require(count.check == "controller.entry_count" && count.actual == 6 && count.expected == 24,
            "Entry count diagnostic lost the observed and expected counts");
    memory.bytes = original;
    for (const auto &[address, check] :
         {std::pair{controller, "controller.vtable"},
          std::pair{controller + 0x38, "controller.mode_read"},
          std::pair{cache, "cached_handles.read"},
          std::pair{test::Fixture::base + 0x1000 + 0x2F4, "equipped_handles.read"}}) {
        const auto unreadable = [&](ern::Address location, std::span<std::byte> bytes) {
            return location != address && memory.read(location, bytes);
        };
        const auto failure = std::get<ern::RelicEquipFailure>(
            ern::read_relic_equip_context(unreadable, memory.roots, module));
        require(failure.check == check && failure.address == address && !failure.actual,
                "An unreadable field was confused with a readable zero");
    }
    auto changing = [&](ern::Address address, std::span<std::byte> bytes) {
        if (address == cache)
            memory.put(character + 0x650, ern::Address(0));
        return memory.read(address, bytes);
    };
    const auto changed = std::get<ern::RelicEquipFailure>(
        ern::read_relic_equip_context(changing, memory.roots, module));
    require(changed.check == "controller.changed" && changed.actual == 0 &&
                changed.expected == controller,
            "Controller replacement during a read did not report its values");
}

enum class Failure { None, Unavailable, Unequip, Write, Verification, Equip, Recovery, Owner };

void transaction(bool deep, Failure failure) {
    test::Fixture memory;
    const std::size_t slot = deep ? 3 : 0;
    if (deep)
        memory.equip_deep(slot);
    memory.put(test::Fixture::base + 0x1000 + 0x2F4 + slot * 4,
               static_cast<std::uint32_t>(0xC0800001 + slot));
    const auto read = memory.reader();
    const auto snapshot =
        std::get<ern::RelicSnapshot>(ern::read_equipped(read, memory.roots, 0x2F4));
    const auto before = *snapshot.relics.slots[slot];
    const auto original_memory = memory.bytes;
    const auto slot_address = snapshot.owner.player + 0x2F4 + slot * 4;
    std::uint32_t original_handle{};
    require(read(slot_address, std::as_writable_bytes(std::span(&original_handle, 1))),
            "Cannot read the original equip handle");
    const auto replacement =
        deep ? ern::Effects{{7000400, UINT32_MAX}, {7000601, UINT32_MAX}, {7001801, 900002}}
             : ern::Effects{{7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
    const ern::RelicValidator rules{synthetic_rules()};
    unsigned writes = 0, unequips = 0, equips = 0;
    std::optional<ern::Effects> active = before.effects;
    bool equipped = true;
    const auto inventory = [&] {
        const auto record = test::Fixture::base + 0x3000 + slot * 0x100;
        ern::EncodedEffects effects;
        require(read(record + 0x18, std::as_writable_bytes(std::span(effects).first(3))) &&
                    read(record + 0x40, std::as_writable_bytes(std::span(effects).subspan(3))),
                "Cannot read the temporarily unequipped relic");
        return ern::decode_effects(effects);
    };
    const auto refresh = [&](ern::RelicEquipStep step) {
        if (step == ern::RelicEquipStep::Unequip) {
            ++unequips;
            if (failure == Failure::Unavailable)
                return ern::RelicEquipResult::Unavailable;
            if (active)
                require(*active == inventory(),
                        "Inventory IDs changed before removing live effects");
            active.reset();
            equipped = false;
            memory.put(slot_address, std::uint32_t(0));
            return failure == Failure::Unequip && unequips == 1 ? ern::RelicEquipResult::Failed
                                                                : ern::RelicEquipResult::Complete;
        }
        ++equips;
        equipped = true;
        memory.put(slot_address, original_handle);
        active = inventory();
        return (failure == Failure::Equip && equips == 1) || failure == Failure::Recovery
                   ? ern::RelicEquipResult::Failed
                   : ern::RelicEquipResult::Complete;
    };
    const auto write = [&](ern::Address address, std::span<const std::byte> input) {
        require(!equipped && !active,
                "A write or restoration ran while the relic was still equipped");
        ++writes;
        std::memcpy(memory.bytes.data() + address - test::Fixture::base, input.data(),
                    input.size());
        if (failure == Failure::Verification && writes == 1)
            memory.put(address, std::uint32_t(7000402));
        if (failure == Failure::Owner && writes == 1)
            memory.put(test::Fixture::base + 0x308, ern::Address(0));
        return !((failure == Failure::Write || failure == Failure::Recovery) && writes == 1);
    };
    const auto result =
        ern::replace_equipped(read, write, memory.roots, 0x2F4, snapshot.owner, slot, before,
                              replacement, rules, ern::DlcState::Unknown, refresh);
    if (failure == Failure::None) {
        require(std::holds_alternative<ern::Relic>(result) && equipped && active == replacement &&
                    std::get<ern::Relic>(result).handle == original_handle &&
                    inventory() == replacement && unequips == 1 && equips == 1,
                "Successful edit did not re-equip the relic with its new effects");
    } else if (failure == Failure::Unavailable) {
        require(std::get<ern::EditRejected>(result).reason ==
                        ern::EditRejectReason::EquipUnavailable &&
                    writes == 0 && equips == 0 && memory.bytes == original_memory &&
                    active == before.effects,
                "Unavailable refresh changed memory or live effects");
    } else {
        const auto error = std::get<ern::WriteFailure>(result);
        const auto stage = failure == Failure::Verification ? ern::WriteStage::Verification
                           : failure == Failure::Equip || failure == Failure::Unequip
                               ? ern::WriteStage::Equipment
                               : ern::WriteStage::Write;
        require(error.stage == stage, "Failure lost the correct write/refresh stage");
        if (failure == Failure::Owner) {
            require(error.restoration == ern::Restoration::Unconfirmed && writes == 1 &&
                        unequips == 1 && equips == 0,
                    "An owner change accessed the old controller or continued restoration");
        } else {
            require(memory.bytes == original_memory && active == before.effects,
                    "Failure did not restore both inventory ranges and active effects");
            require(error.restoration == (failure == Failure::Recovery
                                              ? ern::Restoration::Unconfirmed
                                              : ern::Restoration::Verified),
                    "Recovery reported the wrong verification state");
            require(unequips == 2, "Restoration did not unequip before rewriting IDs");
        }
    }
}

void rejected_before_unequip() {
    test::Fixture memory;
    const auto read = memory.reader();
    const auto loaded = std::get<ern::RelicSnapshot>(ern::read_equipped(read, memory.roots, 0x2F4));
    auto expected = *loaded.relics.slots[0];
    expected.effects.rows[0].effect = 7000400;
    unsigned calls = 0;
    const auto refresh = [&](ern::RelicEquipStep) {
        ++calls;
        return ern::RelicEquipResult::Complete;
    };
    const auto write = [&](ern::Address, std::span<const std::byte>) {
        ++calls;
        return true;
    };
    const ern::RelicValidator rules{synthetic_rules()};
    const auto result =
        ern::replace_equipped(read, write, memory.roots, 0x2F4, loaded.owner, 0, expected,
                              expected.effects, rules, ern::DlcState::Unknown, refresh);
    require(std::get<ern::EditRejected>(result).reason == ern::EditRejectReason::Stale && !calls,
            "A stale edit reached native effects or the memory writer");
}
} // namespace

int main() {
    return test::run("native context, unequip/write/equip ordering and recovery", [] {
        controller_contract();
        native_sequence_success();
        native_sequence_initial_rejections();
        native_sequence_context_drift();
        native_sequence_detach_verification();
        native_sequence_missing_item();
        native_sequence_recovery();
        rejected_before_unequip();
        for (const bool deep : {false, true})
            for (const auto failure :
                 {Failure::None, Failure::Unavailable, Failure::Unequip, Failure::Write,
                  Failure::Verification, Failure::Equip, Failure::Recovery, Failure::Owner})
                transaction(deep, failure);
    });
}
