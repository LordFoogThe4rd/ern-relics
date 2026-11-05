#pragma once

#include "game/relic_reader.hpp"

namespace ern {
inline constexpr std::uint64_t fixture_magic = 0x45524E52454C4932;
enum class HostLocation { Roundtable, Expedition, Loading, Unavailable };
struct RegulationRules;
struct GameAssets;
// Shared with the synthetic host, which exports ern_host_fixture before loading the DLL.
struct HostFixture {
    std::uint64_t magic;
    RelicRoots roots;
    const RegulationRules *rules = nullptr;
    const GameAssets *assets = nullptr;
    bool (*read)(Address, std::span<std::byte>) = nullptr;
    bool (*write)(Address, std::span<const std::byte>) = nullptr;
};
} // namespace ern
