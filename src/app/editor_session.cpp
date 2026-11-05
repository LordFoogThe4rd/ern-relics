#include "app/editor_session.hpp"

namespace ern {
void EditorSession::sync(const EquippedRelics &equipped) {
    for (std::size_t i = 0; i < drafts_.size(); ++i) {
        // Keep every changed draft, including one whose equipped slot became empty.
        if (drafts_[i] && drafts_[i]->effects != drafts_[i]->original.effects)
            continue;
        reload(i, equipped);
    }
}

void EditorSession::reload(std::size_t slot, const EquippedRelics &equipped) {
    drafts_[slot] =
        equipped.slots[slot]
            ? std::optional<Draft>{Draft{*equipped.slots[slot], equipped.slots[slot]->effects}}
            : std::nullopt;
}

void EditorSession::set_effect(std::size_t slot, EffectPosition effect, std::uint32_t id) {
    effect_at(drafts_[slot]->effects, effect) = id;
}

void EditorSession::discard() {
    for (auto &draft : drafts_)
        if (draft)
            draft->effects = draft->original.effects;
}

DraftReview EditorSession::review(const EquippedRelics *equipped, const RelicValidator &rules,
                                  const RuleContext &context) const {
    return review_drafts(drafts_, equipped, rules, context);
}

DraftApplyResult EditorSession::apply(const EquippedRelics *equipped, const RelicValidator &rules,
                                      const RuleContext &context, const DraftWriter &write) {
    return apply_drafts(drafts_, review(equipped, rules, context), write);
}

PresetStage EditorSession::load_preset(const Preset &preset, const EquippedRelics &equipped,
                                       const RelicValidator &rules, const RuleContext &context) {
    auto staged = stage_preset(preset, equipped, rules, context);
    for (std::size_t i = 0; i < drafts_.size(); ++i)
        if (staged.drafts.slots[i])
            drafts_[i] = Draft{*equipped.slots[i], *staged.drafts.slots[i]};
    return staged;
}
} // namespace ern
