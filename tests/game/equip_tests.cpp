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
    static constexpr auto bitmap = test::Fixture::base + 0x7200;
    static constexpr auto initializer_address =
        module + ern::game_layout::relic_cache_initializer_rva;
    static constexpr auto loop_address = module + ern::game_layout::relic_cache_loop_limit_rva;
    static constexpr auto scratch_address =
        module + ern::game_layout::relic_scratch_bitmap_initializer_rva;
    static constexpr auto count_address = controller + ern::game_layout::effect_count;
    test::Fixture memory;
    std::array<std::uint8_t, 12> initializer{0xBA, 0, 0, 0, 0, 0x32, 0xDB, 0xE8, 0xFE, 0xF8, 0, 0};
    std::array<std::uint8_t, 6> loop{0x41, 0x83, 0xFC, 0, 0x7D, 0x0B};
    std::array<std::uint8_t, 10> scratch{0x41, 0xB9, 0, 0, 0, 0, 0x4C, 0x2B, 0x4B, 0x20};
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
        set_capacity(24);
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

    void set_bitmap_sizes(std::uint32_t scratch_bits, ern::Address live_bits,
                          ern::Address live_bytes) {
        std::memcpy(scratch.data() + 2, &scratch_bits, sizeof(scratch_bits));
        memory.put(controller + ern::game_layout::effect_bitmap_bits, live_bits);
        memory.put(controller + ern::game_layout::effect_bitmap_words,
                   std::array{bitmap, bitmap + live_bytes});
    }

    void set_capacity(std::uint8_t capacity) {
        initializer[1] = capacity;
        loop[3] = capacity;
        memory.put(count_address, std::uint32_t(capacity));
        const auto bits = std::uint32_t(capacity) * 6;
        set_bitmap_sizes(bits, bits, ((bits + 31) / 32) * 4);
    }

    bool read(ern::Address address, std::span<std::byte> output) const {
        if (address == initializer_address && output.size() == initializer.size()) {
            std::memcpy(output.data(), initializer.data(), output.size());
            return true;
        }
        if (address == loop_address && output.size() == loop.size()) {
            std::memcpy(output.data(), loop.data(), output.size());
            return true;
        }
        if (address == scratch_address && output.size() == scratch.size()) {
            std::memcpy(output.data(), scratch.data(), output.size());
            return true;
        }
        return memory.read(address, output);
    }

    ern::MemoryRead reader() const {
        return [this](auto address, auto output) { return read(address, output); };
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
            reader(), write, request, module, callback_character, calls(),
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
    NativeFixture fixture;
    auto &memory = fixture.memory;
    constexpr auto module = NativeFixture::module;
    constexpr auto character = NativeFixture::character;
    constexpr auto controller = NativeFixture::controller;
    constexpr auto cache = NativeFixture::cache;
    constexpr auto equipment = NativeFixture::equipment;
    constexpr auto effects = NativeFixture::effects;
    const auto observed = ern::read_relic_equip_context(fixture.reader(), memory.roots, module);
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
        const auto failed = ern::read_relic_equip_context(fixture.reader(), memory.roots, module);
        const auto &failure = std::get<ern::RelicEquipFailure>(failed);
        require(failure.check == check && failure.address == address && failure.actual == value,
                "Native rejection did not identify the failed field and actual value");
        memory.bytes = original;
    }
    for (const auto &[address, check] :
         {std::pair{controller, "controller.vtable"},
          std::pair{controller + 0x38, "controller.mode_read"},
          std::pair{cache, "cached_handles.read"},
          std::pair{test::Fixture::base + 0x1000 + 0x2F4, "equipped_handles.read"}}) {
        const auto unreadable = [&](ern::Address location, std::span<std::byte> bytes) {
            return location != address && fixture.read(location, bytes);
        };
        const auto failure = std::get<ern::RelicEquipFailure>(
            ern::read_relic_equip_context(unreadable, memory.roots, module));
        require(failure.check == check && failure.address == address && !failure.actual,
                "An unreadable field was confused with a readable zero");
    }
    auto changing = [&](ern::Address address, std::span<std::byte> bytes) {
        if (address == cache)
            memory.put(character + 0x650, ern::Address(0));
        return fixture.read(address, bytes);
    };
    const auto changed = std::get<ern::RelicEquipFailure>(
        ern::read_relic_equip_context(changing, memory.roots, module));
    require(changed.check == "controller.changed" && changed.actual == 0 &&
                changed.expected == controller,
            "Controller replacement during a read did not report its values");
}

void controller_capacity() {
    for (const auto capacity : {std::uint8_t(48), std::uint8_t(72)}) {
        NativeFixture fixture;
        fixture.set_capacity(capacity);
        require(std::holds_alternative<ern::Relic>(fixture.commit()) && fixture.refreshes == 2,
                "A consistent larger cache failed the native edit cycle");
    }
    for (const auto &[initializer, loop, count, check, actual, expected] :
         {std::tuple{48, 48, 24, "controller.entry_count", 24, 48},
          std::tuple{48, 24, 48, "controller.loop_limit", 24, 48},
          std::tuple{0, 0, 0, "controller.allocation_count", 0, -1},
          std::tuple{25, 25, 25, "controller.allocation_count", 25, -1},
          std::tuple{132, 132, 132, "controller.allocation_count", 132, -1}}) {
        NativeFixture fixture;
        fixture.initializer[1] = static_cast<std::uint8_t>(initializer);
        fixture.loop[3] = static_cast<std::uint8_t>(loop);
        fixture.memory.put(NativeFixture::count_address, static_cast<std::uint32_t>(count));
        const auto failure = std::get<ern::RelicEquipFailure>(
            ern::read_relic_equip_context(fixture.reader(), fixture.memory.roots, fixture.module));
        require(failure.check == check && failure.actual == static_cast<ern::Address>(actual) &&
                    failure.expected == (expected < 0 ? std::optional<ern::Address>{}
                                                      : static_cast<ern::Address>(expected)),
                "A capacity mismatch lost its check or values");
        const auto detail = ern::relic_capacity_error(failure);
        const auto result = fixture.commit();
        require(detail && detail->find(check) != std::string::npos &&
                    std::get<ern::EditRejected>(result).detail == *detail &&
                    fixture.assigned_positions.empty() && fixture.writes == 0,
                "An inconsistent capacity lost its visible rejection or changed equipment");
    }
    for (const bool initializer : {false, true}) {
        NativeFixture fixture;
        if (initializer)
            fixture.initializer[7] = 0xE9;
        else
            fixture.loop[4] = 0xEB;
        const auto result = fixture.commit();
        const auto check = initializer ? "controller.initializer_code" : "controller.loop_code";
        require(std::get<ern::EditRejected>(result).detail.find(check) != std::string::npos &&
                    fixture.assigned_positions.empty() && fixture.writes == 0,
                "Unexpected controller instructions were not reported before editing");
    }
}

void controller_bitmap_capacity() {
    for (const auto &[scratch, live, storage, check, observed, expected] :
         {std::tuple{144u, 288u, 36u, "controller.scratch_bitmap_bits", 144u, 288u},
          std::tuple{288u, 144u, 20u, "controller.live_bitmap_bits", 144u, 288u},
          std::tuple{288u, 288u, 20u, "controller.live_bitmap_storage", 20u, 36u}}) {
        NativeFixture fixture;
        fixture.set_capacity(48);
        fixture.set_bitmap_sizes(scratch, live, storage);
        const auto result = fixture.commit();
        const auto detail = "Relic controller check failed (" + std::string(check) + "). Found " +
                            std::to_string(observed) + ". Expected " + std::to_string(expected) +
                            ".";
        require(std::get<ern::EditRejected>(result).detail == detail &&
                    fixture.assigned_positions.empty() && fixture.refreshes == 0 &&
                    fixture.writes == 0,
                "A partial bitmap resize reached the native refresh or lost its diagnostic");
    }

    NativeFixture fixture;
    fixture.scratch[1] = 0xB8;
    const auto result = fixture.commit();
    require(std::get<ern::EditRejected>(result).detail.find("controller.scratch_bitmap_code") !=
                    std::string::npos &&
                fixture.assigned_positions.empty() && fixture.refreshes == 0,
            "An unknown scratch bitmap initializer reached the native refresh");
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
        controller_capacity();
        controller_bitmap_capacity();
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
