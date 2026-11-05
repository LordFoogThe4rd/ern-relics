#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include "game/relic_writer.hpp"

#include <cstring>

using test::Fixture;
using test::require;

int main() {
    return test::run("write preconditions, identity races and rollback", [] {
        ern::RelicValidator validator{synthetic_rules()};
        const ern::RuleContext context = ern::DlcState::Unknown;
        const ern::Effects original{
            {7000401, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        const ern::Effects edited{
            {7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        Fixture writable;
        const auto owner = std::get<ern::RelicSnapshot>(
                               ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                               .owner;
        auto live = std::get<ern::RelicSnapshot>(
                        ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                        .relics;
        const auto selected = *live.slots[0];
        unsigned int writes = 0;
        auto writer = [&](ern::Address address, std::span<const std::byte> input) {
            ++writes;
            if (address < Fixture::base || address - Fixture::base > writable.bytes.size() ||
                input.size() > writable.bytes.size() - (address - Fixture::base))
                return false;
            std::memcpy(writable.bytes.data() + address - Fixture::base, input.data(),
                        input.size());
            return true;
        };
        const auto bytes_before = writable.bytes;
        for (const auto slot : {std::size_t{0}, std::size_t{3}}) {
            Fixture reused;
            reused.equip_deep();
            const auto reviewed = std::get<ern::RelicSnapshot>(
                ern::read_equipped(reused.reader(), reused.roots, 0x2F4));
            auto replacement = reviewed.relics.slots[slot]->effects;
            replacement.rows[0].effect = 7000400;
            reused.put(Fixture::base + 0x1000 + 0x2F4 + slot * 4,
                       std::uint32_t(0xC0800000 | (slot + 1)));
            const auto rejected = ern::replace_equipped(
                reused.reader(),
                [](ern::Address, std::span<const std::byte>) -> bool {
                    throw std::runtime_error("A replaced equipment handle reached the writer");
                },
                reused.roots, 0x2F4, reviewed.owner, slot, *reviewed.relics.slots[slot],
                replacement, validator, context);
            require(std::get<ern::EditRejected>(rejected).reason == ern::EditRejectReason::Stale,
                    "A reused inventory index authorized an old normal or Deep draft");
        }
        for (const auto mode : {std::uint8_t(1), std::uint8_t(255)}) {
            writable.put(Fixture::base + 0x300 + 0xD8, mode);
            const auto before_blocked = writable.bytes;
            const auto result =
                ern::replace_equipped(writable.reader(), writer, writable.roots, 0x2F4, owner, 0,
                                      selected, edited, validator, context);
            require(std::get<ern::EditRejected>(result).reason ==
                            ern::EditRejectReason::NotAtRoundtable &&
                        !writes && writable.bytes == before_blocked,
                    "A write outside Roundtable changed memory");
        }
        writable.put(Fixture::base + 0x300 + 0xD8, std::uint8_t(0));
        auto unavailable_roots = writable.roots;
        unavailable_roots.world_chr_global = 0;
        auto unavailable =
            ern::replace_equipped(writable.reader(), writer, unavailable_roots, 0x2F4, owner, 0,
                                  selected, edited, validator, context);
        require(std::get<ern::EditRejected>(unavailable).reason ==
                        ern::EditRejectReason::NotAtRoundtable &&
                    !writes && writable.bytes == bytes_before,
                "Unavailable location allowed a write");
        unsigned int effect_reads = 0;
        auto leave_before_write = [&](ern::Address address, std::span<std::byte> output) {
            if (address == Fixture::base + 0x3018 && ++effect_reads == 2)
                writable.put(Fixture::base + 0x300 + 0xD8, std::uint8_t(1));
            return writable.read(address, output);
        };
        unavailable = ern::replace_equipped(leave_before_write, writer, writable.roots, 0x2F4,
                                            owner, 0, selected, edited, validator, context);
        require(std::get<ern::EditRejected>(unavailable).reason ==
                        ern::EditRejectReason::NotAtRoundtable &&
                    !writes,
                "Leaving Roundtable between validation and write allowed an edit");
        writable.put(Fixture::base + 0x300 + 0xD8, std::uint8_t(0));
        require(writable.bytes == bytes_before, "Blocked edit changed relic memory");
        const ern::Effects boss_effects{
            {10000, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        auto blocked = ern::replace_equipped(writable.reader(), writer, writable.roots, 0x2F4,
                                             owner, 0, selected, boss_effects, validator, context);
        require(std::get<ern::EditRejected>(blocked).reason == ern::EditRejectReason::Invalid &&
                    !writes && writable.bytes == bytes_before,
                "A boss-only effect reached the memory writer");
        blocked = ern::replace_equipped(writable.reader(), writer, writable.roots, 0x2F4, owner, 0,
                                        selected, edited, validator,
                                        ern::UnknownRules{"Mismatched regulation"});
        require(std::get<ern::EditRejected>(blocked).reason == ern::EditRejectReason::Invalid &&
                    !writes && writable.bytes == bytes_before,
                "Unknown rules reached the memory writer");
        auto write_result = ern::replace_equipped(writable.reader(), writer, writable.roots, 0x2F4,
                                                  owner, 0, selected, edited, validator, context);
        require(std::holds_alternative<ern::Relic>(write_result) && writes == 1,
                "Valid edit failed");
        live = std::get<ern::RelicSnapshot>(
                   ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                   .relics;
        require(live.slots[0]->effects == edited && live.slots[1]->effects == original,
                "Edit changed the wrong relic");
        auto expected_bytes = bytes_before;
        const auto encoded = ern::encode_effects(edited);
        std::memcpy(expected_bytes.data() + 0x3018, encoded.data(), 12);
        require(writable.bytes == expected_bytes,
                "An edit changed identity, inventory, or memory outside the three effect IDs");
        write_result = ern::replace_equipped(writable.reader(), writer, writable.roots, 0x2F4,
                                             owner, 0, selected, edited, validator, context);
        require(std::get<ern::EditRejected>(write_result).reason == ern::EditRejectReason::Stale &&
                    writes == 1,
                "Stale draft wrote memory");
        const auto changed = *live.slots[0];
        bool fail_once = true;
        auto partial_writer = [&](ern::Address address, std::span<const std::byte> input) {
            if (fail_once) {
                fail_once = false;
                writer(address, input.first(4));
                return false;
            }
            return writer(address, input);
        };
        write_result =
            ern::replace_equipped(writable.reader(), partial_writer, writable.roots, 0x2F4, owner,
                                  0, changed, selected.effects, validator, context);
        require(std::get<ern::WriteFailure>(write_result).stage == ern::WriteStage::Write &&
                    std::get<ern::WriteFailure>(write_result).restoration ==
                        ern::Restoration::Verified,
                "Partial write not reported");
        live = std::get<ern::RelicSnapshot>(
                   ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                   .relics;
        require(live.slots[0]->effects == edited, "Partial write was not rolled back");

        for (const auto stage : {ern::WriteStage::Write, ern::WriteStage::Verification}) {
            for (const bool restore_succeeds : {false, true}) {
                Fixture recovery;
                const auto before =
                    *std::get<ern::RelicSnapshot>(
                         ern::read_equipped(recovery.reader(), recovery.roots, 0x2F4))
                         .relics.slots[0];
                unsigned int attempts = 0;
                const auto failed = ern::replace_equipped(
                    recovery.reader(),
                    [&](ern::Address address, std::span<const std::byte> bytes) {
                        ++attempts;
                        if (attempts == 1) {
                            const std::uint32_t corrupt = 42;
                            std::memcpy(recovery.bytes.data() + address - Fixture::base, &corrupt,
                                        sizeof(corrupt));
                            return stage == ern::WriteStage::Verification;
                        }
                        if (!restore_succeeds)
                            return false;
                        std::memcpy(recovery.bytes.data() + address - Fixture::base, bytes.data(),
                                    bytes.size());
                        return true;
                    },
                    recovery.roots, 0x2F4, owner, 0, before, edited, validator, context);
                const auto outcome = std::get<ern::WriteFailure>(failed);
                require(outcome.stage == stage &&
                            outcome.restoration == (restore_succeeds
                                                        ? ern::Restoration::Verified
                                                        : ern::Restoration::Unconfirmed),
                        "A failed write lost its stage or claimed an unverified restoration");
                const auto after = std::get<ern::RelicSnapshot>(
                                       ern::read_equipped(recovery.reader(), recovery.roots, 0x2F4))
                                       .relics;
                require((after.slots[0] == before) == restore_succeeds,
                        "Recovery reporting did not match actual relic memory");
            }
        }

        // Identical effect IDs must not make a unique item look editable.
        for (const auto locked_id : {0x80002710u, 0x800007D0u, 0x8FFFFFFFu, 0x80000000u | 11003u}) {
            writable.put(Fixture::base + 0x300C, locked_id);
            live = std::get<ern::RelicSnapshot>(
                       ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                       .relics;
            const auto count_before = writes;
            write_result =
                ern::replace_equipped(writable.reader(), writer, writable.roots, 0x2F4, owner, 0,
                                      *live.slots[0], selected.effects, validator, context);
            require(std::get<ern::EditRejected>(write_result).reason ==
                            ern::EditRejectReason::Locked &&
                        writes == count_before,
                    "Locked relic wrote memory");
        }
        writable.put(Fixture::base + 0x300C, std::uint32_t(0x80002710u));
        write_result =
            ern::replace_equipped(writable.reader(), writer, writable.roots, 0x2F4, owner, 0,
                                  changed, selected.effects, validator, context);
        require(std::get<ern::EditRejected>(write_result).reason == ern::EditRejectReason::Stale,
                "Item identity change accepted as the old draft");
        Fixture identity_race;
        const auto race_before =
            *std::get<ern::RelicSnapshot>(
                 ern::read_equipped(identity_race.reader(), identity_race.roots, 0x2F4))
                 .relics.slots[0];
        unsigned int identity_reads = 0;
        auto race_reader = [&](ern::Address address, std::span<std::byte> output) {
            if (address == Fixture::base + 0x300C && ++identity_reads == 2)
                identity_race.put(address, std::uint32_t(0x80002710u));
            return identity_race.read(address, output);
        };
        auto forbidden_writer = [](ern::Address, std::span<const std::byte>) -> bool {
            throw std::runtime_error("Writer called after item became unique");
        };
        write_result =
            ern::replace_equipped(race_reader, forbidden_writer, identity_race.roots, 0x2F4, owner,
                                  0, race_before, edited, validator, context);
        require(std::get<ern::EditRejected>(write_result).reason == ern::EditRejectReason::Stale,
                "Identity race was not rejected");
        for (const bool late : {false, true}) {
            Fixture changed_owner;
            std::memcpy(changed_owner.bytes.data() + 0x2000, changed_owner.bytes.data() + 0x1000,
                        0x400);
            if (!late)
                changed_owner.put(Fixture::base + 0x308, Fixture::base + 0x2000);
            unsigned int reads = 0;
            const auto reader = [&](ern::Address address, std::span<std::byte> output) {
                if (late && address == Fixture::base + 0x3018 && ++reads == 2)
                    changed_owner.put(Fixture::base + 0x308, Fixture::base + 0x2000);
                return changed_owner.read(address, output);
            };
            const auto rejected =
                ern::replace_equipped(reader, forbidden_writer, changed_owner.roots, 0x2F4, owner,
                                      0, race_before, edited, validator, context);
            require(std::get<ern::EditRejected>(rejected).reason == ern::EditRejectReason::Stale,
                    "Matching relic fields authorized a different player context");
        }
        {
            Fixture lost_owner;
            unsigned int attempts = 0;
            const auto failed = ern::replace_equipped(
                lost_owner.reader(),
                [&](ern::Address address, std::span<const std::byte> bytes) {
                    ++attempts;
                    std::memcpy(lost_owner.bytes.data() + address - Fixture::base, bytes.data(),
                                sizeof(std::uint32_t));
                    lost_owner.put(Fixture::base + 0x308, Fixture::base + 0x2000);
                    return false;
                },
                lost_owner.roots, 0x2F4, owner, 0, race_before, edited, validator, context);
            require(std::get<ern::WriteFailure>(failed).restoration ==
                            ern::Restoration::Unconfirmed &&
                        attempts == 1,
                    "Restoration wrote into a changed player context");
        }
        live = std::get<ern::RelicSnapshot>(
                   ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                   .relics;
        ern::RelicValidator missing_items;
        write_result =
            ern::replace_equipped(writable.reader(), forbidden_writer, writable.roots, 0x2F4, owner,
                                  0, *live.slots[0], edited, missing_items, context);
        require(std::get<ern::EditRejected>(write_result).reason == ern::EditRejectReason::Locked,
                "Missing item metadata did not lock writes");
    });
}
