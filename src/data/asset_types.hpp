#pragma once
#include <compare>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace ern {
enum class IconKind { Item, Status };
struct IconKey {
    IconKind kind;
    std::uint32_t id;
    auto operator<=>(const IconKey &) const = default;
};
struct IconAtlas {
    static constexpr unsigned int columns = 16, icon_size = 160, gutter = 2,
                                  cell_size = icon_size + gutter * 2;
    unsigned int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
struct GameAssets {
    std::unordered_map<std::uint32_t, std::string> item_names;
    std::unordered_map<std::int32_t, std::string> effect_names;
    std::map<IconKey, unsigned int> cells;
    IconAtlas atlas;
    std::vector<std::string> warnings;
};

struct SystemFonts {
    std::vector<std::uint8_t> controls;
    std::vector<std::uint8_t> headings;
};

} // namespace ern
