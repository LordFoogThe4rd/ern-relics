#pragma once
#include "relics/types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace ern {
enum class RelicKind { Rolled, Unique, Fixed };
enum class RelicFamily { Normal, Deep };
struct RelicInfo {
    std::uint32_t id;
    RelicKind kind;
    unsigned int color;
    unsigned int icon;
    Effects fixed_effects;
    std::array<std::int32_t, 3> effect_tables;
    std::array<std::int32_t, 3> curse_tables{-1, -1, -1};
    RelicFamily family = RelicFamily::Normal;
};
class RelicCatalog {
  public:
    const RelicInfo *find(std::uint32_t game_item_id) const;
    std::size_t size() const { return entries_.size(); }
    const auto &entries() const { return entries_; }

  private:
    friend class RelicValidator;
    std::unordered_map<std::uint32_t, RelicInfo> entries_;
};
// One policy shared by selection, preset staging, Apply, Undo, and the memory writer.
const char *relic_lock_reason(const RelicInfo *info);
const char *relic_kind_name(const RelicInfo *info);
} // namespace ern
