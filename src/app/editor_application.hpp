#pragma once
#include "app/editor_view.hpp"
#include "app/event_log.hpp"
#include "app/runtime.hpp"
#include "data/preset_store.hpp"
#include <condition_variable>
#include <thread>

namespace ern {
// One worker owns Runtime and preset storage. Rendering only acquires immutable views.
class EditorApplication {
  public:
    ~EditorApplication();
    void initialize(RuntimeEnvironment environment);
    void start();
    std::shared_ptr<const EditorView> view() const { return view_.load(); }
    bool pending() const { return phase_.load() != Phase::Idle; }
    std::optional<OperationId> submit(const EditorView &observed, EditorAction action);
    void log(std::string_view message);
    void flush_startup_log() { log_.flush(); }

  private:
    struct Request {
        OperationId operation;
        SessionId session;
        std::optional<EquippedRelics> equipped;
        Drafts drafts;
        EditorAction action;
    };
    void run(std::stop_token stop);
    void publish(std::string error = {});
    void reload_presets();
    EditorOutcome execute(const Request &request);
    EditorOutcome handle(const RefreshInspection &);
    EditorOutcome handle(const SetEffect &);
    EditorOutcome handle(const ReloadSlot &);
    EditorOutcome handle(const DiscardDrafts &);
    EditorOutcome handle(const ApplyDrafts &);
    EditorOutcome handle(const UndoEdit &);
    EditorOutcome handle(const ReloadPresets &);
    EditorOutcome handle(const SavePreset &);
    EditorOutcome handle(const DeletePreset &);
    EditorOutcome handle(const LoadPreset &);
    const EquippedRelics *equipped() const;

    Runtime runtime_;
    std::optional<RuntimeEnvironment> environment_;
    std::shared_ptr<const SystemFonts> fonts_ = std::make_shared<const SystemFonts>();
    EventLog log_;
    PresetStore store_{std::filesystem::path{}};
    std::shared_ptr<const std::vector<SavedPreset>> presets_ =
        std::make_shared<const std::vector<SavedPreset>>();
    std::optional<PresetStoreError> preset_error_;
    std::optional<EditorCompletion> completion_;
    std::atomic<std::shared_ptr<const EditorView>> view_;
    enum class Phase { Starting, Idle, Busy, Stopped };
    std::atomic<Phase> phase_ = Phase::Starting;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Request> request_;
    std::uint64_t next_operation_ = 0;
    std::jthread worker_;
};
EditorApplication &application();
} // namespace ern
