#pragma once

#include "relics/effect_catalog.hpp"
#include "relics/relic_catalog.hpp"

namespace ern {
struct RollWeight {
    bool base = false;
    bool dlc = false;
};
using RollPool = std::unordered_map<std::uint32_t, RollWeight>;
struct RegulationRules {
    std::unordered_map<std::uint32_t, Effect> effects;
    std::unordered_map<std::uint32_t, RelicInfo> items;
    std::unordered_map<std::int32_t, RollPool> pools;
};
} // namespace ern
