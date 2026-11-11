#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include "relics/relic_drafts.hpp"
#include "game/relic_writer.hpp"

#include <cstring>
#include <string>

using test::Fixture;
using test::require;

int main() {
    return test::run("batch preflight, partial failures and retry", [] {
        ern::RelicValidator validator{synthetic_rules()};
        const ern::RuleContext context = ern::DlcState::Unknown;
        Fixture batch_fixture;
        const auto owner =
            std::get<ern::RelicSnapshot>(
                ern::read_equipped(batch_fixture.reader(), batch_fixture.roots, 0x2F4))
                .owner;
        auto batch_live =
            std::get<ern::RelicSnapshot>(
                ern::read_equipped(batch_fixture.reader(), batch_fixture.roots, 0x2F4))
                .relics;
        const auto make_drafts = [&]() {
            ern::Drafts drafts;
            for (std::size_t i = 0; i < 3; ++i) {
                drafts[i] = ern::Draft{*batch_live.slots[i], batch_live.slots[i]->effects};
                drafts[i]->effects.rows[0].effect = 7000400;
            }
            drafts[1]->effects.rows[1].effect = 7000601;
            return drafts;
        };
        auto drafts = make_drafts();
        const auto review = ern::review_drafts(drafts, &batch_live, validator, context);
        require(!review.blocked && review.relic_count == 3 && review.effect_count == 4,
                "Batch review omitted pending relics or effects");
        auto invalid_drafts = make_drafts();
        invalid_drafts[0]->effects.rows[0].effect = 10000;
        require(
            ern::review_drafts(invalid_drafts, &batch_live, validator, context).blocked.has_value(),
            "An invalid draft was not blocked in normal mode");
        const auto forced =
            ern::review_drafts(invalid_drafts, &batch_live, validator, context, true);
        require(!forced.blocked && forced.changes[0] &&
                    forced.changes[0]->effects == invalid_drafts[0]->effects,
                "Invalid mode did not stage every draft without validation");
        const auto batch_write = [&](std::size_t slot, const ern::Relic &before,
                                     ern::Effects after) {
            const auto result = ern::replace_equipped(
                batch_fixture.reader(),
                [&](ern::Address address, std::span<const std::byte> bytes) {
                    std::memcpy(batch_fixture.bytes.data() + address - Fixture::base, bytes.data(),
                                bytes.size());
                    return true;
                },
                batch_fixture.roots, 0x2F4, owner, slot, before, after, validator, context);
            return result;
        };
        // An invalid or stale slot blocks the entire batch, including other valid slots.
        unsigned int batch_calls = 0;
        const auto forbidden_batch_write = [&](std::size_t, const ern::Relic &before,
                                               ern::Effects) -> ern::EditResult {
            ++batch_calls;
            return before;
        };
        ern::Drafts reordered;
        reordered[0] = ern::Draft{*batch_live.slots[0], batch_live.slots[0]->effects};
        std::swap(reordered[0]->effects.rows[0].effect, reordered[0]->effects.rows[2].effect);
        const auto canonical = ern::review_drafts(reordered, &batch_live, validator, context);
        require(!canonical.blocked &&
                    ern::apply_drafts(reordered, canonical, forbidden_batch_write).applied == 0 &&
                    batch_calls == 0 && reordered[0]->effects == reordered[0]->original.effects,
                "Canonical ordering repeated a write or left a draft dirty");
        auto stale_live = batch_live;
        stale_live.slots[2]->handle = 99;
        auto blocked_batch = ern::review_drafts(drafts, &stale_live, validator, context);
        require((blocked_batch.blocked && blocked_batch.blocked->slot == 2) &&
                    ern::apply_drafts(drafts, blocked_batch, forbidden_batch_write)
                        .failure.has_value() &&
                    batch_calls == 0,
                "A stale unselected slot allowed part of the batch to be written");
        stale_live.slots[2].reset();
        require(ern::review_drafts(drafts, &stale_live, validator, context).blocked.has_value() &&
                    ern::review_drafts(drafts, nullptr, validator, context).blocked.has_value(),
                "A missing equipped relic allowed batch Apply");
        drafts[2]->effects.rows[0].effect = 10000;
        blocked_batch = ern::review_drafts(drafts, &batch_live, validator, context);
        require((blocked_batch.blocked && blocked_batch.blocked->slot == 2) &&
                    ern::apply_drafts(drafts, blocked_batch, forbidden_batch_write)
                        .failure.has_value() &&
                    batch_calls == 0,
                "An invalid unselected slot allowed part of the batch to be written");
        require(ern::review_drafts(make_drafts(), &batch_live, validator,
                                   ern::UnknownRules{"Fixture rules unavailable"})
                    .blocked.has_value(),
                "Unavailable rules allowed batch Apply");
        auto locked_live = batch_live;
        locked_live.slots[2]->item_id = 0x800007D0u;
        drafts = make_drafts();
        drafts[2]->original = *locked_live.slots[2];
        require(ern::review_drafts(drafts, &locked_live, validator, context).blocked->slot == 2,
                "A changed locked relic allowed batch Apply");
        drafts[2]->effects = drafts[2]->original.effects;
        require(!ern::review_drafts(drafts, &locked_live, validator, context).blocked,
                "An unchanged locked relic blocked other drafts");
        // Fail at each position. Keep completed slots clean and retry only unfinished drafts.
        for (const std::size_t failed_slot : {0, 1, 2}) {
            batch_fixture = Fixture{};
            drafts = make_drafts();
            auto pending = ern::review_drafts(drafts, &batch_live, validator, context);
            batch_calls = 0;
            const auto partial =
                ern::apply_drafts(drafts, pending,
                                  [&](std::size_t slot, const ern::Relic &before,
                                      ern::Effects after) -> ern::EditResult {
                                      ++batch_calls;
                                      if (slot == failed_slot)
                                          return ern::WriteFailure{ern::WriteStage::Write,
                                                                   ern::Restoration::Unconfirmed};
                                      return batch_write(slot, before, after);
                                  });
            require(partial.failure.has_value() && partial.applied == failed_slot &&
                        batch_calls == failed_slot + 1,
                    "Batch Apply continued after a write failure or misreported completed slots");
            const auto after_partial =
                std::get<ern::RelicSnapshot>(
                    ern::read_equipped(batch_fixture.reader(), batch_fixture.roots, 0x2F4))
                    .relics;
            for (std::size_t i = 0; i < 3; ++i)
                require(i < failed_slot ? drafts[i]->effects == drafts[i]->original.effects &&
                                              after_partial.slots[i]->effects == drafts[i]->effects
                                        : drafts[i]->effects != drafts[i]->original.effects &&
                                              after_partial.slots[i] == batch_live.slots[i],
                        "Batch failure lost pending changes or left applied drafts dirty");
            pending = ern::review_drafts(drafts, &after_partial, validator, context);
            const auto retried = ern::apply_drafts(drafts, pending, batch_write);
            require(!retried.failure && retried.applied == 3 - failed_slot,
                    "Retry did not apply all remaining relics");
            const auto after_batch =
                std::get<ern::RelicSnapshot>(
                    ern::read_equipped(batch_fixture.reader(), batch_fixture.roots, 0x2F4))
                    .relics;
            for (std::size_t i = 0; i < 3; ++i)
                require(after_batch.slots[i]->effects == review.changes[i]->effects &&
                            drafts[i]->effects == drafts[i]->original.effects,
                        "Batch Apply did not update and clean every changed relic");
            const auto clean = ern::review_drafts(drafts, &after_batch, validator, context);
            require(clean.relic_count == 0 &&
                        ern::apply_drafts(drafts, clean, forbidden_batch_write).applied == 0,
                    "Applying a clean batch repeated a write");
        }
    });
}
