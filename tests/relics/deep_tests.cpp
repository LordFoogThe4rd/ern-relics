#include "app/runtime.hpp"
#include "game/relic_writer.hpp"
#include "relics/preset.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include <fstream>

using test::require;
namespace {
constexpr std::uint32_t deep_id = 0x80000000u | 2000000u;
constexpr ern::Effects original{{7000401, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, 900001}};
constexpr ern::Effects replacement{{7000400, UINT32_MAX}, {7000601, UINT32_MAX}, {7001801, 900002}};

void validation() {
    const ern::RelicValidator rules{synthetic_rules()};
    const auto validate = [&](const ern::Effects &effects) {
        return rules.validate(deep_id, effects, ern::DlcState::Unknown);
    };
    require(!ern::relic_lock_reason(rules.items().find(deep_id)), "Rolled Deep Relic is locked");
    require(std::holds_alternative<ern::Obtainable>(validate(original)),
            "Deep effect/curse pair was rejected");
    require(std::holds_alternative<ern::CompatiblePair>(
                rules.validate_pair(7001801, 900001, ern::DlcState::Unknown)) &&
                std::holds_alternative<ern::CompatiblePair>(
                    rules.validate_pair(7000400, UINT32_MAX, ern::DlcState::Unknown)) &&
                std::holds_alternative<ern::Impossible>(
                    rules.validate_pair(7001801, UINT32_MAX, ern::DlcState::Unknown)) &&
                std::holds_alternative<ern::Impossible>(
                    rules.validate_pair(7000400, 900001, ern::DlcState::Unknown)),
            "Pair guidance disagrees with the positive and curse roll tables");
    require(std::holds_alternative<ern::UnknownRules>(
                rules.validate_pair(7001801, 0xFFFFFF00, ern::DlcState::Present)) &&
                std::holds_alternative<ern::UnknownRules>(
                    rules.validate_pair(7001801, 900001, ern::UnknownRules{"Unavailable"})),
            "Pair guidance presented unknown rules as a known compatibility result");
    auto shuffled = original;
    std::swap(shuffled.rows[0], shuffled.rows[2]);
    require(std::get<ern::Obtainable>(validate(shuffled)).effects == original,
            "Canonical ordering detached a curse from its positive effect");
    auto invalid = original;
    invalid.rows[2].curse = UINT32_MAX;
    require(std::holds_alternative<ern::Impossible>(validate(invalid)),
            "A curse-bound effect was accepted without a curse");
    invalid = original;
    std::swap(invalid.rows[0].curse, invalid.rows[2].curse);
    require(std::holds_alternative<ern::Impossible>(validate(invalid)),
            "A curse on the wrong row was accepted");
    invalid = original;
    invalid.rows[0].curse = 900002;
    require(std::holds_alternative<ern::Impossible>(validate(invalid)),
            "An extra curse was accepted");
    invalid = original;
    invalid.rows[2].curse = 7000400;
    require(std::holds_alternative<ern::Impossible>(validate(invalid)),
            "A positive effect was accepted as a curse");
    invalid = original;
    invalid.rows[2].curse = 0xFFFFFF00;
    require(std::holds_alternative<ern::UnknownRules>(validate(invalid)),
            "An unknown curse bypassed validation");
    invalid = original;
    invalid.rows[1].effect = UINT32_MAX;
    require(std::holds_alternative<ern::Impossible>(validate(invalid)),
            "Deep effect count was allowed to change");
    require(std::holds_alternative<ern::Impossible>(
                rules.validate(0x80000066, original, ern::DlcState::Unknown)),
            "A normal relic accepted curses");
    require(rules.candidates(deep_id).size() == 7 && rules.candidates(deep_id, true).size() == 3 &&
                rules.candidates(0x80000066, true).empty(),
            "Positive and curse candidate pools were mixed");
    auto custom = synthetic_rules();
    custom.items.at(2000000).effect_tables = {2100000, 2100000, 2100000};
    custom.items.at(2000000).curse_tables = {-1, -1, -1};
    custom.items.emplace(2000001, ern::RelicInfo{2000001,
                                                 ern::RelicKind::Rolled,
                                                 0,
                                                 0,
                                                 ern::empty_effects,
                                                 {2000000, -1, -1},
                                                 {3000000, -1, -1},
                                                 ern::RelicFamily::Deep});
    require(std::holds_alternative<ern::Obtainable>(
                ern::RelicValidator{custom}.validate(deep_id, original, ern::DlcState::Unknown)),
            "Deep validation incorrectly required the item's original lottery template");
    custom.pools.at(3000000).at(900001) = {false, true};
    const ern::RelicValidator dlc{custom};
    require(std::holds_alternative<ern::CompatiblePair>(
                dlc.validate_pair(7001801, 900001, ern::DlcState::Present)) &&
                std::holds_alternative<ern::Impossible>(
                    dlc.validate_pair(7001801, 900001, ern::DlcState::Absent)) &&
                std::holds_alternative<ern::UnknownRules>(
                    dlc.validate_pair(7001801, 900001, ern::DlcState::Unknown)),
            "Pair guidance ignored the active DLC weights");
    require(std::holds_alternative<ern::Impossible>(
                dlc.validate(deep_id, original, ern::DlcState::Absent)) &&
                std::holds_alternative<ern::Obtainable>(
                    dlc.validate(deep_id, original, ern::DlcState::Present)) &&
                std::holds_alternative<ern::UnknownRules>(
                    dlc.validate(deep_id, original, ern::DlcState::Unknown)),
            "Curse weights bypassed DLC ownership checks");
    custom.pools.erase(3000000);
    require(std::holds_alternative<ern::UnknownRules>(
                ern::RelicValidator{custom}.validate_pair(7001801, 900001, ern::DlcState::Present)),
            "Pair guidance accepted a missing curse table");
    require(std::holds_alternative<ern::UnknownRules>(
                ern::RelicValidator{custom}.validate(deep_id, original, ern::DlcState::Present)),
            "Missing curse table was accepted");
    custom = synthetic_rules();
    custom.effects.at(7000401).exclusivity = 100;
    custom.effects.at(7000602).exclusivity = 100;
    require(std::holds_alternative<ern::Impossible>(
                ern::RelicValidator{custom}.validate(deep_id, original, ern::DlcState::Present)),
            "Exclusivity group was ignored");
    invalid = {{7001801, 900001}, {7000602, UINT32_MAX}, {7001802, 900001}};
    require(std::holds_alternative<ern::Impossible>(validate(invalid)),
            "Duplicate curses were accepted");
}

void memory() {
    const ern::RelicValidator rules{synthetic_rules()};
    for (std::size_t slot = 3; slot < ern::relic_slot_count; ++slot) {
        test::Fixture fixture;
        fixture.equip_deep(slot);
        const auto read = fixture.reader();
        const auto snapshot =
            std::get<ern::RelicSnapshot>(ern::read_equipped(read, fixture.roots, 0x2F4));
        const auto before = *snapshot.relics.slots[slot];
        require(before.effects == original, "Reader lost the equipped Deep Relic's curses");
        const auto initial = fixture.bytes;
        const auto write = [&](ern::Address address, std::span<const std::byte> bytes) {
            std::memcpy(fixture.bytes.data() + address - test::Fixture::base, bytes.data(),
                        bytes.size());
            return true;
        };
        const auto replace = [&](const ern::MemoryWrite &writer) {
            return ern::replace_equipped(read, writer, fixture.roots, 0x2F4, snapshot.owner, slot,
                                         before, replacement, rules, ern::DlcState::Unknown);
        };
        const auto result = replace(write);
        require(std::holds_alternative<ern::Relic>(result), "Deep Relic write failed");
        auto expected = initial;
        const auto at = 0x3000 + slot * 0x100;
        const auto fields = ern::encode_effects(replacement);
        std::memcpy(expected.data() + at + 0x18, fields.data(), 12);
        std::memcpy(expected.data() + at + 0x40, fields.data() + 3, 12);
        require(fixture.bytes == expected, "Write changed bytes outside the six effect/curse IDs");
        require(std::get<ern::RelicSnapshot>(ern::read_equipped(read, fixture.roots, 0x2F4))
                        .relics.slots[slot]
                        ->effects == replacement,
                "Deep Relic readback differs from replacement");
        require(std::get<ern::EditRejected>(replace(write)).reason == ern::EditRejectReason::Stale,
                "Stale curse/effect draft wrote memory");
        fixture.bytes = initial;
        unsigned int calls = 0;
        const auto partial = [&](ern::Address address, std::span<const std::byte> bytes) {
            write(address, bytes);
            return ++calls != 2;
        };
        const auto failed = std::get<ern::WriteFailure>(replace(partial));
        require(failed.stage == ern::WriteStage::Write &&
                    failed.restoration == ern::Restoration::Verified && fixture.bytes == initial &&
                    calls == 4,
                "Failure on the curse write did not restore both ranges");
        fixture.bytes = initial;
        calls = 0;
        const auto corrupt = [&](ern::Address address, std::span<const std::byte> bytes) {
            write(address, bytes);
            if (++calls == 2)
                fixture.put(address, std::uint32_t(900003));
            return true;
        };
        const auto mismatched = std::get<ern::WriteFailure>(replace(corrupt));
        require(mismatched.stage == ern::WriteStage::Verification &&
                    mismatched.restoration == ern::Restoration::Verified &&
                    fixture.bytes == initial,
                "Curse readback mismatch was not detected and restored");
        fixture.bytes = initial;
        calls = 0;
        const auto lose_owner = [&](ern::Address address, std::span<const std::byte> bytes) {
            write(address, bytes);
            ++calls;
            fixture.put(test::Fixture::base + 0x308, ern::Address{0});
            return true;
        };
        const auto lost = std::get<ern::WriteFailure>(replace(lose_owner));
        require(lost.restoration == ern::Restoration::Unconfirmed && calls == 1,
                "A character change between ranges allowed a curse write or restoration");
        fixture.bytes = initial;
        fixture.put(test::Fixture::base + at + 0x40,
                    std::array<std::uint32_t, 3>{UINT32_MAX, UINT32_MAX, 900003});
        calls = 0;
        require(std::get<ern::EditRejected>(replace(partial)).reason ==
                        ern::EditRejectReason::Stale &&
                    !calls,
                "A changed curse was not detected before writing");
        fixture.bytes = initial;
        unsigned int index_reads = 0;
        const auto racing = [&](ern::Address address, std::span<std::byte> bytes) {
            if (address == test::Fixture::base + 0x1000 + 0x2F4 + slot * 4 && ++index_reads == 2)
                fixture.put(address, UINT16_MAX);
            return fixture.read(address, bytes);
        };
        require(std::get<ern::ReadFailure>(ern::read_equipped(racing, fixture.roots, 0x2F4)) ==
                    ern::ReadFailure::Changed,
                "A Deep equipment race produced an accepted snapshot");
    }
}

void workflow() {
    test::Fixture fixture;
    for (std::size_t slot = 3; slot < ern::relic_slot_count; ++slot)
        fixture.equip_deep(slot);
    const ern::RelicValidator rules{synthetic_rules()};
    const auto live =
        std::get<ern::RelicSnapshot>(ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4))
            .relics;
    const auto preset = ern::capture_preset(live, rules, ern::DlcState::Unknown);
    require(preset.slots[5] == original &&
                ern::parse_preset(ern::serialize_preset(preset)) == preset,
            "Six-slot preset did not retain paired curses");
    ern::EditorSession session;
    session.sync(live);
    session.set_effect(0, {0, ern::EffectField::Effect}, 7000400);
    session.set_effect(5, {2, ern::EffectField::Curse}, 900003);
    const auto review = session.review(&live, rules, ern::DlcState::Unknown);
    require(!review.blocked && review.relic_count == 2 && review.effect_count == 2,
            "Review omitted a Deep curse draft from a mixed batch");
    session.load_preset(preset, live, rules, ern::DlcState::Unknown);
    require(session.drafts()[5]->effects == original, "Preset staging dropped Deep curses");

    const auto directory =
        std::filesystem::temp_directory_path() /
        ("deep-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    ern::RuntimeEnvironment environment;
    environment.directory = directory;
    environment.executable_version = "Deep Relic fixture";
    environment.layout_verified = true;
    environment.rules = synthetic_rules();
    environment.read = fixture.reader();
    environment.write = [&](ern::Address address, std::span<const std::byte> bytes) {
        require(std::filesystem::file_size(directory / "relic-edits.jsonl") > 0,
                "Write preceded the journal");
        std::memcpy(fixture.bytes.data() + address - test::Fixture::base, bytes.data(),
                    bytes.size());
        return true;
    };
    environment.locate_roots = [&] {
        return std::variant<ern::RelicRoots, std::string>{fixture.roots};
    };
    environment.dlc = [] { return ern::DlcState::Unknown; };
    {
        ern::Runtime runtime;
        runtime.initialize(std::move(environment));
        runtime.refresh();
        for (const auto field : {ern::EffectField::Effect, ern::EffectField::Curse})
            for (std::size_t row = 0; row < replacement.rows.size(); ++row)
                runtime.editor().set_effect(5, {row, field},
                                            ern::effect_at(replacement, {row, field}));
        const auto applied = runtime.apply_drafts();
        require(!applied.failure && applied.applied == 1 && runtime.can_undo(),
                "Runtime rejected a complete Deep draft");
        const auto undone = runtime.undo();
        require(!undone.failure && undone.restored == 1 &&
                    std::get<ern::EquippedRelics>(runtime.inspection()).slots[5]->effects ==
                        original,
                "Undo did not restore the positive effects and curses");
        std::ifstream journal(directory / "relic-edits.jsonl");
        const std::string contents{std::istreambuf_iterator<char>{journal}, {}};
        require(contents.find("900001") != std::string::npos &&
                    contents.find("900002") != std::string::npos,
                "Journal did not record the original and replacement curses");
    }
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    return test::run("Deep slots, curse pairing, ordering, recovery, presets and Undo", [] {
        validation();
        memory();
        workflow();
    });
}
