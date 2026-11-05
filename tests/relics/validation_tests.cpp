#include "support/check.hpp"
#include "support/synthetic_rules.hpp"
#include "relics/relic_validator.hpp"

#include <array>
#include <string>
#include <utility>

using test::require;

int main() {
    return test::run("catalogues, locks and effect validation", [] {
        ern::RelicValidator validator{synthetic_rules()};
        const auto &catalog = validator.effects();
        const auto &items = validator.items();
        const ern::RuleContext context = ern::DlcState::Unknown;
        require(items.size() == 11, "Synthetic relic definitions did not load");
        require(items.find(0x800F4AE4)->kind == ern::RelicKind::Rolled,
                "Live item ID did not resolve the real name");
        require(items.find(0x80002710)->kind == ern::RelicKind::Unique,
                "Unique item name is wrong");
        require(items.find(0x80002710)->fixed_effects == ern::Effects{{10000, UINT32_MAX},
                                                                      {7000402, UINT32_MAX},
                                                                      {UINT32_MAX, UINT32_MAX}},
                "Unique definition effects are wrong");
        require(ern::relic_lock_reason(items.find(0x80002710)) != nullptr,
                "Old Pocketwatch is not locked");
        require(ern::relic_lock_reason(items.find(0x800007D0)) != nullptr,
                "Night of the Beast is not locked");
        require(!ern::relic_lock_reason(items.find(0x800F4AE4)), "Rolled relic is locked");
        require(ern::relic_lock_reason(items.find(0x8FFFFFFF)) != nullptr && !items.find(100),
                "Unknown or wrong-category item is editable");
        require(catalog.size() == 15, "Synthetic effect catalogue did not load");
        ern::Effects edited{{7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        require(std::holds_alternative<ern::Obtainable>(
                    validator.validate(0x80000066, edited, context)),
                "Valid normal edit rejected");
        auto sparse_rules = synthetic_rules();
        sparse_rules.items.at(102).effect_tables = {-1, 20, -1};
        const ern::RelicValidator sparse_validator{std::move(sparse_rules)};
        require(std::holds_alternative<ern::Obtainable>(sparse_validator.validate(
                    0x80000066,
                    {{7000602, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
                    context)),
                "A valid normal effect was not permuted past an absent roll table");
        ern::Effects conflict{{7000400, UINT32_MAX}, {7000402, UINT32_MAX}, {7001802, UINT32_MAX}};
        require(std::holds_alternative<ern::Impossible>(
                    validator.validate(0x80000066, conflict, context)),
                "Exclusive effect category conflict accepted");
        conflict = {{7000400, UINT32_MAX}, {7000400, UINT32_MAX}, {7001802, UINT32_MAX}};
        require(std::holds_alternative<ern::Impossible>(
                    validator.validate(0x80000066, conflict, context)),
                "Duplicate effects accepted");
        conflict = {{7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}};
        require(std::holds_alternative<ern::Impossible>(
                    validator.validate(0x80000066, conflict, context)),
                "Relic effect count changed");
        // Editing proceeds from the first row. Later values never constrain an earlier choice.
        const ern::Effects prefix{{7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001801, 900001}};
        for (const auto field : {ern::EffectField::Effect, ern::EffectField::Curse})
            require(std::holds_alternative<ern::Impossible>(validator.validate_choice(
                        0x801E8480, prefix, {ern::effects_per_relic, field}, context)),
                    "An out-of-range typed effect position reached row storage");
        auto proposed = prefix;
        proposed.rows[0].effect = 7000601;
        proposed.rows[2].effect = 0xFFFFFF00;
        require(std::holds_alternative<ern::AvailableChoice>(validator.validate_choice(
                    0x80000066, proposed, {0, ern::EffectField::Effect}, context)),
                "Later values constrained the first effect choice");
        proposed = prefix;
        proposed.rows[2].effect = 7000402;
        require(std::holds_alternative<ern::Impossible>(validator.validate_choice(
                    0x80000066, proposed, {2, ern::EffectField::Effect}, context)),
                "A later choice bypassed an earlier exclusion group");
        require(std::holds_alternative<ern::AvailableChoice>(validator.validate_choice(
                    0x801E8480, prefix, {2, ern::EffectField::Effect}, context)),
                "A curse-required positive effect could not be picked before its curse");
        proposed = prefix;
        proposed.rows[0].curse = 900001;
        require(std::holds_alternative<ern::Impossible>(validator.validate_choice(
                    0x801E8480, proposed, {0, ern::EffectField::Curse}, context)),
                "An incompatible curse was offered for the selected effect");
        proposed = prefix;
        proposed.rows[0].effect = 7001801;
        proposed.rows[0].curse = UINT32_MAX;
        require(std::holds_alternative<ern::AvailableChoice>(validator.validate_choice(
                    0x801E8480, proposed, {1, ern::EffectField::Effect}, context)),
                "An earlier missing curse hid later effect choices");
        require(std::holds_alternative<ern::Impossible>(
                    validator.validate(0x801E8480, proposed, context)),
                "An incomplete effect/curse pair bypassed Apply validation");
        auto choice_rules = synthetic_rules();
        choice_rules.effects.at(7000400).compatibility = -1;
        choice_rules.effects.at(7000402).compatibility = -1;
        ern::RelicValidator lottery{choice_rules};
        proposed = prefix;
        proposed.rows[1].effect = 7000402;
        require(std::holds_alternative<ern::Impossible>(lottery.validate_choice(
                    0x80000066, proposed, {1, ern::EffectField::Effect}, context)),
                "Two prefix effects were assigned to the same lottery table");
        choice_rules.pools.at(3000000).at(900001) = {false, true};
        lottery = ern::RelicValidator{choice_rules};
        require(
            std::holds_alternative<ern::Impossible>(lottery.validate_choice(
                0x801E8480, prefix, {2, ern::EffectField::Curse}, ern::DlcState::Absent)) &&
                std::holds_alternative<ern::AvailableChoice>(lottery.validate_choice(
                    0x801E8480, prefix, {2, ern::EffectField::Curse}, ern::DlcState::Present)) &&
                std::holds_alternative<ern::UnknownRules>(lottery.validate_choice(
                    0x801E8480, prefix, {2, ern::EffectField::Curse}, context)),
            "Sequential choice filtering bypassed DLC ownership");
        choice_rules.effects.at(900001).exclusivity = 20;
        choice_rules.effects.at(7000400).exclusivity = 20;
        require(std::holds_alternative<ern::Impossible>(
                    ern::RelicValidator{choice_rules}.validate_choice(
                        0x801E8480, prefix, {2, ern::EffectField::Curse}, ern::DlcState::Present)),
                "A curse choice ignored an earlier positive effect's exclusivity group");
        choice_rules.pools.erase(3000000);
        require(std::holds_alternative<ern::UnknownRules>(
                    ern::RelicValidator{choice_rules}.validate_choice(
                        0x801E8480, prefix, {2, ern::EffectField::Effect}, context)),
                "Missing roll rules produced an available choice");
        conflict = {{8100000, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        require(std::holds_alternative<ern::Impossible>(
                    validator.validate(0x80000066, conflict, context)),
                "Non-pool effect accepted");
    });
}
