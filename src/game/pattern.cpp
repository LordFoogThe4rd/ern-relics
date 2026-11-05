#include "game/pattern.hpp"

#include <charconv>
#include <cstring>
#include <limits>

namespace ern {
namespace {
bool matches(std::span<const std::byte> bytes, const Pattern &pattern) {
    for (std::size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] && *pattern[i] != bytes[i])
            return false;
    return true;
}
} // namespace

std::optional<Pattern> parse_pattern(std::string_view text) {
    Pattern result;
    while (!text.empty()) {
        const auto start = text.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
            break;
        text.remove_prefix(start);
        const auto end = text.find_first_of(" \t\r\n");
        const auto token = text.substr(0, end);
        if (token == "?" || token == "??") {
            result.push_back(std::nullopt);
        } else {
            unsigned int value = 0;
            const auto parsed =
                std::from_chars(token.data(), token.data() + token.size(), value, 16);
            if (token.size() != 2 || parsed.ec != std::errc{} ||
                parsed.ptr != token.data() + token.size() || value > 255)
                return std::nullopt;
            result.push_back(static_cast<std::byte>(value));
        }
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end);
    }
    if (result.empty())
        return std::nullopt;
    return result;
}

std::optional<std::size_t> find_unique(std::span<const std::byte> bytes, const Pattern &pattern) {
    if (pattern.empty() || pattern.size() > bytes.size())
        return std::nullopt;
    std::optional<std::size_t> match;
    for (std::size_t i = 0; i <= bytes.size() - pattern.size(); ++i) {
        if (matches(bytes.subspan(i, pattern.size()), pattern)) {
            if (match)
                return std::nullopt;
            match = i;
        }
    }
    return match;
}

std::optional<std::uintptr_t> resolve_rip(std::span<const std::byte> bytes,
                                          std::uintptr_t section_address, std::size_t instruction,
                                          std::size_t displacement_offset) {
    if (instruction > bytes.size() || displacement_offset > bytes.size() - instruction)
        return std::nullopt;
    const auto offset = instruction + displacement_offset;
    if (bytes.size() - offset < sizeof(std::int32_t))
        return std::nullopt;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + offset, sizeof(displacement));
    if (section_address > static_cast<std::uintptr_t>(INTPTR_MAX) - offset - 4)
        return std::nullopt;
    const auto next = static_cast<std::intptr_t>(section_address + offset + 4);
    if (displacement > 0 && next > INTPTR_MAX - displacement)
        return std::nullopt;
    if (displacement < 0 && next < -static_cast<std::intptr_t>(displacement))
        return std::nullopt;
    return static_cast<std::uintptr_t>(next + displacement);
}

std::optional<std::uintptr_t> find_unique_rip_target(std::span<const std::byte> bytes,
                                                     std::uintptr_t section_address,
                                                     const Pattern &pattern,
                                                     std::size_t displacement_offset) {
    if (pattern.empty() || pattern.size() > bytes.size())
        return std::nullopt;
    std::optional<std::uintptr_t> target;
    for (std::size_t i = 0; i <= bytes.size() - pattern.size(); ++i) {
        if (!matches(bytes.subspan(i, pattern.size()), pattern))
            continue;
        const auto resolved = resolve_rip(bytes, section_address, i, displacement_offset);
        if (!resolved || (target && target != resolved))
            return std::nullopt;
        target = resolved;
    }
    return target;
}
} // namespace ern
