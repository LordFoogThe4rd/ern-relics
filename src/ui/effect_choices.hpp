#pragma once

#include "data/asset_types.hpp"
#include "relics/relic_validator.hpp"
#include "ui/effect_groups.hpp"
#include <memory>
#include <optional>

namespace ern::ui {
struct EffectChoice {
    std::uint32_t id;
    std::string description;
    ChoiceValidation validation;
    std::string tag;
};

struct PreparedEffectChoices {
    bool deep = false;
    bool curses = false;
    bool can_clear = false;
    std::array<std::vector<EffectChoice>, effect_groups.size()> groups;
};

struct EffectChoiceInputs {
    std::shared_ptr<const RelicValidator> rules;
    std::shared_ptr<const GameAssets> resources;
    std::uint32_t item_id;
    Effects effects;
    EffectPosition position;
    RuleContext context;
    bool invalid = false;
    bool operator==(const EffectChoiceInputs &other) const;
};

// Prepare all choices independently of ImGui state, search text, and category
// selection.
PreparedEffectChoices prepare_effect_choices(const EffectChoiceInputs &inputs);

// One selected field is visible at a time. Retain immutable inputs across view
// publications.
class EffectChoiceCache {
  public:
    std::shared_ptr<const PreparedEffectChoices> get(EffectChoiceInputs inputs);

  private:
    std::optional<EffectChoiceInputs> inputs_;
    std::shared_ptr<const PreparedEffectChoices> choices_;
};
} // namespace ern::ui
