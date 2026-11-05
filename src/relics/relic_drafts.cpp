#include "relics/relic_drafts.hpp"

namespace ern {
std::size_t changed_effect_count(const Draft &draft) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < draft.effects.rows.size(); ++i) {
        count += draft.effects.rows[i].effect != draft.original.effects.rows[i].effect;
        count += draft.effects.rows[i].curse != draft.original.effects.rows[i].curse;
    }
    return count;
}

DraftReview review_drafts(const Drafts &drafts, const EquippedRelics *equipped,
                          const RelicValidator &validator, const RuleContext &context) {
    DraftReview review;
    for (std::size_t i = 0; i < drafts.size(); ++i) {
        if (!drafts[i] || drafts[i]->effects == drafts[i]->original.effects)
            continue;
        const auto &draft = *drafts[i];
        ++review.relic_count;
        review.effect_count += changed_effect_count(draft);
        std::optional<EditRejected> rejection;
        if (!equipped || !equipped->slots[i] || *equipped->slots[i] != draft.original)
            rejection = EditRejected{EditRejectReason::Stale, {}};
        else if (const auto *reason =
                     relic_lock_reason(validator.items().find(draft.original.item_id)))
            rejection = EditRejected{EditRejectReason::Locked, reason};
        else {
            const auto validation =
                validator.validate(draft.original.item_id, draft.effects, context);
            if (const auto *valid = std::get_if<Obtainable>(&validation))
                review.changes[i] = Draft{draft.original, valid->effects};
            else
                rejection = EditRejected{std::holds_alternative<UnknownRules>(validation)
                                             ? EditRejectReason::RulesUnavailable
                                             : EditRejectReason::Invalid,
                                         validation_reason(validation)};
        }
        if (rejection && !review.blocked)
            review.blocked = SlotRejection{i, std::move(*rejection)};
    }
    return review;
}

DraftApplyResult apply_drafts(Drafts &drafts, const DraftReview &review, const DraftWriter &write) {
    DraftApplyResult result;
    if (review.blocked) {
        result.failure = SlotFailure{review.blocked->slot, review.blocked->rejection};
        return result;
    }
    for (std::size_t i = 0; i < review.changes.size(); ++i) {
        if (!review.changes[i])
            continue;
        const auto &change = *review.changes[i];
        if (change.effects == change.original.effects) {
            drafts[i] = Draft{change.original, change.original.effects};
            continue;
        }
        const auto outcome = write(i, change.original, change.effects);
        if (const auto *after = std::get_if<Relic>(&outcome)) {
            ++result.applied;
            drafts[i] = Draft{*after, after->effects};
        } else {
            result.failure = std::holds_alternative<EditRejected>(outcome)
                                 ? SlotFailure{i, std::get<EditRejected>(outcome)}
                                 : SlotFailure{i, std::get<WriteFailure>(outcome)};
            return result;
        }
    }
    return result;
}
} // namespace ern
