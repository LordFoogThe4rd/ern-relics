#pragma once

#include "relics/preset.hpp"
#include "relics/relic_drafts.hpp"

namespace ern {
class EditorSession {
  public:
    const Drafts &drafts() const { return drafts_; }
    void sync(const EquippedRelics &equipped);
    void reload(std::size_t slot, const EquippedRelics &equipped);
    void set_effect(std::size_t slot, EffectPosition effect, std::uint32_t id);
    void discard();
    DraftReview review(const EquippedRelics *equipped, const RelicValidator &rules,
                       const RuleContext &context) const;
    DraftApplyResult apply(const EquippedRelics *equipped, const RelicValidator &rules,
                           const RuleContext &context, const DraftWriter &write);
    PresetStage load_preset(const Preset &preset, const EquippedRelics &equipped,
                            const RelicValidator &rules, const RuleContext &context);

  private:
    Drafts drafts_;
};
} // namespace ern
