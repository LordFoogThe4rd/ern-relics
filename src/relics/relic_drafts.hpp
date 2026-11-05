#pragma once

#include "relics/types.hpp"
#include "relics/relic_validator.hpp"
#include "relics/edit_result.hpp"
#include <functional>

namespace ern {
struct Draft {
    Relic original;
    Effects effects;
    bool operator==(const Draft &) const = default;
};
using Drafts = std::array<std::optional<Draft>, relic_slot_count>;
std::size_t changed_effect_count(const Draft &draft);

struct DraftReview {
    Drafts changes;
    std::size_t relic_count = 0;
    std::size_t effect_count = 0;
    std::optional<SlotRejection> blocked;
};

DraftReview review_drafts(const Drafts &drafts, const EquippedRelics *equipped,
                          const RelicValidator &validator, const RuleContext &context);

struct DraftApplyResult {
    std::size_t applied = 0;
    std::optional<SlotFailure> failure;
};
using DraftWriter = std::function<EditResult(std::size_t, const Relic &, Effects)>;
// Stop on the first failure. Successful slots become clean; remaining drafts survive.
DraftApplyResult apply_drafts(Drafts &drafts, const DraftReview &review, const DraftWriter &write);
} // namespace ern
