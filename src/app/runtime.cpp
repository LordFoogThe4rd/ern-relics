#include "app/runtime.hpp"
#include <Windows.h>
#include "data/edit_journal.hpp"
#include "data/game_assets.hpp"
#include "data/catalog_presentation.hpp"

namespace ern {
void Runtime::initialize(RuntimeEnvironment environment) {
    environment_ = std::move(environment);
    journal_path_ = directory() / environment_.journal_name;
    log("ERN Relics " ERN_VERSION " starting");
    log("Executable version: " + version());
    hotkey_name_ = "F" + std::to_string(hotkey() - VK_F1 + 1);
    assets_ = std::make_shared<const GameAssets>(std::move(environment_.assets));
    if (auto *file = std::get_if<FileRuleSource>(&environment_.rules))
        regulation_ = std::make_unique<ActiveRegulation>(file->path, environment_.game_directory,
                                                         std::move(file->read));
    else
        supplied_rules_ = std::make_shared<const RelicValidator>(
            std::get<RegulationRules>(std::move(environment_.rules)));
    verify_rule_context();
    if (regulation_)
        regulation_->start();
    log("Effect catalogue entries: " + std::to_string(catalog().size()));
    log("Relic item definitions: " + std::to_string(items().size()));
    log("Relic rule source SHA-256: " + source_hash());
    if (const auto *unknown = std::get_if<UnknownRules>(&rule_context_))
        log("Rules unavailable: " + unknown->reason);
}

bool Runtime::locate_roots() {
    const auto found = environment_.locate_roots();
    if (const auto *roots = std::get_if<RelicRoots>(&found)) {
        roots_ = *roots;
        return true;
    }
    inspection_ = InspectionMessage{std::get<std::string>(found)};
    return false;
}

void Runtime::poll() {
    update_native_edit_error();
    if (std::chrono::steady_clock::now() < next_read_)
        return;
    poll_rule_context();
    refresh_memory();
    if (needs_rule_binding()) {
        verify_rule_context();
        lock_rules_if_readable();
    }
}

void Runtime::refresh() {
    update_native_edit_error();
    verify_rule_context();
    next_scan_ = {};
    refresh_memory();
    lock_rules_if_readable();
}

void Runtime::synchronize() {
    update_native_edit_error();
    poll_rule_context();
    next_scan_ = {};
    refresh_memory();
    if (needs_rule_binding()) {
        verify_rule_context();
        lock_rules_if_readable();
    }
}

bool Runtime::needs_rule_binding() const {
    return regulation_ && !regulation_->locked() &&
           std::holds_alternative<EquippedRelics>(inspection_) &&
           std::holds_alternative<DlcState>(rule_context_);
}

void Runtime::lock_rules_if_readable() {
    if (needs_rule_binding())
        regulation_->lock_source();
}

void Runtime::invalidate_session() {
    ++session_.value;
    owner_.reset();
    editor_ = {};
    undo_.reset();
}

void Runtime::refresh_memory() {
    const auto now = std::chrono::steady_clock::now();
    next_read_ = now + std::chrono::milliseconds(500);
    location_ = EditLocation::Unavailable;
    if (!roots_) {
        if (now < next_scan_)
            return;
        next_scan_ = now + std::chrono::seconds(5);
        if (!locate_roots())
            return;
    }
    location_ = read_edit_location(environment_.read, *roots_);
    const auto result = location_ == EditLocation::NotLoaded
                            ? RelicReadResult{ReadFailure::NotLoaded}
                            : read_equipped(environment_.read, *roots_, equipped_offset());
    if (const auto *snapshot = std::get_if<RelicSnapshot>(&result)) {
        if (owner_ != snapshot->owner)
            invalidate_session();
        owner_ = snapshot->owner;
        inspection_ = snapshot->relics;
        editor_.sync(snapshot->relics);
    } else {
        location_ = EditLocation::Unavailable;
        switch (std::get<ReadFailure>(result)) {
        case ReadFailure::NotLoaded:
            if (owner_) {
                invalidate_session();
                log("Character unloaded. Drafts and Undo cleared.");
            }
            location_ = EditLocation::NotLoaded;
            inspection_ = InspectionMessage{
                "Load your character and enter Roundtable Hold to inspect relics."};
            break;
        case ReadFailure::Unreadable:
            inspection_ = InspectionMessage{
                "Load your character to inspect relics. If this remains after loading, the relic layout may not match this patch."};
            break;
        case ReadFailure::Changed:
            inspection_ = InspectionMessage{"Equipment changed during the read. Refreshing..."};
            break;
        }
    }
}

bool Runtime::editing_supported() const { return editing_lock_reason() == nullptr; }

void Runtime::update_native_edit_error() {
    auto error = environment_.native_edit_error ? environment_.native_edit_error() : std::nullopt;
    if (error && error != native_edit_error_)
        log("Native relic editing unavailable: " + *error);
    native_edit_error_ = std::move(error);
}

const char *Runtime::editing_lock_reason() const {
    if (!environment_.edit_lock.empty())
        return environment_.edit_lock.c_str();
    if (native_edit_error_)
        return native_edit_error_->c_str();
    if (const auto *unknown = std::get_if<UnknownRules>(&rule_context_))
        return unknown->reason.c_str();
    if (!roots_)
        return "Wait until the equipped relics are readable.";
    return location_lock_reason(location_);
}

const std::string &Runtime::source_hash() const {
    static const std::string empty;
    return regulation_ ? regulation_->source_hash() : empty;
}

const RegulationDiagnostics &Runtime::rule_diagnostics() const {
    static const RegulationDiagnostics synthetic;
    return regulation_ ? regulation_->diagnostics() : synthetic;
}

void Runtime::poll_rule_context() {
    if (regulation_)
        regulation_->poll();
    update_rule_context();
}

void Runtime::verify_rule_context() {
    if (regulation_)
        regulation_->verify();
    update_rule_context();
}

void Runtime::update_rule_context() {
    if (!environment_.layout_verified)
        rule_context_ = UnknownRules{"Editing requires game build " +
                                     std::string(game_layout::executable_version) +
                                     " and its default relic layout."};
    else
        rule_context_ = regulation_ ? regulation_->context(environment_.dlc())
                                    : RuleContext{environment_.dlc()};
    if (!regulation_)
        return;
    const auto &diagnostic = rule_diagnostics();
    const auto path_text = [](const std::filesystem::path &path) {
        const auto utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    };
    const auto summary = "Regulation requested: " + path_text(diagnostic.requested_path) +
                         "\nRegulation opened: " + path_text(diagnostic.resolved_path) +
                         "\nActive regulation SHA-256: " + diagnostic.detected_hash +
                         "\nValidation rules SHA-256: " + source_hash() + "\nRegulation status: " +
                         (diagnostic.error.empty() ? diagnostic.detected_hash == source_hash()
                                                         ? "Rules match the active regulation."
                                                         : "Rules not checked yet."
                                                   : diagnostic.error);
    if (summary != last_rule_diagnostics_) {
        last_rule_diagnostics_ = summary;
        log(summary);
    }
}

void Runtime::prepare_assets() {
    if (!environment_.game_directory.empty()) {
        GameAssets loaded;
        try {
            loaded = read_game_assets(environment_.game_directory, requested_icons(validator()));
        } catch (const std::exception &error) {
            loaded.warnings.push_back(error.what());
        }
        assets_ = std::make_shared<const GameAssets>(std::move(loaded));
    }
    log("Local game assets: " + std::to_string(assets().item_names.size()) + " item names, " +
        std::to_string(assets().effect_names.size()) + " effect names, " +
        std::to_string(assets().cells.size()) + " icon cells.");
    for (const auto &warning : assets().warnings)
        log("Asset warning: " + warning);
}

DraftApplyResult Runtime::apply_drafts(bool invalid) {
    const auto session = session_;
    const auto owner = owner_;
    const auto drafts = editor_.drafts();
    poll_rule_context();
    next_scan_ = {};
    refresh_memory();
    if (owner != owner_) {
        for (std::size_t i = 0; i < drafts.size(); ++i)
            if (drafts[i] && drafts[i]->effects != drafts[i]->original.effects)
                return {
                    0,
                    SlotFailure{
                        i,
                        EditRejected{
                            EditRejectReason::Stale,
                            "Character session changed. Previous drafts and Undo were cleared."}}};
    }
    const auto *equipped = std::get_if<EquippedRelics>(&inspection_);
    std::vector<UndoChange> changes;
    auto result = editor_.apply(
        equipped, validator(), rule_context_,
        [this, owner, invalid, &changes](auto slot, const auto &before,
                                         auto effects) -> EditResult {
            if (!owner)
                return EditRejected{EditRejectReason::Unreadable, {}};
            auto written = write_change(*owner, slot, before, effects, invalid);
            if (const auto *after = std::get_if<Relic>(&written))
                changes.push_back(UndoChange{slot, before, *after});
            return written;
        },
        invalid);
    refresh_memory();
    if (!changes.empty() && session_ == session && owner_ == owner)
        undo_ = Undo{*owner, std::move(changes), invalid};
    return result;
}

UndoResult Runtime::undo() {
    if (!undo_)
        return {};
    const auto previous = *undo_;
    UndoResult result;
    for (auto it = previous.changes.rbegin(); it != previous.changes.rend(); ++it) {
        const auto written =
            write_change(previous.owner, it->slot, it->after, it->before.effects, previous.invalid);
        if (std::holds_alternative<Relic>(written)) {
            ++result.restored;
            if (undo_)
                undo_->changes.pop_back();
        } else {
            result.failure = std::holds_alternative<EditRejected>(written)
                                 ? SlotFailure{it->slot, std::get<EditRejected>(written)}
                                 : SlotFailure{it->slot, std::get<WriteFailure>(written)};
            break;
        }
    }
    if (undo_ && undo_->changes.empty())
        undo_.reset();
    refresh_memory();
    return result;
}

EditResult Runtime::write_change(const RelicOwner &owner, std::size_t slot, const Relic &expected,
                                 Effects replacement, bool invalid) {
    update_native_edit_error();
    if (native_edit_error_)
        return EditRejected{EditRejectReason::EquipUnavailable, *native_edit_error_};
    verify_rule_context();
    lock_rules_if_readable();
    location_ = roots_ ? read_edit_location(environment_.read, *roots_) : EditLocation::Unavailable;
    if (const auto *unknown = std::get_if<UnknownRules>(&rule_context_))
        return EditRejected{EditRejectReason::RulesUnavailable, unknown->reason};
    if (!environment_.edit_lock.empty())
        return EditRejected{EditRejectReason::EquipUnavailable, environment_.edit_lock};
    if (!roots_)
        return EditRejected{EditRejectReason::Unreadable, {}};
    const auto observed = read_equipped(environment_.read, *roots_, equipped_offset());
    const auto *current = std::get_if<RelicSnapshot>(&observed);
    if (!current && std::get<ReadFailure>(observed) != ReadFailure::NotLoaded)
        return EditRejected{EditRejectReason::Unreadable, {}};
    if (!current || current->owner != owner) {
        invalidate_session();
        return EditRejected{EditRejectReason::Stale,
                            "Character session changed. Previous drafts and Undo were cleared."};
    }
    if (!current->relics.slots[slot] || *current->relics.slots[slot] != expected)
        return EditRejected{EditRejectReason::Stale, {}};
    if (location_ != EditLocation::RoundtableHold)
        return EditRejected{EditRejectReason::NotAtRoundtable, {}};
    if (const auto *reason = relic_lock_reason(items().find(expected.item_id)))
        return EditRejected{EditRejectReason::Locked, reason};
    if (!invalid) {
        const auto validation = validate(expected.item_id, replacement);
        const auto *valid = std::get_if<Obtainable>(&validation);
        if (!valid)
            return EditRejected{std::holds_alternative<UnknownRules>(validation)
                                    ? EditRejectReason::RulesUnavailable
                                    : EditRejectReason::Invalid,
                                validation_reason(validation)};
        replacement = valid->effects;
    }
    if (replacement == expected.effects)
        return EditRejected{EditRejectReason::Unchanged, {}};
    if (const auto error = record_edit(journal_path_, slot, expected, replacement))
        return EditRejected{*error == JournalError::Open ? EditRejectReason::JournalOpen
                                                         : EditRejectReason::JournalWrite,
                            {}};
    const auto result =
        environment_.commit
            ? environment_.commit(RelicEditRequest{*roots_, equipped_offset(), owner, slot,
                                                   expected, replacement, rules(), rule_context_,
                                                   invalid})
            : replace_equipped(environment_.read, environment_.write, *roots_, equipped_offset(),
                               owner, slot, expected, replacement, validator(), rule_context_, {},
                               invalid);
    if (std::holds_alternative<Relic>(result)) {
        log("Updated relic slot " + std::to_string(slot + 1) + ", inventory index " +
            std::to_string(expected.inventory_index()));
    }
    return result;
}
} // namespace ern
