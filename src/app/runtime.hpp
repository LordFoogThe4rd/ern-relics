#pragma once

#include "relics/effect_catalog.hpp"
#include "game/game_layout.hpp"
#include "game/relic_reader.hpp"
#include "game/relic_writer.hpp"
#include "app/active_regulation.hpp"
#include "app/editor_session.hpp"
#include "app/runtime_environment.hpp"
#include "app/inspection.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace ern {
class Runtime {
  public:
    void initialize(RuntimeEnvironment environment);
    // Called by the application worker after installing the queue-capture hooks.
    void prepare_assets();
    void poll();
    void refresh();
    void synchronize();
    int hotkey() const { return environment_.hotkey; }
    bool open_on_start() const { return environment_.open_on_start; }
    const std::string &version() const { return environment_.executable_version; }
    const std::string &hotkey_name() const { return hotkey_name_; }
    const EffectCatalog &catalog() const { return validator().effects(); }
    const RelicCatalog &items() const { return validator().items(); }
    const RelicValidator &validator() const { return *rules(); }
    std::shared_ptr<const RelicValidator> rules() const {
        return regulation_ ? regulation_->rules() : supplied_rules_;
    }
    const std::string &source_hash() const;
    const RegulationDiagnostics &rule_diagnostics() const;
    const RuleContext &rule_context() const { return rule_context_; }
    RelicValidation validate(std::uint32_t item_id, Effects effects) const {
        return validator().validate(item_id, effects, rule_context_);
    }
    const Inspection &inspection() const { return inspection_; }
    std::uint32_t equipped_offset() const { return environment_.equipped_offset; }
    const std::filesystem::path &directory() const { return environment_.directory; }
    const std::filesystem::path &journal_path() const { return journal_path_; }
    const std::string &source_label() const { return environment_.source_label; }
    const GameAssets &assets() const { return *assets_; }
    std::shared_ptr<const GameAssets> shared_assets() const { return assets_; }
    SessionId session() const { return session_; }
    EditorSession &editor() { return editor_; }
    bool editing_supported() const;
    const char *editing_lock_reason() const;
    EditLocation edit_location() const { return location_; }
    DraftApplyResult apply_drafts();
    UndoResult undo();
    bool can_undo() const { return undo_.has_value(); }

  private:
    void log(std::string_view message) const { environment_.log(message); }
    bool locate_roots();
    void refresh_memory();
    void update_native_edit_error();
    void invalidate_session();
    void poll_rule_context();
    void verify_rule_context();
    void update_rule_context();
    bool needs_rule_binding() const;
    void lock_rules_if_readable();
    EditResult write_change(const RelicOwner &owner, std::size_t slot, const Relic &expected,
                            Effects replacement);
    struct UndoChange {
        std::size_t slot;
        Relic before;
        Relic after;
    };
    struct Undo {
        RelicOwner owner;
        std::vector<UndoChange> changes;
    };
    std::optional<Undo> undo_;
    RuntimeEnvironment environment_;
    std::filesystem::path journal_path_;
    std::string hotkey_name_ = "F8";
    std::shared_ptr<const RelicValidator> supplied_rules_ =
        std::make_shared<const RelicValidator>();
    std::unique_ptr<ActiveRegulation> regulation_;
    EditorSession editor_;
    RuleContext rule_context_ = UnknownRules{"Waiting for the game data."};
    std::shared_ptr<const GameAssets> assets_ = std::make_shared<const GameAssets>();
    SessionId session_;
    std::string last_rule_diagnostics_;
    std::optional<std::string> native_edit_error_;
    std::optional<RelicRoots> roots_;
    std::optional<RelicOwner> owner_;
    EditLocation location_ = EditLocation::Unavailable;
    Inspection inspection_ = InspectionMessage{"Waiting for the game to load."};
    std::chrono::steady_clock::time_point next_scan_{};
    std::chrono::steady_clock::time_point next_read_{};
};

} // namespace ern
