#include "data/regulation.hpp"
#include "data/game_files.hpp"
#include "game_keys.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace ern {
namespace {
using namespace game;
using Row = std::pair<std::int32_t, View>;
std::vector<Row> rows(View data, std::uint16_t version, std::size_t size, const char *name) {
    require(number<std::uint8_t>(data, 44) == 0 && number<std::uint8_t>(data, 45) == 0x85 &&
                number<std::uint16_t>(data, 8) == version,
            name);
    const auto count = number<std::uint16_t>(data, 10);
    slice(data, 64, std::size_t(count) * 24);
    std::vector<Row> result;
    result.reserve(count);
    const std::size_t first_data = 64 + std::size_t(count) * 24;
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = number<std::int32_t>(data, 64 + i * 24);
        const auto at = number<std::uint64_t>(data, 72 + i * 24);
        const auto end = i + 1 < count ? number<std::uint64_t>(data, 72 + (i + 1) * 24)
                                       : number<std::uint32_t>(data, 0);
        require(id >= 0 && at >= first_data && end >= at && end - at == size, name);
        result.emplace_back(id, slice(data, static_cast<std::size_t>(at), size));
    }
    require(!result.empty(), name);
    return result;
}
} // namespace

std::variant<RegulationRules, FileError>
read_regulation(game::View bytes, const std::filesystem::path &game_directory) {
    using namespace game;
    try {
        Bytes decrypted;
        if (!starts(bytes, "BND4") && !starts(bytes, std::string_view("DCX\0", 4))) {
            require(bytes.size() >= 32, "The regulation is not an AES-CBC file.");
            decrypted = decrypt_aes(bytes.subspan(16), regulation_key, bytes.first(16));
            bytes = decrypted;
        }
        constexpr std::array<std::string_view, 3> names{
            "EquipParamAntique.param", "AttachEffectParam.param", "AttachEffectTableParam.param"};
        const auto files = binder(bytes, game_directory, [&](auto name) {
            return std::find(names.begin(), names.end(), name) != names.end();
        });
        std::array<View, 3> params;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto found = files.find(std::string(names[i]));
            require(found != files.end(),
                    "The regulation is missing a required relic parameter file.");
            params[i] = found->second;
        }
        RegulationRules result;
        for (const auto &[table, row] :
             rows(params[2], 1, 12, "Unsupported AttachEffectTableParam version or row layout.")) {
            const auto id = number<std::int32_t>(row, 4);
            const auto base = number<std::uint16_t>(row, 8);
            const auto dlc = number<std::int16_t>(row, 10);
            require(id >= -1 && dlc >= -1,
                    "A roll table contains an unsupported effect ID or DLC weight.");
            auto &weight = result.pools[table][static_cast<std::uint32_t>(id)];
            weight.base = weight.base || base > 0;
            weight.dlc = weight.dlc || (dlc >= 0 ? dlc : base) > 0;
        }
        for (const auto &[id, row] :
             rows(params[1], 3, 68, "Unsupported AttachEffectParam version or row layout.")) {
            Effect effect{static_cast<std::uint32_t>(id), number<std::int32_t>(row, 24),
                          number<std::int32_t>(row, 52)};
            effect.text_id = number<std::int32_t>(row, 28);
            effect.icon = number<std::int32_t>(row, 40);
            effect.exclusivity = number<std::int32_t>(row, 60);
            require(result.effects.emplace(effect.id, std::move(effect)).second,
                    "Duplicate AttachEffectParam row ID.");
        }
        for (const auto &[id, row] :
             rows(params[0], 2, 48, "Unsupported EquipParamAntique version or row layout.")) {
            if (id < 100 || (row[0] & 1) || row[4] > 3)
                continue;
            RelicInfo item{static_cast<std::uint32_t>(id), RelicKind::Rolled, row[4],
                           number<std::uint32_t>(row, 28), empty_effects,     {}};
            item.family = (row[7] & 1) != 0 ? RelicFamily::Deep : RelicFamily::Normal;
            bool occupied = false, rolled = row[6] != 0;
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const auto table = number<std::int32_t>(row, 16 + slot * 4);
                const auto curse_table = number<std::int32_t>(row, 32 + slot * 4);
                require(table >= -1 && curse_table >= -1,
                        "A relic contains an unsupported roll table ID.");
                item.effect_tables[slot] = table;
                item.curse_tables[slot] = curse_table;
                if (table == -1)
                    continue;
                occupied = true;
                const auto pool = result.pools.find(table);
                std::set<std::uint32_t> candidates;
                if (pool != result.pools.end())
                    for (const auto &[effect, weight] : pool->second)
                        if (effect != UINT32_MAX && (weight.base || weight.dlc))
                            candidates.insert(effect);
                rolled = rolled && candidates.size() > 1;
                if (candidates.size() == 1)
                    item.fixed_effects.rows[slot].effect = *candidates.begin();
                if (item.family == RelicFamily::Deep && curse_table != -1) {
                    const auto curse_pool = result.pools.find(curse_table);
                    // Include empty outcomes, since optional curses are not fixed.
                    std::set<std::uint32_t> curses;
                    if (curse_pool != result.pools.end())
                        for (const auto &[effect, weight] : curse_pool->second)
                            if (weight.base || weight.dlc)
                                curses.insert(effect);
                    if (curses.size() == 1)
                        item.fixed_effects.rows[slot].curse = *curses.begin();
                }
            }
            if (!occupied)
                continue;
            item.kind = rolled ? RelicKind::Rolled : !row[6] ? RelicKind::Unique : RelicKind::Fixed;
            require(result.items.emplace(item.id, std::move(item)).second,
                    "Duplicate EquipParamAntique row ID.");
        }
        require(!result.items.empty() && !result.effects.empty() && !result.pools.empty(),
                "The regulation contains no usable relic rules.");
        return result;
    } catch (const std::exception &error) {
        return FileError{error.what()};
    }
}
} // namespace ern
