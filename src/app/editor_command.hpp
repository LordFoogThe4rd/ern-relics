#pragma once
#include "app/inspection.hpp"
#include "data/preset_types.hpp"
#include "relics/relic_drafts.hpp"
#include "relics/preset.hpp"

namespace ern {
struct RefreshInspection {};
struct SetEffect {
    std::size_t slot;
    EffectPosition effect;
    std::uint32_t id;
};
struct ReloadSlot {
    std::size_t slot;
};
struct DiscardDrafts {};
struct ApplyDrafts {
    bool invalid = false;
};
struct UndoEdit {};
struct ReloadPresets {};
struct SavePreset {
    std::string name;
    PresetSaveMode mode = PresetSaveMode::Create;
};
struct DeletePreset {
    std::string name;
};
struct LoadPreset {
    Preset preset;
};
using EditorAction =
    std::variant<RefreshInspection, SetEffect, ReloadSlot, DiscardDrafts, ApplyDrafts, UndoEdit,
                 ReloadPresets, SavePreset, DeletePreset, LoadPreset>;

struct AppliedDrafts {
    DraftApplyResult result;
};
struct UndoneEdit {
    UndoResult result;
};
struct SavedPresetResult {
    std::string name;
    std::size_t count;
};
struct DeletedPresetResult {};
struct LoadedPresetResult {
    PresetStage staged;
    std::size_t requested;
};
struct OperationFailed {
    std::string detail;
};
using EditorOutcome =
    std::variant<std::monostate, EditRejected, AppliedDrafts, UndoneEdit, SavedPresetResult,
                 DeletedPresetResult, LoadedPresetResult, PresetStoreError, OperationFailed>;
struct OperationId {
    std::uint64_t value;
    bool operator==(const OperationId &) const = default;
};
struct EditorCompletion {
    OperationId operation;
    EditorAction action;
    EditorOutcome outcome;
};
} // namespace ern
