#pragma once

#include "data/game_assets.hpp"

// Invented labels, independent of the fixture's validation rules.
inline ern::GameAssets synthetic_assets() {
    ern::GameAssets assets;
    for (const auto id : {7000400u, 7000401u, 7000402u, 7000601u, 7000602u, 7001801u, 7001802u,
                          10000u, 11000u, 8100000u, 7100110u, 7120100u})
        assets.effect_names[static_cast<std::int32_t>(id)] = "Fixture effect " + std::to_string(id);
    assets.effect_names[11000] = "[Fixture] Character bonus";
    assets.effect_names[900001] = "Fixture curse: reduced damage negation";
    assets.effect_names[900002] = "Fixture curse: increased FP consumption";
    assets.effect_names[900003] = "Fixture curse: reduced maximum HP";
    for (const auto id : {102u, 202u, 105u, 108u, 111u, 0xF4AE4u, 1003002u})
        assets.item_names[id] = "Fixture random relic";
    assets.item_names[10000] = "Fixture unique relic";
    assets.item_names[2000] = "Fixture locked reward";
    assets.item_names[11003] = "Fixture fixed relic";
    assets.item_names[2000000] = "Fixture Deep relic";
    return assets;
}
