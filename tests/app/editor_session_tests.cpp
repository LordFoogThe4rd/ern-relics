#include "app/editor_session.hpp"
#include "game/relic_writer.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include <cstring>

int main() {
    return test::run("editing session, stale equipment, preset staging and partial retry", [] {
        const ern::RelicValidator rules{synthetic_rules()};
        const ern::RuleContext context = ern::DlcState::Unknown;
        test::Fixture fixture;
        const auto owner =
            std::get<ern::RelicSnapshot>(ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4))
                .owner;
        auto live =
            std::get<ern::RelicSnapshot>(ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4))
                .relics;
        ern::EditorSession session;
        session.sync(live);
        session.set_effect(0, {0, ern::EffectField::Effect}, 7000400);
        auto changed = live;
        changed.slots[0].reset();
        changed.slots[1]->handle = 99;
        session.sync(changed);
        test::require(session.drafts()[0] && session.drafts()[1]->original.inventory_index() == 99,
                      "Sync lost a dirty draft or failed to update a clean slot");
        test::require(session.review(&changed, rules, context).blocked->rejection.reason ==
                          ern::EditRejectReason::Stale,
                      "Missing equipped relic did not block a preserved draft");
        session.reload(0, changed);
        test::require(!session.drafts()[0], "Reload did not clear an empty destination");
        session.sync(live);
        const ern::Effects replacement{
            {7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        const ern::Preset preset{{replacement, replacement, replacement}};
        auto locked = live;
        locked.slots[2]->item_id = 0x80002710;
        const auto staged = session.load_preset(preset, locked, rules, context);
        test::require(staged.drafts.slots[0] && staged.drafts.slots[1] && !staged.drafts.slots[2] &&
                          session.drafts()[2]->effects == live.slots[2]->effects,
                      "Preset changed a skipped slot or failed to stage valid effects");
        session.discard();
        test::require(session.review(&live, rules, context).relic_count == 0,
                      "Discard left pending changes");
        session.load_preset(preset, live, rules, context);
        const auto write = [&](std::size_t slot, const ern::Relic &expected, ern::Effects effects) {
            return ern::replace_equipped(
                fixture.reader(),
                [&](ern::Address address, std::span<const std::byte> bytes) {
                    std::memcpy(fixture.bytes.data() + address - test::Fixture::base, bytes.data(),
                                bytes.size());
                    return true;
                },
                fixture.roots, 0x2F4, owner, slot, expected, effects, rules, context);
        };
        const auto partial =
            session.apply(&live, rules, context,
                          [&](std::size_t slot, const ern::Relic &expected,
                              ern::Effects effects) -> ern::EditResult {
                              if (slot == 1)
                                  return ern::WriteFailure{ern::WriteStage::Verification,
                                                           ern::Restoration::Unconfirmed};
                              return write(slot, expected, effects);
                          });
        test::require(partial.applied == 1 && partial.failure && partial.failure->slot == 1 &&
                          std::get<ern::WriteFailure>(partial.failure->failure).restoration ==
                              ern::Restoration::Unconfirmed,
                      "Session lost the failed slot or its recovery outcome");
        live =
            std::get<ern::RelicSnapshot>(ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4))
                .relics;
        session.sync(live);
        const auto retried = session.apply(&live, rules, context, write);
        test::require(!retried.failure && retried.applied == 2,
                      "Retry repeated a completed write or lost pending drafts");
        live =
            std::get<ern::RelicSnapshot>(ern::read_equipped(fixture.reader(), fixture.roots, 0x2F4))
                .relics;
        test::require(session.review(&live, rules, context).relic_count == 0,
                      "Successful writes did not clean their drafts");
        session.set_effect(0, {0, ern::EffectField::Effect}, 7000401);
        unsigned int calls = 0;
        const auto blocked = session.apply(
            &live, rules, ern::UnknownRules{"Changed source"},
            [&](std::size_t, const ern::Relic &expected, ern::Effects) -> ern::EditResult {
                ++calls;
                return expected;
            });
        test::require(blocked.failure && calls == 0 &&
                          std::get<ern::EditRejected>(blocked.failure->failure).reason ==
                              ern::EditRejectReason::RulesUnavailable,
                      "Application bypassed unavailable rules or erased the typed rejection");
    });
}
