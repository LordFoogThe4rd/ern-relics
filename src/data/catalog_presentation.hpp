#pragma once

#include "data/asset_types.hpp"
#include "relics/relic_validator.hpp"

namespace ern {
std::vector<IconKey> requested_icons(const RelicValidator &rules);
struct PresentedEffect {
    const Effect *effect;
    std::string description;
};

// A view of optional display data. It never changes the validation rules.
class CatalogPresentation {
  public:
    CatalogPresentation(const RelicValidator &rules, const GameAssets &assets)
        : rules_(rules), assets_(assets) {}
    std::string name(const RelicInfo *item) const;
    std::string describe(std::uint32_t id) const;
    std::optional<unsigned int> item_cell(const RelicInfo *item) const;
    std::optional<unsigned int> empty_relic_cell(bool deep) const;
    std::optional<unsigned int> effect_cell(std::uint32_t id) const;
    std::vector<PresentedEffect> candidates(std::uint32_t item_id, bool curses = false) const;

  private:
    std::optional<unsigned int> cell(IconKey key) const;
    const RelicValidator &rules_;
    const GameAssets &assets_;
};
} // namespace ern
