#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "game/edit_location.hpp"

using test::Fixture;
using test::require;

int main() {
    return test::run("location, equipped memory and read races", [] {
        Fixture fixture;
        require(ern::read_edit_location(fixture.reader(), fixture.roots) ==
                    ern::EditLocation::RoundtableHold,
                "Loaded Roundtable was not recognized");
        fixture.put(Fixture::base + 0x300 + 0xD8, std::uint8_t(1));
        require(ern::read_edit_location(fixture.reader(), fixture.roots) ==
                    ern::EditLocation::OutsideRoundtable,
                "Expedition allowed relic editing");
        fixture.put(Fixture::base + 0x300 + 0xD8, std::uint8_t(255));
        require(ern::read_edit_location(fixture.reader(), fixture.roots) !=
                    ern::EditLocation::RoundtableHold,
                "Unknown world type allowed relic editing");
        fixture.put(Fixture::base + 0x300 + 0xD8, std::uint8_t(0));
        fixture.put(Fixture::base + 0x8000 + 0x174E8, ern::Address(0));
        require(ern::read_edit_location(fixture.reader(), fixture.roots) ==
                    ern::EditLocation::NotLoaded,
                "Confirmed player absence was not distinguished from a failed read");
        fixture.put(Fixture::base + 0x8000 + 0x174E8, ern::Address(Fixture::base + 0x5000));
        const auto failed_location_read = [&](ern::Address address, std::span<std::byte> output) {
            return address != Fixture::base + 0x8000 + 0x174E8 && fixture.read(address, output);
        };
        require(ern::read_edit_location(failed_location_read, fixture.roots) ==
                    ern::EditLocation::Unavailable,
                "An unreadable player pointer was mistaken for a confirmed unload");
        auto missing_world = fixture.roots;
        missing_world.world_chr_global = 0;
        require(ern::read_edit_location(fixture.reader(), missing_world) ==
                    ern::EditLocation::Unavailable,
                "Missing location signature allowed relic editing");
        unsigned int location_reads = 0;
        auto leaving_roundtable = [&](ern::Address address, std::span<std::byte> output) {
            if (address == Fixture::base + 0x300 + 0xD8 && ++location_reads == 2)
                fixture.put(address, std::uint8_t(1));
            return fixture.read(address, output);
        };
        require(ern::read_edit_location(leaving_roundtable, fixture.roots) ==
                    ern::EditLocation::Unavailable,
                "Location transition was accepted as Roundtable");
        fixture.put(Fixture::base + 0x300 + 0xD8, std::uint8_t(0));
        auto result = ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4);
        require(std::holds_alternative<ern::RelicSnapshot>(result),
                "Equipped relic fixture failed");
        const auto snapshot = std::get<ern::RelicSnapshot>(result);
        require(snapshot.owner == ern::RelicOwner{Fixture::base + 0x100, Fixture::base + 0x300,
                                                  Fixture::base + 0x1000},
                "Read discarded the player/inventory context");
        auto relics = snapshot.relics;
        require(relics.slots[0]->effects.rows[0].effect == 7000401 &&
                    relics.slots[2]->inventory_index() == 3,
                "Relic decoding failed");
        for (const auto address : {fixture.roots.item_manager_global,
                                   fixture.roots.game_data_global, Fixture::base + 0x308}) {
            const auto unreadable = [&](ern::Address at, std::span<std::byte> output) {
                return at != address && fixture.read(at, output);
            };
            require(std::get<ern::ReadFailure>(ern::read_equipped(
                        unreadable, fixture.roots, 0x2F4)) == ern::ReadFailure::Unreadable,
                    "An unreadable owner pointer was mistaken for a confirmed unload");
            Fixture unloaded;
            unloaded.put(address, ern::Address{0});
            require(std::get<ern::ReadFailure>(ern::read_equipped(
                        unloaded.reader(), unloaded.roots, 0x2F4)) == ern::ReadFailure::NotLoaded,
                    "A readable null owner pointer did not confirm an unload");
        }
        {
            Fixture reused;
            reused.put(Fixture::base + 0x1000 + 0x2F4, std::uint32_t(0xC0800001));
            const auto loaded = std::get<ern::RelicSnapshot>(
                ern::read_equipped(reused.reader(), reused.roots, 0x2F4));
            require(loaded.relics.slots[0]->handle == 0xC0800001 &&
                        loaded.relics.slots[0]->inventory_index() == 1 &&
                        loaded.relics.slots[0] != relics.slots[0],
                    "A new equipment handle compared equal to an old relic at the same index");
        }
        {
            Fixture empty_deep;
            for (std::size_t slot = 3; slot < ern::relic_slot_count; ++slot)
                empty_deep.put(Fixture::base + 0x1000 + 0x2F4 + slot * 4, std::uint32_t(0));
            const auto loaded = ern::read_equipped(empty_deep.reader(), empty_deep.roots, 0x2F4);
            require(std::holds_alternative<ern::RelicSnapshot>(loaded),
                    "Live zero handles in empty Deep slots blocked normal relics");
            const auto &slots = std::get<ern::RelicSnapshot>(loaded).relics.slots;
            require(slots[0] && slots[1] && slots[2] && !slots[3] && !slots[4] && !slots[5],
                    "Zero handles were resolved as inventory records");
        }
        fixture.put(Fixture::base + 0x1000 + 0x2F8, UINT16_MAX);
        result = ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4);
        require(!std::get<ern::RelicSnapshot>(result).relics.slots[1],
                "Empty relic not recognized");
        fixture.put(Fixture::base + 0x1000 + 0x2F4, std::uint16_t(0xFFFE));
        result = ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4);
        require(std::get<ern::ReadFailure>(result) == ern::ReadFailure::Unreadable,
                "Invalid inventory pointer accepted");
        fixture.put(Fixture::base, ern::Address(0));
        result = ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4);
        require(std::get<ern::ReadFailure>(result) == ern::ReadFailure::NotLoaded,
                "Unloaded game not recognized");

        Fixture changing;
        unsigned int index_reads = 0;
        auto changing_read = [&](ern::Address address, std::span<std::byte> output) {
            if (address == Fixture::base + 0x1000 + 0x2F4 && ++index_reads == 2)
                changing.put(address, std::uint16_t(6));
            return changing.read(address, output);
        };
        result = ern::read_equipped(changing_read, changing.roots, 0x2F4);
        require(std::get<ern::ReadFailure>(result) == ern::ReadFailure::Changed,
                "Equipment race not rejected");
        Fixture handle_change;
        index_reads = 0;
        const auto handle_read = [&](ern::Address address, std::span<std::byte> output) {
            if (address == Fixture::base + 0x1000 + 0x2F4 && ++index_reads == 2)
                handle_change.put(address, std::uint32_t(0xC0800001));
            return handle_change.read(address, output);
        };
        result = ern::read_equipped(handle_read, handle_change.roots, 0x2F4);
        require(std::get<ern::ReadFailure>(result) == ern::ReadFailure::Changed,
                "A changed handle with the same inventory index was accepted");
    });
}
