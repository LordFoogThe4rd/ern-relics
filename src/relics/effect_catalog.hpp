#pragma once

#include <cstdint>
#include <unordered_map>

namespace ern {
struct Effect {
    std::uint32_t id;
    int compatibility;
    int order;
    std::int32_t text_id = -1;
    std::int32_t icon = -1;
    int exclusivity = -1;
};

class EffectCatalog {
  public:
    std::size_t size() const { return effects_.size(); }
    const Effect *find(std::uint32_t id) const;
    const auto &entries() const { return effects_; }

  private:
    friend class RelicValidator;
    std::unordered_map<std::uint32_t, Effect> effects_;
};
} // namespace ern
