#pragma once

#include "data/preset_types.hpp"
#include <filesystem>
#include <vector>

namespace ern {

class PresetStore {
  public:
    explicit PresetStore(std::filesystem::path directory) : directory_(std::move(directory)) {}
    std::variant<std::vector<SavedPreset>, PresetStoreError> list() const;
    std::optional<PresetStoreError> save(std::string_view name, const Preset &preset,
                                         PresetSaveMode mode = PresetSaveMode::Create) const;
    std::optional<PresetStoreError> remove(std::string_view name) const;

  private:
    std::filesystem::path directory_;
};
} // namespace ern
