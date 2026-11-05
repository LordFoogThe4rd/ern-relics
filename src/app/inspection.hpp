#pragma once
#include "relics/types.hpp"
#include <string>
#include <variant>
#include <filesystem>

namespace ern {
struct SessionId {
    std::uint64_t value = 0;
    bool operator==(const SessionId &) const = default;
};
struct InspectionMessage {
    std::string text;
};
using Inspection = std::variant<InspectionMessage, EquippedRelics>;
struct RegulationDiagnostics {
    std::filesystem::path requested_path;
    std::filesystem::path resolved_path;
    std::string detected_hash;
    std::string error;
};
} // namespace ern
