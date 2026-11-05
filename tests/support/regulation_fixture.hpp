#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

// Invented binary records shared by source and runtime integration tests.
namespace test::regulation {
using Bytes = std::vector<std::uint8_t>;
template <typename T> void put(Bytes &data, std::size_t at, T value) {
    std::memcpy(data.data() + at, &value, sizeof(value));
}

inline Bytes param(std::uint16_t version, const std::vector<std::pair<std::int32_t, Bytes>> &rows) {
    Bytes result(64 + rows.size() * 24);
    put(result, 8, version);
    put(result, 10, static_cast<std::uint16_t>(rows.size()));
    result[45] = 0x85;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        put(result, 64 + index * 24, rows[index].first);
        put(result, 72 + index * 24, static_cast<std::uint64_t>(result.size()));
        result.insert(result.end(), rows[index].second.begin(), rows[index].second.end());
    }
    put(result, 0, static_cast<std::uint32_t>(result.size()));
    return result;
}

inline Bytes item(std::uint8_t salable, std::uint8_t deep, std::array<std::int32_t, 3> tables,
                  std::array<std::int32_t, 3> curses = {-1, -1, -1}) {
    Bytes row(48);
    row[6] = salable;
    row[7] = deep;
    for (std::size_t i = 0; i < 3; ++i) {
        put(row, 16 + i * 4, tables[i]);
        put(row, 32 + i * 4, curses[i]);
    }
    put(row, 28, std::uint32_t(19108));
    return row;
}

inline Bytes effect(std::int32_t id) {
    Bytes row(68);
    put(row, 24, std::int32_t(-1));
    put(row, 28, id);
    put(row, 52, -id);
    put(row, 60, std::int32_t(-1));
    return row;
}

inline Bytes weight(std::int32_t id, std::uint16_t base = 1, std::int16_t dlc = -1) {
    Bytes row(12);
    put(row, 4, id);
    put(row, 8, base);
    put(row, 10, dlc);
    return row;
}

inline Bytes binder(const std::array<Bytes, 3> &params, bool with_ids = true) {
    constexpr std::array<std::string_view, 3> names{
        "EquipParamAntique.param", "AttachEffectParam.param", "AttachEffectTableParam.param"};
    const std::size_t stride = with_ids ? 36 : 32;
    Bytes result(64 + 3 * stride);
    std::memcpy(result.data(), "BND4", 4);
    put(result, 12, std::uint32_t(3));
    put(result, 32, static_cast<std::uint64_t>(stride));
    result[49] = with_ids ? 0x2E : 0x2C;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto entry = 64 + i * stride;
        put(result, entry + 8, static_cast<std::uint64_t>(params[i].size()));
        put(result, entry + (with_ids ? 32 : 28), static_cast<std::uint32_t>(result.size()));
        result.insert(result.end(), names[i].begin(), names[i].end());
        result.push_back(0);
        put(result, entry + 24, static_cast<std::uint32_t>(result.size()));
        result.insert(result.end(), params[i].begin(), params[i].end());
    }
    return result;
}

inline Bytes rolled_relics() {
    const auto items = param(2, {{102, item(1, 0, {10, 20, 30})},
                                 {105, item(1, 0, {10, 20, 30})},
                                 {108, item(1, 0, {10, 20, 30})}});
    const auto effects = param(3, {{7000400, effect(7000400)},
                                   {7000401, effect(7000401)},
                                   {7000601, effect(7000601)},
                                   {7000602, effect(7000602)},
                                   {7001801, effect(7001801)},
                                   {7001802, effect(7001802)}});
    const auto pools = param(1, {{10, weight(7000400)},
                                 {10, weight(7000401)},
                                 {20, weight(7000601)},
                                 {20, weight(7000602)},
                                 {30, weight(7001801)},
                                 {30, weight(7001802)}});
    return binder({items, effects, pools});
}

} // namespace test::regulation
