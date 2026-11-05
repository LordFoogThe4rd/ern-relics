#pragma once

#include "relics/types.hpp"
#include <optional>
#include <string>
#include <variant>

namespace ern {
enum class EditRejectReason {
    NotAtRoundtable,
    Stale,
    Locked,
    Invalid,
    Unreadable,
    RulesUnavailable,
    JournalOpen,
    JournalWrite,
    Unchanged,
    EquipUnavailable
};
struct EditRejected {
    EditRejectReason reason;
    std::string detail;
};
enum class WriteStage { Write, Verification, Equipment };
enum class Restoration { Verified, Unconfirmed };
struct WriteFailure {
    WriteStage stage;
    Restoration restoration;
};
using EditFailure = std::variant<EditRejected, WriteFailure>;
using EditResult = std::variant<Relic, EditRejected, WriteFailure>;

struct SlotRejection {
    std::size_t slot;
    EditRejected rejection;
};
struct SlotFailure {
    std::size_t slot;
    EditFailure failure;
};
struct UndoResult {
    std::size_t restored = 0;
    std::optional<SlotFailure> failure;
};
} // namespace ern
