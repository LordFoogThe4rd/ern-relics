#include "relics/effect_catalog.hpp"

namespace ern {

const Effect *EffectCatalog::find(std::uint32_t id) const {
    const auto it = effects_.find(id);
    return it == effects_.end() ? nullptr : &it->second;
}

} // namespace ern
