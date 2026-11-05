#include "support/check.hpp"
#include "game/pattern.hpp"

#include <array>
#include <cstring>
#include <vector>

using test::require;

int main() {
    return test::run("signature matching and RIP targets", [] {
        const auto pattern = ern::parse_pattern("48 8B ?? 01");
        require(pattern.has_value(), "Valid pattern rejected");
        require(!ern::parse_pattern(""), "Empty pattern accepted");
        require(!ern::parse_pattern("48 ZZ"), "Malformed pattern accepted");
        const std::array<std::byte, 8> code{std::byte{0}, std::byte{0x48}, std::byte{0x8B},
                                            std::byte{0xFF}, std::byte{1}};
        require(ern::find_unique(code, *pattern) == 1, "Wildcard signature search failed");
        auto ambiguous = std::vector<std::byte>(code.begin(), code.end());
        ambiguous.insert(ambiguous.end(), code.begin(), code.end());
        require(!ern::find_unique(ambiguous, *pattern), "Ambiguous signature accepted");
        require(!ern::find_unique(std::span(code).first(2), *pattern),
                "Truncated pattern accepted");
        std::array<std::byte, 16> rip{};
        const std::int32_t backward = -10;
        std::memcpy(rip.data() + 3, &backward, sizeof(backward));
        require(ern::resolve_rip(rip, 0x1000, 0, 3) == 0xFFD, "Negative RIP displacement failed");
        require(!ern::resolve_rip(rip, 0x1000, 14, 3), "Out-of-range RIP displacement accepted");

        std::array<std::byte, 48> references{};
        const auto reference_pattern = *ern::parse_pattern("48 8B 05 ?? ?? ?? ?? 48 8B 70 08");
        for (const std::size_t start : {0u, 16u}) {
            for (std::size_t i = 0; i < reference_pattern.size(); ++i)
                if (reference_pattern[i])
                    references[start + i] = *reference_pattern[i];
            const auto displacement = static_cast<std::int32_t>(0x5000 - (0x1000 + start + 7));
            std::memcpy(references.data() + start + 3, &displacement, sizeof(displacement));
        }
        require(ern::find_unique_rip_target(references, 0x1000, reference_pattern, 3) == 0x5000,
                "Multiple references to the same global were rejected");
        const std::int32_t different_global = 0x6000 - (0x1000 + 16 + 7);
        std::memcpy(references.data() + 19, &different_global, sizeof(different_global));
        require(!ern::find_unique_rip_target(references, 0x1000, reference_pattern, 3),
                "Distinct global targets accepted");
    });
}
