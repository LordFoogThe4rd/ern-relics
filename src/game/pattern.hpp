#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ern {
using Pattern = std::vector<std::optional<std::byte>>;
std::optional<Pattern> parse_pattern(std::string_view text);
// Ambiguous signatures are rejected instead of choosing an arbitrary match.
std::optional<std::size_t> find_unique(std::span<const std::byte> bytes, const Pattern &pattern);
std::optional<std::uintptr_t> resolve_rip(std::span<const std::byte> bytes,
                                          std::uintptr_t section_address, std::size_t instruction,
                                          std::size_t displacement_offset);
// Multiple instructions may refer to one global. Reject distinct global targets.
std::optional<std::uintptr_t> find_unique_rip_target(std::span<const std::byte> bytes,
                                                     std::uintptr_t section_address,
                                                     const Pattern &pattern,
                                                     std::size_t displacement_offset);
} // namespace ern
