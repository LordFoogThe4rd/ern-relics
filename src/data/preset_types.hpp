#pragma once
#include "relics/preset.hpp"

namespace ern {
struct SavedPreset {
    std::string name;
    std::optional<Preset> contents;
};
enum class PresetSaveMode { Create, Replace };
enum class PresetStoreError { InvalidName, AlreadyExists, Read, CreateFolder, Write, Delete };
} // namespace ern
