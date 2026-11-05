#include "app/runtime.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include <algorithm>
#include <fstream>

using test::require;

namespace {
struct Fixture {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("undo-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    test::Fixture memory;
    ern::Runtime runtime;
    std::array<std::optional<ern::Effects>, ern::relic_slot_count> active;
    std::vector<std::size_t> commits;
    std::optional<std::size_t> blocked_slot;
    std::optional<std::size_t> change_owner_after_slot;
    std::optional<ern::Address> fail_write_at;

    Fixture() {
        std::filesystem::create_directories(directory);
        for (std::size_t slot = 3; slot < ern::relic_slot_count; ++slot)
            memory.equip_deep(slot);
        for (std::size_t slot = 0; slot < ern::relic_slot_count; ++slot)
            memory.put(test::Fixture::base + 0x1000 + ern::game_layout::equipped_relics + slot * 4,
                       static_cast<std::uint32_t>(0xC0800001 + slot));
        const auto initial = snapshot();
        for (std::size_t slot = 0; slot < ern::relic_slot_count; ++slot)
            active[slot] = initial.relics.slots[slot]->effects;
        ern::RuntimeEnvironment environment;
        environment.directory = directory;
        environment.layout_verified = true;
        environment.rules = synthetic_rules();
        environment.read = memory.reader();
        environment.locate_roots = [this] {
            return std::variant<ern::RelicRoots, std::string>{memory.roots};
        };
        environment.dlc = [] { return ern::DlcState::Unknown; };
        environment.commit = [this](ern::RelicEditRequest request) -> ern::EditResult {
            if (request.slot == blocked_slot)
                return ern::EditRejected{ern::EditRejectReason::EquipUnavailable,
                                         "Injected native commit rejection"};
            commits.push_back(request.slot);
            const auto write = [&](ern::Address address, std::span<const std::byte> bytes) {
                require(!active[request.slot], "Undo wrote effects before native unequip");
                if (address == fail_write_at) {
                    fail_write_at.reset();
                    std::memcpy(memory.bytes.data() + address - test::Fixture::base, bytes.data(),
                                4);
                    return false;
                }
                std::memcpy(memory.bytes.data() + address - test::Fixture::base, bytes.data(),
                            bytes.size());
                return true;
            };
            const auto cycle = [&](ern::RelicEquipStep step) {
                if (step == ern::RelicEquipStep::Unequip)
                    active[request.slot].reset();
                else
                    active[request.slot] = snapshot().relics.slots[request.slot]->effects;
                return ern::RelicEquipResult::Complete;
            };
            auto result = ern::replace_equipped(memory.reader(), write, request.roots,
                                                request.equipped_offset, request.owner,
                                                request.slot, request.expected, request.replacement,
                                                *request.rules, request.context, cycle);
            if (request.slot == change_owner_after_slot) {
                std::memmove(memory.bytes.data() + 0x2000, memory.bytes.data() + 0x1000, 0x400);
                memory.put(test::Fixture::base + 0x308, test::Fixture::base + 0x2000);
                change_owner_after_slot.reset();
            }
            return result;
        };
        runtime.initialize(std::move(environment));
        runtime.refresh();
    }
    ~Fixture() {
        std::filesystem::remove(directory / "relic-edits.jsonl");
        std::filesystem::remove(directory);
    }
    ern::RelicSnapshot snapshot() {
        return std::get<ern::RelicSnapshot>(
            ern::read_equipped(memory.reader(), memory.roots, ern::game_layout::equipped_relics));
    }
    void stage(std::span<const std::size_t> slots) {
        for (const auto slot : slots) {
            runtime.editor().set_effect(slot, {0, ern::EffectField::Effect}, 7000400);
            if (slot >= 3)
                runtime.editor().set_effect(slot, {2, ern::EffectField::Curse}, 900002);
        }
    }
};

void restores_last_apply() {
    for (const auto &slots : {std::vector<std::size_t>{0, 1}, {3, 4}, {0, 3}, {0, 1, 2, 3, 4, 5}}) {
        Fixture fixture;
        const auto before = fixture.memory.bytes;
        const auto active_before = fixture.active;
        fixture.stage(slots);
        const auto applied = fixture.runtime.apply_drafts();
        require(!applied.failure && applied.applied == slots.size() && fixture.runtime.can_undo(),
                "Could not apply the normal, Deep or mixed batch");
        const auto undone = fixture.runtime.undo();
        require(!undone.failure && undone.restored == slots.size() &&
                    fixture.memory.bytes == before && fixture.active == active_before &&
                    !fixture.runtime.can_undo(),
                "One Undo did not restore every relic and paired curse from the last Apply");
        auto expected = slots;
        expected.insert(expected.end(), slots.rbegin(), slots.rend());
        require(fixture.commits == expected, "Undo did not reverse the last Apply's commit order");
        std::ifstream journal(fixture.runtime.journal_path());
        const std::string contents{std::istreambuf_iterator<char>{journal}, {}};
        require(static_cast<std::size_t>(std::count(contents.begin(), contents.end(), '\n')) ==
                    slots.size() * 2,
                "A restored relic skipped its journal entry");
        require(fixture.runtime.undo().restored == 0 && fixture.commits == expected,
                "A second Undo repeated a completed batch");
    }
}

void retries_remaining_relics() {
    for (const auto &slots : {std::vector<std::size_t>{0, 1}, {3, 4}, {0, 3}}) {
        Fixture fixture;
        const auto before = fixture.memory.bytes;
        const auto active_before = fixture.active;
        fixture.stage(slots);
        require(fixture.runtime.apply_drafts().applied == 2, "Could not prepare Undo retry");
        fixture.blocked_slot = slots.front();
        auto undone = fixture.runtime.undo();
        require(undone.restored == 1 && undone.failure && undone.failure->slot == slots.front() &&
                    fixture.runtime.can_undo() && fixture.commits.size() == 3,
                "A partial Undo lost the remaining relic or reported the wrong count");
        undone = fixture.runtime.undo();
        require(undone.restored == 0 && undone.failure && fixture.commits.size() == 3,
                "Retry rewrote a relic that had already been restored");
        fixture.blocked_slot.reset();
        undone = fixture.runtime.undo();
        require(!undone.failure && undone.restored == 1 && !fixture.runtime.can_undo() &&
                    fixture.memory.bytes == before && fixture.active == active_before,
                "Retry did not restore the remaining normal or Deep relic");
    }
}

void undoes_only_successful_changes() {
    for (const auto &slots : {std::vector<std::size_t>{0, 1}, {3, 4}, {0, 3}}) {
        Fixture fixture;
        const auto before = fixture.memory.bytes;
        fixture.stage(slots);
        fixture.blocked_slot = slots.back();
        const auto applied = fixture.runtime.apply_drafts();
        require(applied.applied == 1 && applied.failure && fixture.runtime.can_undo(),
                "A partial Apply did not retain its completed relic for Undo");
        fixture.blocked_slot.reset();
        const auto undone = fixture.runtime.undo();
        require(!undone.failure && undone.restored == 1 && !fixture.runtime.can_undo() &&
                    fixture.memory.bytes == before && fixture.commits.size() == 2,
                "Undo included an unapplied draft or lost a successfully changed relic");
    }
}

void retries_after_a_partial_memory_write() {
    for (const auto slot : {std::size_t{0}, std::size_t{3}}) {
        Fixture fixture;
        const auto before = fixture.memory.bytes;
        const auto active_before = fixture.active;
        const std::array<std::size_t, 1> slots{slot};
        fixture.stage(slots);
        require(fixture.runtime.apply_drafts().applied == 1, "Could not prepare a failed Undo");
        const auto applied = fixture.memory.bytes;
        const auto active_applied = fixture.active;
        fixture.fail_write_at =
            test::Fixture::base + 0x3000 + slot * 0x100 + (slot >= 3 ? 0x40 : 0x18);
        const auto failed = fixture.runtime.undo();
        require(failed.restored == 0 && failed.failure &&
                    std::get<ern::WriteFailure>(failed.failure->failure).restoration ==
                        ern::Restoration::Verified &&
                    fixture.runtime.can_undo() && fixture.memory.bytes == applied &&
                    fixture.active == active_applied,
                "A failed Undo lost its retry or failed to recover both effect ranges");
        const auto retried = fixture.runtime.undo();
        require(!retried.failure && retried.restored == 1 && !fixture.runtime.can_undo() &&
                    fixture.memory.bytes == before && fixture.active == active_before,
                "Retry after a partial memory write did not restore the relic and its curses");
    }
}

void keeps_the_last_successful_batch() {
    Fixture fixture;
    const auto before = fixture.memory.bytes;
    const std::array<std::size_t, 2> slots{0, 3};
    fixture.stage(slots);
    require(fixture.runtime.apply_drafts().applied == 2, "Could not prepare prior Undo batch");
    require(fixture.runtime.apply_drafts().applied == 0 && fixture.runtime.can_undo(),
            "A no-op Apply discarded the previous Undo batch");
    fixture.runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000602);
    require(fixture.runtime.apply_drafts().failure && fixture.runtime.can_undo(),
            "An invalid draft discarded the previous Undo batch");
    fixture.runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000402);
    fixture.blocked_slot = 0;
    require(fixture.runtime.apply_drafts().failure && fixture.commits.size() == 2,
            "A refused Apply reached native memory");
    fixture.blocked_slot.reset();
    const auto undone = fixture.runtime.undo();
    require(!undone.failure && undone.restored == 2 && fixture.memory.bytes == before,
            "A refused Apply replaced a still-usable Undo batch");

    fixture.runtime.editor().discard();
    fixture.runtime.refresh();
    fixture.stage(slots);
    require(fixture.runtime.apply_drafts().applied == 2, "Could not prepare the first Apply");
    const auto after_first = fixture.memory.bytes;
    const std::array<std::size_t, 2> next_slots{1, 4};
    fixture.stage(next_slots);
    require(fixture.runtime.apply_drafts().applied == 2, "Could not prepare the latest Apply");
    require(fixture.runtime.undo().restored == 2 && fixture.memory.bytes == after_first &&
                !fixture.runtime.can_undo(),
            "Undo restored an older Apply instead of just the latest batch");
}

void rejects_stale_relics_and_character_changes() {
    Fixture fixture;
    const auto before = fixture.snapshot().relics;
    const std::array<std::size_t, 2> slots{0, 3};
    fixture.stage(slots);
    require(fixture.runtime.apply_drafts().applied == 2, "Could not prepare stale Undo test");
    auto external = fixture.snapshot().relics.slots[0]->effects;
    external.rows[0].effect = 7000402;
    fixture.memory.put(test::Fixture::base + 0x3018, external);
    fixture.active[0] = external;
    const auto undone = fixture.runtime.undo();
    require(undone.restored == 1 && undone.failure && undone.failure->slot == 0 &&
                std::get<ern::EditRejected>(undone.failure->failure).reason ==
                    ern::EditRejectReason::Stale &&
                fixture.snapshot().relics.slots[0]->effects == external &&
                fixture.snapshot().relics.slots[3] == before.slots[3],
            "Undo overwrote an externally changed relic or misreported partial restoration");

    Fixture changing;
    changing.stage(slots);
    require(changing.runtime.apply_drafts().applied == 2, "Could not prepare owner-change test");
    const auto session = changing.runtime.session();
    changing.change_owner_after_slot = 3;
    const auto interrupted = changing.runtime.undo();
    require(interrupted.restored == 1 && interrupted.failure &&
                std::get<ern::EditRejected>(interrupted.failure->failure).reason ==
                    ern::EditRejectReason::Stale &&
                !changing.runtime.can_undo() && changing.commits.size() == 3 &&
                changing.runtime.session() != session &&
                changing.snapshot().relics.slots[0]->effects.rows[0].effect == 7000400,
            "A character change carried a pending Undo into the next character");
}
void rejects_reused_inventory_indices() {
    for (const auto slot : {std::size_t{0}, std::size_t{3}}) {
        Fixture fixture;
        const std::array<std::size_t, 1> slots{slot};
        fixture.stage(slots);
        require(fixture.runtime.apply_drafts().applied == 1, "Could not prepare stale handle Undo");
        fixture.memory.put(test::Fixture::base + 0x1000 + ern::game_layout::equipped_relics +
                               slot * 4,
                           static_cast<std::uint32_t>(0xC0810001 + slot));
        const auto before = fixture.memory.bytes;
        const auto journal_size = std::filesystem::file_size(fixture.runtime.journal_path());
        const auto result = fixture.runtime.undo();
        require(
            result.restored == 0 && result.failure && result.failure->slot == slot &&
                std::get<ern::EditRejected>(result.failure->failure).reason ==
                    ern::EditRejectReason::Stale &&
                fixture.runtime.can_undo() && fixture.commits.size() == 1 &&
                fixture.memory.bytes == before &&
                std::filesystem::file_size(fixture.runtime.journal_path()) == journal_size,
            "Undo accepted a new handle at the same inventory index or journaled a stale relic");
    }
}
} // namespace

int main() {
    return test::run("Undo batches, partial retries, stale relics and character ownership", [] {
        restores_last_apply();
        retries_remaining_relics();
        undoes_only_successful_changes();
        retries_after_a_partial_memory_write();
        keeps_the_last_successful_batch();
        rejects_stale_relics_and_character_changes();
        rejects_reused_inventory_indices();
    });
}
