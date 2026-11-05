#pragma once
#include "relics/effect_catalog.hpp"
#include "relics/relic_catalog.hpp"
#include "relics/rules.hpp"
#include "relics/types.hpp"
#include <variant>
#include <vector>
#include <string>

namespace ern {
enum class DlcState { Unknown, Absent, Present };
struct UnknownRules {
    std::string reason;
};
using RuleContext = std::variant<DlcState, UnknownRules>;
struct Obtainable {
    Effects effects;
};
struct Impossible {
    std::string reason;
};
using RelicValidation = std::variant<Obtainable, Impossible, UnknownRules>;
struct CompatiblePair {};
using PairValidation = std::variant<CompatiblePair, Impossible, UnknownRules>;
struct AvailableChoice {};
using ChoiceValidation = std::variant<AvailableChoice, Impossible, UnknownRules>;
const char *validation_label(const RelicValidation &result);
std::string validation_reason(const RelicValidation &result);
std::string validation_reason(const ChoiceValidation &result);

class RelicValidator {
  public:
    RelicValidator() = default;
    explicit RelicValidator(RegulationRules rules);
    const EffectCatalog &effects() const { return effects_; }
    const RelicCatalog &items() const { return items_; }
    RelicValidation validate(std::uint32_t item_id, Effects effects,
                             const RuleContext &context) const;
    std::vector<const Effect *> candidates(std::uint32_t item_id, bool curses = false) const;
    // Tests the shared Deep roll tables. Whole-draft exclusions still require validate().
    PairValidation validate_pair(std::uint32_t effect, std::uint32_t curse,
                                 const RuleContext &context) const;
    // Earlier pairs constrain a choice. Current/later values remain free to be replaced.
    ChoiceValidation validate_choice(std::uint32_t item_id, Effects effects,
                                     EffectPosition position, const RuleContext &context) const;

  private:
    bool rollable(const RelicInfo &item, Effects effects, bool dlc) const;
    bool permits(std::int32_t table, std::uint32_t id, bool dlc) const;
    bool pair_rollable(std::uint32_t effect, std::uint32_t curse, bool dlc) const;
    EffectCatalog effects_;
    RelicCatalog items_;
    std::unordered_map<std::int32_t, RollPool> pools_;
    // Deep item IDs retain effect count, but not the original lottery template.
    std::vector<std::pair<std::int32_t, std::int32_t>> deep_tables_;
};
} // namespace ern
