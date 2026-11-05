#include "relics/relic_validator.hpp"
#include <algorithm>
#include <set>

namespace ern {
const char *validation_label(const RelicValidation &result) {
    if (std::holds_alternative<Obtainable>(result))
        return "Obtainable";
    return std::holds_alternative<Impossible>(result) ? "Impossible" : "Unknown";
}
std::string validation_reason(const RelicValidation &result) {
    if (const auto *invalid = std::get_if<Impossible>(&result))
        return invalid->reason;
    if (const auto *unknown = std::get_if<UnknownRules>(&result))
        return unknown->reason;
    return {};
}

std::string validation_reason(const ChoiceValidation &result) {
    if (const auto *invalid = std::get_if<Impossible>(&result))
        return invalid->reason;
    if (const auto *unknown = std::get_if<UnknownRules>(&result))
        return unknown->reason;
    return {};
}

RelicValidator::RelicValidator(RegulationRules rules) {
    effects_.effects_ = std::move(rules.effects);
    items_.entries_ = std::move(rules.items);
    pools_ = std::move(rules.pools);
    std::set<std::pair<std::int32_t, std::int32_t>> deep_tables;
    for (const auto &[id, item] : items_.entries())
        if (item.kind == RelicKind::Rolled && item.family == RelicFamily::Deep)
            for (std::size_t i = 0; i < effects_per_relic; ++i)
                if (item.effect_tables[i] != -1)
                    deep_tables.emplace(item.effect_tables[i], item.curse_tables[i]);
    deep_tables_.assign(deep_tables.begin(), deep_tables.end());
}

bool RelicValidator::permits(std::int32_t table, std::uint32_t id, bool dlc) const {
    if (table == -1)
        return id == UINT32_MAX;
    const auto &pool = pools_.at(table);
    const auto found = pool.find(id);
    return found != pool.end() && (dlc ? found->second.dlc : found->second.base);
}

bool RelicValidator::pair_rollable(std::uint32_t effect, std::uint32_t curse, bool dlc) const {
    return std::any_of(deep_tables_.begin(), deep_tables_.end(), [&](const auto &tables) {
        return permits(tables.first, effect, dlc) && permits(tables.second, curse, dlc);
    });
}

PairValidation RelicValidator::validate_pair(std::uint32_t effect, std::uint32_t curse,
                                             const RuleContext &context) const {
    if (const auto *unknown = std::get_if<UnknownRules>(&context))
        return *unknown;
    if (!effects_.find(effect) || (curse != UINT32_MAX && !effects_.find(curse)))
        return UnknownRules{"An effect is missing from the installed game's catalogue."};
    for (const auto &[positive, negative] : deep_tables_)
        if (!pools_.contains(positive) || (negative != -1 && !pools_.contains(negative)))
            return UnknownRules{"A Deep Relic roll table is missing."};
    const auto dlc = std::get<DlcState>(context);
    const bool base = dlc != DlcState::Present && pair_rollable(effect, curse, false);
    const bool expansion = dlc != DlcState::Absent && pair_rollable(effect, curse, true);
    if (dlc == DlcState::Unknown && base != expansion)
        return UnknownRules{"This pair depends on DLC ownership, which could not be detected."};
    if (!base && !expansion)
        return Impossible{curse == UINT32_MAX
                              ? "This effect requires a paired curse."
                              : "This curse needs a compatible effect in the same row."};
    return CompatiblePair{};
}

ChoiceValidation RelicValidator::validate_choice(std::uint32_t item_id, Effects effects,
                                                 EffectPosition position,
                                                 const RuleContext &context) const {
    if (const auto *unknown = std::get_if<UnknownRules>(&context))
        return *unknown;
    const auto *item = items_.find(item_id);
    if (const auto *reason = relic_lock_reason(item))
        return UnknownRules{reason};
    if (position.row >= effects_per_relic)
        return Impossible{"This effect position is unavailable."};
    const bool curses = position.field == EffectField::Curse;
    const auto row = position.row;
    const auto count = static_cast<std::size_t>(
        std::count_if(item->effect_tables.begin(), item->effect_tables.end(),
                      [](auto table) { return table != -1; }));
    if (row >= count || (curses && item->family != RelicFamily::Deep))
        return Impossible{"This effect position is unavailable."};
    for (std::size_t i = 0; i < effects_per_relic; ++i) {
        if (i > row)
            effects.rows[i].effect = UINT32_MAX;
        if (i > row || (i == row && !curses))
            effects.rows[i].curse = UINT32_MAX;
        if (i <= row && effects.rows[i].effect == UINT32_MAX)
            return Impossible{"Choose the earlier effects first."};
    }
    // Check only the retained prefix, including its curses and cross-field exclusions.
    const auto conflicts = [&](const Effects &selected) {
        const auto pair_conflicts = [&](std::uint32_t left_id, std::uint32_t right_id) {
            if (left_id == UINT32_MAX || right_id == UINT32_MAX)
                return false;
            const auto *left = effects_.find(left_id);
            const auto *right = effects_.find(right_id);
            return left_id == right_id ||
                   (left->compatibility >= 0 && left->compatibility == right->compatibility) ||
                   (left->exclusivity >= 0 && left->exclusivity == right->exclusivity);
        };
        for (std::size_t i = 0; i < selected.rows.size(); ++i) {
            const auto &left = selected.rows[i];
            if (pair_conflicts(left.effect, left.curse))
                return true;
            for (std::size_t j = 0; j < i; ++j) {
                const auto &right = selected.rows[j];
                for (const auto left_id : {left.effect, left.curse})
                    for (const auto right_id : {right.effect, right.curse})
                        if (pair_conflicts(left_id, right_id))
                            return true;
            }
        }
        return false;
    };
    for (const auto &pair : effects.rows)
        for (const auto id : {pair.effect, pair.curse})
            if (id != UINT32_MAX && !effects_.find(id))
                return UnknownRules{"An effect is missing from the installed game's catalogue."};
    if (conflicts(effects))
        return Impossible{"This choice conflicts with an earlier effect or curse."};
    for (const auto table : item->effect_tables)
        if (table != -1 && !pools_.contains(table))
            return UnknownRules{"This relic's roll table is missing."};
    if (item->family == RelicFamily::Deep)
        for (const auto &[positive, negative] : deep_tables_)
            if (!pools_.contains(positive) || (negative != -1 && !pools_.contains(negative)))
                return UnknownRules{"A Deep Relic roll table is missing."};
    const auto possible = [&](bool dlc) {
        if (item->family != RelicFamily::Deep) {
            std::array<std::size_t, effects_per_relic> tables{0, 1, 2};
            do {
                bool fits = true;
                for (std::size_t i = 0; i <= row; ++i)
                    fits = fits &&
                           permits(item->effect_tables[tables[i]], effects.rows[i].effect, dlc);
                if (fits)
                    return true;
            } while (std::next_permutation(tables.begin(), tables.end()));
            return false;
        }
        for (std::size_t i = 0; i < row + (curses ? 1u : 0u); ++i) {
            // An earlier missing curse is unfinished input, not a constraint on later choices.
            if (i < row && effects.rows[i].curse == UINT32_MAX) {
                if (std::none_of(deep_tables_.begin(), deep_tables_.end(), [&](const auto &tables) {
                        return permits(tables.first, effects.rows[i].effect, dlc);
                    }))
                    return false;
            } else if (!pair_rollable(effects.rows[i].effect, effects.rows[i].curse, dlc))
                return false;
        }
        if (curses || pair_rollable(effects.rows[row].effect, UINT32_MAX, dlc))
            return true;
        // A positive effect can be chosen before its curse, provided a legal curse exists.
        for (const auto &[positive, negative] : deep_tables_) {
            if (negative == -1 || !permits(positive, effects.rows[row].effect, dlc))
                continue;
            for (const auto &[curse, weight] : pools_.at(negative)) {
                if (!(dlc ? weight.dlc : weight.base))
                    continue;
                if (curse != UINT32_MAX && !effects_.find(curse))
                    continue;
                auto paired = effects;
                paired.rows[row].curse = curse;
                if (!conflicts(paired))
                    return true;
            }
        }
        return false;
    };
    const auto dlc = std::get<DlcState>(context);
    const bool base = dlc != DlcState::Present && possible(false);
    const bool expansion = dlc != DlcState::Absent && possible(true);
    if (dlc == DlcState::Unknown && base != expansion)
        return UnknownRules{"This choice depends on DLC ownership, which could not be detected."};
    if (!base && !expansion)
        return Impossible{"This choice cannot roll with the earlier effect and curse pairs."};
    return AvailableChoice{};
}

bool RelicValidator::rollable(const RelicInfo &item, Effects effects, bool dlc) const {
    if (item.family == RelicFamily::Deep) {
        for (std::size_t i = 0; i < effects_per_relic; ++i) {
            if (effects.rows[i].effect == UINT32_MAX) {
                if (effects.rows[i].curse != UINT32_MAX)
                    return false;
                continue;
            }
            if (!pair_rollable(effects.rows[i].effect, effects.rows[i].curse, dlc))
                return false;
        }
        return true;
    }
    if (std::any_of(effects.rows.begin(), effects.rows.end(),
                    [](const auto &row) { return row.curse != UINT32_MAX; }))
        return false;
    // Stored effects are sorted by the game. Their original lottery order is lost.
    // A roll is possible if ANY assignment to the item's three tables has positive weight.
    std::array<std::uint32_t, effects_per_relic> positives;
    std::transform(effects.rows.begin(), effects.rows.end(), positives.begin(),
                   [](const auto &row) { return row.effect; });
    std::sort(positives.begin(), positives.end());
    do {
        bool matches = true;
        for (std::size_t slot = 0; slot < effects_per_relic; ++slot) {
            if (!permits(item.effect_tables[slot], positives[slot], dlc)) {
                matches = false;
                break;
            }
        }
        if (matches)
            return true;
    } while (std::next_permutation(positives.begin(), positives.end()));
    return false;
}

RelicValidation RelicValidator::validate(std::uint32_t item_id, Effects effects,
                                         const RuleContext &context) const {
    if (const auto *unknown = std::get_if<UnknownRules>(&context))
        return *unknown;
    const auto *item = items_.find(item_id);
    if (const auto *reason = relic_lock_reason(item))
        return UnknownRules{reason};
    for (const auto table : item->effect_tables)
        if (table != -1 && !pools_.contains(table))
            return UnknownRules{"This relic's roll table is missing."};
    if (item->family == RelicFamily::Deep)
        for (const auto &[positive, curse] : deep_tables_)
            if (!pools_.contains(positive) || (curse != -1 && !pools_.contains(curse)))
                return UnknownRules{"A Deep Relic roll table is missing."};
    const auto count = std::count_if(effects.rows.begin(), effects.rows.end(),
                                     [](const auto &row) { return row.effect != UINT32_MAX; });
    const auto required = std::count_if(item->effect_tables.begin(), item->effect_tables.end(),
                                        [](auto table) { return table != -1; });
    if (count != required)
        return Impossible{"This relic requires " + std::to_string(required) + " effect(s)."};
    std::set<std::uint32_t> seen;
    std::set<int> compatibility;
    std::set<int> exclusivity;
    for (const auto field : {EffectField::Effect, EffectField::Curse})
        for (const auto &pair : effects.rows) {
            const auto id = field == EffectField::Effect ? pair.effect : pair.curse;
            if (id == UINT32_MAX)
                continue;
            const auto *effect = effects_.find(id);
            if (!effect)
                return UnknownRules{"An effect is missing from the installed game's catalogue."};
            if (!seen.insert(id).second)
                return Impossible{"The same effect cannot appear twice."};
            if (effect->compatibility >= 0 && !compatibility.insert(effect->compatibility).second)
                return Impossible{
                    "These effects share an exclusion group and cannot roll together."};
            if (effect->exclusivity >= 0 && !exclusivity.insert(effect->exclusivity).second)
                return Impossible{
                    "These effects share an exclusivity group and cannot roll together."};
        }
    const auto dlc = std::get<DlcState>(context);
    const auto base_possible = dlc != DlcState::Present && rollable(*item, effects, false);
    const auto dlc_possible = dlc != DlcState::Absent && rollable(*item, effects, true);
    if (dlc == DlcState::Unknown && base_possible != dlc_possible)
        return UnknownRules{
            "This combination depends on DLC ownership, which could not be detected."};
    if (!base_possible && !dlc_possible)
        return Impossible{
            item->family == RelicFamily::Deep
                ? "These effects and curses cannot roll together. Choose a compatible effect and curse in the same row."
                : "No roll on this relic's tables can produce this combination with positive weights."};
    std::sort(effects.rows.begin(), effects.rows.end(),
              [this](const auto &left_row, const auto &right_row) {
                  const auto left = left_row.effect, right = right_row.effect;
                  if (left == UINT32_MAX)
                      return false;
                  if (right == UINT32_MAX)
                      return true;
                  const auto *a = effects_.find(left);
                  const auto *b = effects_.find(right);
                  return a->order == b->order ? left < right : a->order < b->order;
              });
    return Obtainable{effects};
}

std::vector<const Effect *> RelicValidator::candidates(std::uint32_t item_id, bool curses) const {
    std::vector<const Effect *> result;
    const auto *item = items_.find(item_id);
    if (relic_lock_reason(item))
        return result;
    std::set<std::uint32_t> seen;
    std::set<std::int32_t> tables;
    if (item->family == RelicFamily::Deep) {
        for (const auto &[positive, curse] : deep_tables_)
            tables.insert(curses ? curse : positive);
    } else if (!curses)
        tables.insert(item->effect_tables.begin(), item->effect_tables.end());
    for (const auto table : tables) {
        const auto pool = pools_.find(table);
        if (pool == pools_.end())
            continue;
        for (const auto &[id, weight] : pool->second) {
            const auto *effect = effects_.find(id);
            if (id != UINT32_MAX && effect && (weight.base || weight.dlc) && seen.insert(id).second)
                result.push_back(effect);
        }
    }
    return result;
}
} // namespace ern
