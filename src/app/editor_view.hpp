#pragma once
#include "app/editor_command.hpp"
#include "data/catalog_presentation.hpp"
#include "game/edit_location.hpp"

namespace ern {
// Published as shared_ptr<const EditorView>. No reference points into mutable Runtime state.
struct EditorView {
    SessionId session;
    std::shared_ptr<const RelicValidator> rules;
    std::shared_ptr<const GameAssets> resources;
    std::shared_ptr<const SystemFonts> fonts;
    Inspection inspection = InspectionMessage{"Waiting for the game to load."};
    Drafts drafts;
    DraftReview review;
    RuleContext context;
    EditLocation location = EditLocation::Unavailable;
    RegulationDiagnostics rule_diagnostics;
    std::string lock_reason, version, source_label, source_hash, hotkey_name;
    std::string application_error;
    std::filesystem::path journal_path;
    int hotkey = 0x77;
    bool open_on_start = false;
    bool can_undo = false;
    std::shared_ptr<const std::vector<SavedPreset>> presets;
    std::optional<PresetStoreError> preset_error;
    std::optional<EditorCompletion> completion;

    const RelicValidator &validator() const { return *rules; }
    const EffectCatalog &catalog() const { return rules->effects(); }
    const RelicCatalog &items() const { return rules->items(); }
    const GameAssets &assets() const { return *resources; }
    CatalogPresentation presentation() const { return {*rules, *resources}; }
    bool editing_supported() const { return lock_reason.empty(); }
    const char *editing_lock_reason() const {
        return lock_reason.empty() ? nullptr : lock_reason.c_str();
    }
    RelicValidation validate(std::uint32_t item, Effects effects) const {
        return rules->validate(item, effects, context);
    }
};
} // namespace ern
