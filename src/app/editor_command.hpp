#pragma once
#include "app/inspection.hpp"
#include "data/preset_types.hpp"
#include "relics/relic_drafts.hpp"
#include "relics/preset.hpp"

namespace ern {
// Equipped checks the character session and equipped relics. Drafts also checks pending edits.
enum class ActionFreshness { Independent, Equipped, Drafts };

struct RefreshInspection {
    static constexpr auto freshness = ActionFreshness::Independent;
};
struct SetEffect {
    static constexpr auto freshness = ActionFreshness::Drafts;
    std::size_t slot;
    EffectPosition effect;
    std::uint32_t id;
};
struct ReloadSlot {
    static constexpr auto freshness = ActionFreshness::Drafts;
    std::size_t slot;
};
struct DiscardDrafts {
    static constexpr auto freshness = ActionFreshness::Drafts;
};
struct ApplyDrafts {
    static constexpr auto freshness = ActionFreshness::Drafts;
};
struct UndoEdit {
    static constexpr auto freshness = ActionFreshness::Equipped;
};
struct ReloadPresets {
    static constexpr auto freshness = ActionFreshness::Independent;
};
struct SavePreset {
    static constexpr auto freshness = ActionFreshness::Equipped;
    std::string name;
    PresetSaveMode mode = PresetSaveMode::Create;
};
struct DeletePreset {
    static constexpr auto freshness = ActionFreshness::Independent;
    std::string name;
};
struct LoadPreset {
    static constexpr auto freshness = ActionFreshness::Drafts;
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
