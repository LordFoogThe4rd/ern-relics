#pragma once
#include "relics/rules.hpp"

// Invented fixture rules. No extracted game files are needed by tests.
inline ern::RegulationRules synthetic_rules() {
    ern::RegulationRules rules;
    for (const auto id : {7000400u, 7000401u, 7000402u, 7000601u, 7000602u, 7001801u, 7001802u,
                          10000u, 11000u, 8100000u, 7100110u, 7120100u}) {
        const int group = id >= 7000400 && id <= 7000402 ? 1 : -1;
        rules.effects.emplace(id,
                              ern::Effect{id, group, static_cast<int>(id), static_cast<int>(id)});
    }
    for (const auto id : {7000400u, 7000401u, 7000402u})
        rules.pools[10][id] = {true, true};
    for (const auto id : {7000601u, 7000602u})
        rules.pools[20][id] = {true, true};
    for (const auto id : {7001801u, 7001802u})
        rules.pools[30][id] = {true, true};
    rules.pools[40][10000] = {true, true};
    rules.pools[50][7000402] = {true, true};
    rules.pools[60][7100110] = {true, true};
    rules.pools[70][7120100] = {true, true};
    for (const auto id : {102u, 202u, 105u, 108u, 111u, 0xF4AE4u, 1003002u})
        rules.items.emplace(
            id, ern::RelicInfo{
                    id, ern::RelicKind::Rolled, id % 4, 0, ern::empty_effects, {10, 20, 30}});
    rules.items.emplace(
        10000,
        ern::RelicInfo{10000,
                       ern::RelicKind::Unique,
                       0,
                       0,
                       {{10000, UINT32_MAX}, {7000402, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
                       {40, 50, -1}});
    rules.items.emplace(
        2000,
        ern::RelicInfo{2000,
                       ern::RelicKind::Unique,
                       0,
                       0,
                       {{7100110, UINT32_MAX}, {7120100, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
                       {60, 70, -1}});
    rules.items.emplace(
        11003,
        ern::RelicInfo{11003,
                       ern::RelicKind::Fixed,
                       0,
                       0,
                       {{10000, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}, {UINT32_MAX, UINT32_MAX}},
                       {40, -1, -1}});
    for (const auto id : {900001u, 900002u, 900003u}) {
        rules.effects.emplace(id, ern::Effect{id, -1, static_cast<int>(id), static_cast<int>(id)});
        rules.pools[3000000][id] = {true, true};
    }
    for (const auto id : {7001801u, 7001802u})
        rules.pools[2000000][id] = {true, true};
    for (const auto id : {7000400u, 7000401u, 7000402u, 7000601u, 7000602u})
        rules.pools[2100000][id] = {true, true};
    rules.items.emplace(2000000, ern::RelicInfo{2000000,
                                                ern::RelicKind::Rolled,
                                                0,
                                                0,
                                                ern::empty_effects,
                                                {2000000, 2100000, 2100000},
                                                {3000000, -1, -1},
                                                ern::RelicFamily::Deep});
    return rules;
}
