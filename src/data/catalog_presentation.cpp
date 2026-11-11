#include "data/catalog_presentation.hpp"
#include <algorithm>
#include <set>

namespace ern {
namespace {
// Scenic Flatstone and Deep Scenic Flatstone, read from the local menu atlas.
constexpr IconKey empty_relic_art{IconKind::Item, 19200};
constexpr IconKey empty_deep_relic_art{IconKind::Item, 19201};
} // namespace

std::vector<IconKey> requested_icons(const RelicValidator &rules) {
    std::set<IconKey> icons{empty_relic_art, empty_deep_relic_art};
    for (const auto &[id, item] : rules.items().entries())
        icons.insert({IconKind::Item, item.icon});
    for (const auto &[id, effect] : rules.effects().entries())
        if (effect.icon >= 0)
            icons.insert({IconKind::Status, static_cast<std::uint32_t>(effect.icon)});
    return {icons.begin(), icons.end()};
}

std::string CatalogPresentation::name(const RelicInfo *item) const {
    if (!item)
        return "Unrecognized relic";
    const auto found = assets_.item_names.find(item->id);
    return found != assets_.item_names.end() && !found->second.empty()
               ? found->second
               : "Relic ID " + std::to_string(item->id);
}

std::string CatalogPresentation::describe(std::uint32_t id) const {
    if (id == UINT32_MAX)
        return "Empty effect slot";
    const auto *effect = rules_.effects().find(id);
    if (!effect)
        return "Unknown effect. ID " + std::to_string(id);
    const auto found = assets_.effect_names.find(effect->text_id);
    return found != assets_.effect_names.end() && !found->second.empty()
               ? found->second
               : "Effect ID " + std::to_string(id);
}

std::optional<unsigned int> CatalogPresentation::cell(IconKey key) const {
    const auto found = assets_.cells.find(key);
    return found == assets_.cells.end() ? std::nullopt : std::optional{found->second};
}

std::optional<unsigned int> CatalogPresentation::item_cell(const RelicInfo *item) const {
    return item ? cell({IconKind::Item, item->icon}) : std::nullopt;
}

std::optional<unsigned int> CatalogPresentation::empty_relic_cell(bool deep) const {
    return cell(deep ? empty_deep_relic_art : empty_relic_art);
}

std::optional<unsigned int> CatalogPresentation::effect_cell(std::uint32_t id) const {
    const auto *effect = rules_.effects().find(id);
    return effect && effect->icon >= 0
               ? cell({IconKind::Status, static_cast<std::uint32_t>(effect->icon)})
               : std::nullopt;
}

std::vector<PresentedEffect> CatalogPresentation::candidates(std::uint32_t item_id, bool curses,
                                                             bool all) const {
    std::vector<PresentedEffect> result;
    if (all) {
        for (const auto &[id, effect] : rules_.effects().entries())
            result.push_back({&effect, describe(id)});
    } else {
        for (const auto *effect : rules_.candidates(item_id, curses))
            result.push_back({effect, describe(effect->id)});
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.description == right.description ? left.effect->id < right.effect->id
                                                     : left.description < right.description;
    });
    return result;
}
} // namespace ern
