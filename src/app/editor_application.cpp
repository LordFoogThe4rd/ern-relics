#include "app/editor_application.hpp"
#include "data/system_fonts.hpp"
#include <algorithm>

namespace ern {
EditorApplication &application() {
    // The DLL and its hooks are pinned until process exit.
    static auto *instance = new EditorApplication;
    return *instance;
}
EditorApplication::~EditorApplication() {
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
    log_.flush();
}
void EditorApplication::initialize(RuntimeEnvironment environment) {
    if (worker_.joinable())
        throw std::logic_error("The running editor cannot be reconfigured.");
    log_.set_path(environment.directory / "ern-relics.log");
    store_ = PresetStore{environment.directory / "presets"};
    environment.log = [this](std::string_view message) { log(message); };
    environment_ = std::move(environment);
}
void EditorApplication::start() {
    if (worker_.joinable())
        return;
    if (!environment_)
        throw std::logic_error("Configure the editor before starting its worker.");
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
void EditorApplication::log(std::string_view message) {
    log_.enqueue(message);
    wake_.notify_all();
}
std::optional<OperationId> EditorApplication::submit(const EditorView &observed,
                                                     EditorAction action) {
    std::lock_guard lock(mutex_);
    if (phase_.load() != Phase::Idle)
        return std::nullopt;
    const auto *loaded = std::get_if<EquippedRelics>(&observed.inspection);
    const OperationId operation{++next_operation_};
    request_ = Request{operation, observed.session, loaded ? std::optional{*loaded} : std::nullopt,
                       observed.drafts, std::move(action)};
    phase_.store(Phase::Busy);
    wake_.notify_all();
    return operation;
}
void EditorApplication::run(std::stop_token stop) {
    try {
        log_.flush();
        runtime_.initialize(std::move(*environment_));
        environment_.reset();
        log_.flush();
        runtime_.prepare_assets();
        fonts_ = std::make_shared<const SystemFonts>(read_system_fonts());
        runtime_.refresh();
        reload_presets();
        publish();
        phase_.store(Phase::Idle);
        while (!stop.stop_requested()) {
            std::optional<Request> request;
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, stop, std::chrono::milliseconds(100),
                               [this] { return request_.has_value(); });
                if (stop.stop_requested())
                    break;
                request = std::move(request_);
                request_.reset();
            }
            if (request) {
                EditorOutcome outcome;
                try {
                    outcome = execute(*request);
                } catch (const std::exception &error) {
                    outcome = OperationFailed{error.what()};
                }
                completion_ =
                    EditorCompletion{request->operation, request->action, std::move(outcome)};
            } else {
                const auto session = runtime_.session();
                runtime_.poll();
                if (runtime_.session() != session)
                    completion_.reset();
            }
            publish();
            if (request)
                phase_.store(Phase::Idle);
            log_.flush();
        }
    } catch (const std::exception &error) {
        log(std::string("Application worker stopped: ") + error.what());
        publish(std::string("The editor stopped: ") + error.what());
    }
    phase_.store(Phase::Stopped);
    log_.flush();
}
const EquippedRelics *EditorApplication::equipped() const {
    return std::get_if<EquippedRelics>(&runtime_.inspection());
}
EditorOutcome EditorApplication::execute(const Request &request) {
    const bool independent = std::holds_alternative<RefreshInspection>(request.action) ||
                             std::holds_alternative<ReloadPresets>(request.action) ||
                             std::holds_alternative<DeletePreset>(request.action);
    if (!independent) {
        runtime_.synchronize();
        const auto *current = equipped();
        const auto observed = current ? std::optional{*current} : std::nullopt;
        if (request.session != runtime_.session() || observed != request.equipped)
            return EditRejected{
                EditRejectReason::Stale,
                "The character or equipped relics changed. Review the refreshed state."};
        const bool draft_bound = std::holds_alternative<SetEffect>(request.action) ||
                                 std::holds_alternative<ReloadSlot>(request.action) ||
                                 std::holds_alternative<DiscardDrafts>(request.action) ||
                                 std::holds_alternative<ApplyDrafts>(request.action) ||
                                 std::holds_alternative<LoadPreset>(request.action);
        if (draft_bound && runtime_.editor().drafts() != request.drafts)
            return EditRejected{EditRejectReason::Stale,
                                "Drafts changed. Review them before applying."};
    }
    return std::visit([this](const auto &action) { return handle(action); }, request.action);
}
EditorOutcome EditorApplication::handle(const RefreshInspection &) {
    runtime_.refresh();
    return {};
}
EditorOutcome EditorApplication::handle(const SetEffect &action) {
    if (action.slot >= relic_slot_count || action.effect.row >= effects_per_relic ||
        !runtime_.editor().drafts()[action.slot])
        return EditRejected{EditRejectReason::Unreadable, {}};
    if (!runtime_.editing_supported())
        return EditRejected{EditRejectReason::RulesUnavailable, runtime_.editing_lock_reason()};
    const auto &draft = *runtime_.editor().drafts()[action.slot];
    if (!equipped() || !equipped()->slots[action.slot] ||
        *equipped()->slots[action.slot] != draft.original)
        return EditRejected{EditRejectReason::Stale, {}};
    if (const auto *reason = relic_lock_reason(runtime_.items().find(draft.original.item_id)))
        return EditRejected{EditRejectReason::Locked, reason};
    runtime_.editor().set_effect(action.slot, action.effect, action.id);
    return {};
}
EditorOutcome EditorApplication::handle(const ReloadSlot &action) {
    if (action.slot >= relic_slot_count || !equipped())
        return EditRejected{EditRejectReason::Unreadable, {}};
    runtime_.editor().reload(action.slot, *equipped());
    return {};
}
EditorOutcome EditorApplication::handle(const DiscardDrafts &) {
    runtime_.editor().discard();
    if (const auto *current = equipped())
        runtime_.editor().sync(*current);
    return {};
}
EditorOutcome EditorApplication::handle(const ApplyDrafts &action) {
    return AppliedDrafts{runtime_.apply_drafts(action.invalid)};
}
EditorOutcome EditorApplication::handle(const UndoEdit &) { return UndoneEdit{runtime_.undo()}; }
void EditorApplication::reload_presets() {
    auto result = store_.list();
    preset_error_.reset();
    if (const auto *error = std::get_if<PresetStoreError>(&result)) {
        preset_error_ = *error;
        presets_ = std::make_shared<const std::vector<SavedPreset>>();
    } else
        presets_ = std::make_shared<const std::vector<SavedPreset>>(
            std::get<std::vector<SavedPreset>>(std::move(result)));
}
EditorOutcome EditorApplication::handle(const ReloadPresets &) {
    reload_presets();
    return preset_error_ ? EditorOutcome{*preset_error_} : EditorOutcome{};
}
EditorOutcome EditorApplication::handle(const SavePreset &action) {
    if (!equipped() || !runtime_.editing_supported())
        return EditRejected{EditRejectReason::Unreadable,
                            "Equipped relics are unavailable for saving."};
    const auto preset = capture_preset(*equipped(), runtime_.validator(), runtime_.rule_context());
    const auto count =
        static_cast<std::size_t>(std::count_if(preset.slots.begin(), preset.slots.end(),
                                               [](const auto &slot) { return slot.has_value(); }));
    if (!count)
        return EditRejected{EditRejectReason::Locked,
                            "No obtainable relics are equipped. Nothing was saved."};
    if (const auto error = store_.save(action.name, preset, action.mode)) {
        if (*error == PresetStoreError::AlreadyExists)
            reload_presets();
        return *error;
    }
    reload_presets();
    return SavedPresetResult{action.name, count};
}
EditorOutcome EditorApplication::handle(const DeletePreset &action) {
    if (const auto error = store_.remove(action.name))
        return *error;
    reload_presets();
    return DeletedPresetResult{};
}
EditorOutcome EditorApplication::handle(const LoadPreset &action) {
    if (!equipped() || !runtime_.editing_supported())
        return EditRejected{EditRejectReason::Unreadable,
                            "Equipped relics are unavailable for staging."};
    const auto count = static_cast<std::size_t>(
        std::count_if(action.preset.slots.begin(), action.preset.slots.end(),
                      [](const auto &slot) { return slot.has_value(); }));
    return LoadedPresetResult{runtime_.editor().load_preset(action.preset, *equipped(),
                                                            runtime_.validator(),
                                                            runtime_.rule_context()),
                              count};
}
void EditorApplication::publish(std::string error) {
    auto next = std::make_shared<EditorView>();
    next->session = runtime_.session();
    next->rules = runtime_.rules();
    next->resources = runtime_.shared_assets();
    next->fonts = fonts_;
    next->inspection = runtime_.inspection();
    next->drafts = runtime_.editor().drafts();
    next->context = runtime_.rule_context();
    next->review =
        runtime_.editor().review(equipped(), runtime_.validator(), runtime_.rule_context());
    next->location = runtime_.edit_location();
    next->rule_diagnostics = runtime_.rule_diagnostics();
    if (const auto *reason = runtime_.editing_lock_reason())
        next->lock_reason = reason;
    if (!error.empty()) {
        next->application_error = error;
        next->lock_reason = std::move(error);
    }
    next->version = runtime_.version();
    next->source_label = runtime_.source_label();
    next->source_hash = runtime_.source_hash();
    next->hotkey_name = runtime_.hotkey_name();
    next->journal_path = runtime_.journal_path();
    next->hotkey = runtime_.hotkey();
    next->open_on_start = runtime_.open_on_start();
    next->can_undo = runtime_.can_undo();
    next->presets = presets_;
    next->preset_error = preset_error_;
    next->completion = completion_;
    view_.store(std::move(next));
}
} // namespace ern
