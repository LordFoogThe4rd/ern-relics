#include "ui/effect_choices.hpp"
#include "data/catalog_presentation.hpp"

namespace ern::ui {
bool EffectChoiceInputs::operator==(const EffectChoiceInputs &other) const {
    if (rules != other.rules || resources != other.resources || item_id != other.item_id ||
        effects != other.effects || position != other.position ||
        context.index() != other.context.index() || invalid != other.invalid)
        return false;
    if (const auto *dlc = std::get_if<DlcState>(&context))
        return *dlc == std::get<DlcState>(other.context);
    return std::get<UnknownRules>(context).reason == std::get<UnknownRules>(other.context).reason;
}

PreparedEffectChoices prepare_effect_choices(const EffectChoiceInputs &inputs) {
    PreparedEffectChoices result;
    const auto &rules = *inputs.rules;
    const auto *item = rules.items().find(inputs.item_id);
    result.deep = item && item->family == RelicFamily::Deep;
    result.curses = inputs.position.field == EffectField::Curse;
    if (result.curses && (result.deep || inputs.invalid)) {
        auto cleared = inputs.effects;
        effect_at(cleared, inputs.position) = UINT32_MAX;
        result.can_clear =
            inputs.invalid || std::holds_alternative<AvailableChoice>(rules.validate_choice(
                                  inputs.item_id, cleared, inputs.position, inputs.context));
    }
    const CatalogPresentation presentation{rules, *inputs.resources};
    for (auto &candidate : presentation.candidates(inputs.item_id, result.curses, inputs.invalid)) {
        auto proposed = inputs.effects;
        effect_at(proposed, inputs.position) = candidate.effect->id;
        auto validation = inputs.invalid ? ChoiceValidation{AvailableChoice{}}
                                         : rules.validate_choice(inputs.item_id, proposed,
                                                                 inputs.position, inputs.context);
        const bool available = std::holds_alternative<AvailableChoice>(validation);
        std::string tag;
        if (!inputs.invalid && available && result.deep && !result.curses &&
            std::holds_alternative<Impossible>(
                rules.validate_pair(candidate.effect->id, UINT32_MAX, inputs.context)))
            tag = "Curse required";
        else if (!available)
            tag = std::holds_alternative<UnknownRules>(validation) ? "Rules unavailable"
                                                                   : "Unavailable";
        const auto group = effect_group(candidate.description);
        result.groups[static_cast<std::size_t>(group)].push_back(
            {candidate.effect->id, std::move(candidate.description), std::move(validation),
             std::move(tag)});
    }
    return result;
}

std::shared_ptr<const PreparedEffectChoices> EffectChoiceCache::get(EffectChoiceInputs inputs) {
    if (!inputs_ || *inputs_ != inputs) {
        auto choices =
            std::make_shared<const PreparedEffectChoices>(prepare_effect_choices(inputs));
        inputs_ = std::move(inputs);
        choices_ = std::move(choices);
    }
    return choices_;
}
} // namespace ern::ui
