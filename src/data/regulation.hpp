#pragma once
#include "data/fingerprint.hpp"
#include "relics/rules.hpp"
#include <span>
#include <unordered_map>

namespace ern {
// All offsets, row sizes and versions are checked before interpreting external data.
// Supports encrypted regulation, DCX/Zstandard, DCX/Kraken and plain BND4.
std::variant<RegulationRules, FileError>
read_regulation(std::span<const std::uint8_t> bytes, const std::filesystem::path &game_directory);
} // namespace ern
