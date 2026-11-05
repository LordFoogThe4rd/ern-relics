#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace ern {
// Presentation categories only. Roll-table compatibility still belongs to the validator.
enum class EffectGroup {
    Attributes,
    Attack,
    Recovery,
    Defense,
    Discovery,
    Skills,
    Character,
    Other
};
inline constexpr std::array effect_groups{
    EffectGroup::Attributes, EffectGroup::Attack, EffectGroup::Recovery,  EffectGroup::Defense,
    EffectGroup::Discovery,  EffectGroup::Skills, EffectGroup::Character, EffectGroup::Other};

inline const char *effect_group_name(EffectGroup group) {
    switch (group) {
    case EffectGroup::Attributes:
        return "Attributes";
    case EffectGroup::Attack:
        return "Attack";
    case EffectGroup::Recovery:
        return "Recovery";
    case EffectGroup::Defense:
        return "Defense";
    case EffectGroup::Discovery:
        return "Discovery";
    case EffectGroup::Skills:
        return "Skills & spells";
    case EffectGroup::Character:
        return "Character";
    case EffectGroup::Other:
        return "Other";
    }
    return "Other";
}

inline EffectGroup effect_group(std::string_view description) {
    if (description.starts_with('['))
        return EffectGroup::Character;
    std::string text(description);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const auto has = [&text](std::string_view word) {
        return text.find(word) != std::string::npos;
    };
    for (const auto stat : {"vigor +", "mind +", "endurance +", "strength +", "dexterity +",
                            "intelligence +", "faith +", "arcane +"})
        if (text.starts_with(stat))
            return EffectGroup::Attributes;
    if (has("discover") || has("rune acquisition") || has("earn runes"))
        return EffectGroup::Discovery;
    if (has("character skill") || has("ultimate art") || has("art gauge"))
        return EffectGroup::Character;
    if (has("incantation") || has("sorcer") || has("spell") || has("changes compatible armament") ||
        has("skill attack"))
        return EffectGroup::Skills;
    if (has("restor") || has("recover") || has("heal") || has("flask"))
        return EffectGroup::Recovery;
    if (has("resistance") || has("damage negation") || has("damage reduction") ||
        has("damage taken") || has("guarding") || has("maximum hp") || has("max hp") ||
        has("max fp") || has("max stamina"))
        return EffectGroup::Defense;
    if (has("attack") || has("damage") || has("critical hit") || has("status ailment") ||
        has("blood loss") || has("frostbite") || has("poison"))
        return EffectGroup::Attack;
    return EffectGroup::Other;
}
} // namespace ern
