#include "relics/relic_catalog.hpp"

namespace ern {

const RelicInfo *RelicCatalog::find(std::uint32_t game_item_id) const {
    // CSGaitem +0x0C contains the category-tagged EquipParamAntique row ID.
    if ((game_item_id & 0xF0000000) != 0x80000000)
        return nullptr;
    const auto found = entries_.find(game_item_id & 0x0FFFFFFF);
    return found == entries_.end() ? nullptr : &found->second;
}

const char *relic_lock_reason(const RelicInfo *info) {
    if (!info)
        return "This relic is unrecognized. Editing is unavailable.";
    switch (info->kind) {
    case RelicKind::Rolled:
        return nullptr;
    case RelicKind::Unique:
        return "Unique relics have fixed effects and cannot be modified.";
    case RelicKind::Fixed:
        return "This relic has fixed effects and cannot be modified.";
    }
    return "This relic is unrecognized. Editing is unavailable.";
}

const char *relic_kind_name(const RelicInfo *info) {
    if (!info)
        return "Unrecognized relic";
    switch (info->kind) {
    case RelicKind::Rolled:
        return info->family == RelicFamily::Deep ? "Deep Relic" : "Normal relic";
    case RelicKind::Unique:
        return info->family == RelicFamily::Deep ? "Unique Deep Relic" : "Unique relic";
    case RelicKind::Fixed:
        return info->family == RelicFamily::Deep ? "Fixed Deep Relic" : "Fixed relic";
    }
    return "Unrecognized relic";
}
} // namespace ern
