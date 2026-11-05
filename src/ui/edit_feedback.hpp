#pragma once

#include "data/preset_types.hpp"
#include "relics/edit_result.hpp"

namespace ern::ui {
inline std::string rejection_message(const EditRejected &rejected) {
    if (!rejected.detail.empty())
        return rejected.detail;
    switch (rejected.reason) {
    case EditRejectReason::NotAtRoundtable:
        return "Relic editing is only available in Roundtable Hold. Return there and wait for loading to finish.";
    case EditRejectReason::Stale:
        return "Relic changed in game. Reload it before applying changes.";
    case EditRejectReason::Locked:
        return "This relic is read only. Its effects cannot be modified.";
    case EditRejectReason::Invalid:
        return "The final relic check failed. No effects were changed.";
    case EditRejectReason::Unreadable:
        return "Cannot resolve the equipped relic. Wait until loading finishes, then refresh.";
    case EditRejectReason::RulesUnavailable:
        return "Relic roll data is unavailable.";
    case EditRejectReason::JournalOpen:
        return "Cannot open the backup journal. No effects were changed.";
    case EditRejectReason::JournalWrite:
        return "Cannot write the backup journal. No effects were changed.";
    case EditRejectReason::Unchanged:
        return "No effects have changed.";
    case EditRejectReason::EquipUnavailable:
        return "The game cannot unequip this relic yet. No effects were changed. Try again after loading finishes.";
    }
    return "Unknown edit rejection.";
}

inline std::string failure_message(const EditFailure &failure) {
    if (const auto *rejected = std::get_if<EditRejected>(&failure))
        return rejection_message(*rejected);
    const auto failed = std::get<WriteFailure>(failure);
    const char *reason = failed.stage == WriteStage::Write ? "The memory write failed. "
                         : failed.stage == WriteStage::Equipment
                             ? "The relic equip cycle failed. "
                             : "The change could not be verified. ";
    return std::string(reason) +
           (failed.restoration == Restoration::Verified
                ? "The original effects were restored."
                : "Restoration could not be verified. Check the relic before continuing.");
}

inline std::string slot_failure_message(const SlotFailure &failure) {
    return "Slot " + std::to_string(failure.slot + 1) + ": " + failure_message(failure.failure);
}

inline std::string preset_store_message(PresetStoreError error) {
    switch (error) {
    case PresetStoreError::InvalidName:
        return "Use 1-32 letters, numbers, spaces, underscores, or hyphens. Start and end without spaces, and avoid Windows device names such as CON, NUL, COM1, or LPT1.";
    case PresetStoreError::AlreadyExists:
        return "A preset with this name already exists. Choose Replace preset to overwrite it, or use another name.";
    case PresetStoreError::Read:
        return "Could not read the presets folder.";
    case PresetStoreError::CreateFolder:
        return "Could not create the presets folder.";
    case PresetStoreError::Write:
        return "Could not save the preset.";
    case PresetStoreError::Delete:
        return "Could not delete the preset.";
    }
    return "Unknown preset error.";
}
} // namespace ern::ui
