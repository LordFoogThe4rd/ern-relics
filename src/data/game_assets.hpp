#pragma once
#include "data/asset_types.hpp"
#include <filesystem>
#include <span>

namespace ern {
// Extract selected artwork and messages into memory from the user's installation.
// The source tree contains no exported game data.
GameAssets read_game_assets(const std::filesystem::path &directory, std::span<const IconKey> icons);
} // namespace ern
