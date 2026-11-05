#include "app/editor_application.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/regulation_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include <algorithm>
#include <fstream>
#include <future>

namespace {
using namespace std::chrono_literals;
using View = std::shared_ptr<const ern::EditorView>;

struct Fixture {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("application-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::mutex memory_mutex;
    test::Fixture memory;
    std::atomic<unsigned int> writes = 0;
    std::atomic<bool> caller_access = false;
    const std::thread::id caller = std::this_thread::get_id();

    Fixture() { std::filesystem::create_directories(directory); }
    ~Fixture() {
        std::error_code ignored;
        for (const auto *name :
             {L"presets/demo.json", L"presets/\u6d4b\u8bd5.txt", L"presets/\u6d4b\u8bd5.json",
              L"presets", L"regulation.bin", L"relic-edits.jsonl", L"ern-relics.log"})
            std::filesystem::remove(directory / name, ignored);
        std::filesystem::remove(directory, ignored);
    }
    ern::RuntimeEnvironment environment() {
        ern::RuntimeEnvironment value;
        value.directory = directory;
        value.layout_verified = true;
        value.rules = synthetic_rules();
        value.assets.item_names[102] = "Fixture relic";
        value.read = [this](auto address, auto output) {
            if (std::this_thread::get_id() == caller)
                caller_access = true;
            std::lock_guard lock(memory_mutex);
            return memory.read(address, output);
        };
        value.write = [this](ern::Address address, std::span<const std::byte> input) {
            if (std::this_thread::get_id() == caller)
                caller_access = true;
            test::require(journal_size() > 0, "Memory changed before its journal was flushed");
            std::lock_guard lock(memory_mutex);
            ++writes;
            std::memcpy(memory.bytes.data() + address - test::Fixture::base, input.data(),
                        input.size());
            return true;
        };
        value.locate_roots = [this] {
            return std::variant<ern::RelicRoots, std::string>{memory.roots};
        };
        value.dlc = [] { return ern::DlcState::Unknown; };
        return value;
    }
    std::uintmax_t journal_size() const {
        std::error_code error;
        const auto size = std::filesystem::file_size(directory / "relic-edits.jsonl", error);
        return error ? 0 : size;
    }
    auto bytes() {
        std::lock_guard lock(memory_mutex);
        return memory.bytes;
    }
    template <typename T> void put(ern::Address address, const T &value) {
        std::lock_guard lock(memory_mutex);
        memory.put(address, value);
    }
};

template <typename Ready> View await(ern::EditorApplication &app, Ready ready) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    do {
        if (auto view = app.view(); view && ready(*view))
            return view;
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("The application worker did not publish the expected result");
}
View idle(ern::EditorApplication &app) {
    return await(app, [&](const auto &) { return !app.pending(); });
}
View request(ern::EditorApplication &app, ern::EditorAction action, View observed = {}) {
    const auto current = idle(app);
    const auto operation = app.submit(*(observed ? observed : current), std::move(action));
    test::require(operation.has_value(), "An idle application refused a command");
    return await(app, [&](const auto &view) {
        return !app.pending() && view.completion && view.completion->operation == *operation;
    });
}
const ern::EditorOutcome &outcome(const View &view) { return view->completion->outcome; }
void require_stale(const View &view) {
    const auto *rejected = std::get_if<ern::EditRejected>(&outcome(view));
    test::require(rejected && rejected->reason == ern::EditRejectReason::Stale,
                  "An outdated review retained authority to change the editor");
}
void require_applied(const View &view, std::size_t count = 1) {
    const auto *applied = std::get_if<ern::AppliedDrafts>(&outcome(view));
    test::require(applied && !applied->result.failure && applied->result.applied == count,
                  "A valid application command applied the wrong number of relics");
}

void unsupported_preset_filenames() {
    Fixture fixture;
    const auto directory = fixture.directory / "presets";
    std::filesystem::create_directory(directory);
    const ern::Preset preset{};
    for (const auto *name : {L"demo.json", L"\u6d4b\u8bd5.txt", L"\u6d4b\u8bd5.json"}) {
        std::ofstream file(directory / name);
        file << ern::serialize_preset(preset);
        test::require(bool(file), "Could not write a startup preset filename fixture");
    }
    ern::EditorApplication app;
    app.initialize(fixture.environment());
    app.start();
    const auto initial = await(
        app, [&](const auto &view) { return !app.pending() || !view.application_error.empty(); });
    test::require(initial->application_error.empty() && initial->editing_supported() &&
                      !initial->preset_error && initial->presets->size() == 1 &&
                      initial->presets->front().name == "demo" &&
                      initial->presets->front().contents == preset,
                  "Unsupported Unicode filenames stopped startup or hid a valid preset");
    const auto reloaded = request(app, ern::ReloadPresets{});
    test::require(!reloaded->preset_error && reloaded->presets->size() == 1 &&
                      request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400})
                              ->review.relic_count == 1,
                  "The worker stopped handling commands after scanning Unicode filenames");
}

void workflows() {
    Fixture fixture;
    View retained;
    {
        ern::EditorApplication app;
        app.initialize(fixture.environment());
        app.start();
        retained = idle(app);
        app.start(); // Repeated startup does not reinitialize a running worker.
        test::require(retained->editing_supported() && retained->review.relic_count == 0,
                      "The worker did not publish a usable initial view");
        const auto initial_memory = fixture.bytes();
        const auto saved = request(app, ern::SavePreset{"demo"});
        test::require(std::holds_alternative<ern::SavedPresetResult>(outcome(saved)) &&
                          saved->presets->size() == 1 && retained->presets->empty(),
                      "Saving failed or mutated an already published preset list");
        const auto preset = *saved->presets->front().contents;
        const auto duplicate = request(app, ern::SavePreset{"DEMO"});
        test::require(std::get<ern::PresetStoreError>(outcome(duplicate)) ==
                              ern::PresetStoreError::AlreadyExists &&
                          duplicate->presets->size() == 1 &&
                          duplicate->presets->front().contents == preset,
                      "An implicit save replaced an existing preset through a case alias");
        auto view = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
        test::require(view->review.relic_count == 1 && retained->review.relic_count == 0 &&
                          retained->drafts[0]->effects.rows[0].effect == 7000401,
                      "Draft staging changed an older immutable view");
        const auto reviewed = view;
        view = request(app, ern::SetEffect{0, {1, ern::EffectField::Effect}, 7000601});
        require_stale(request(app, ern::ApplyDrafts{}, reviewed));
        test::require(fixture.writes == 0 && fixture.journal_size() == 0,
                      "An obsolete draft review reached persistence");
        view = request(app, ern::DiscardDrafts{});
        view = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
        view = request(app, ern::SetEffect{1, {0, ern::EffectField::Effect}, 7000400});
        require_applied(request(app, ern::ApplyDrafts{}), 2);
        view = request(app, ern::SavePreset{"demo"});
        test::require(std::get<ern::PresetStoreError>(outcome(view)) ==
                              ern::PresetStoreError::AlreadyExists &&
                          view->presets->front().contents == preset,
                      "A rejected create changed an existing preset after editing");
        view = request(app, ern::SavePreset{"demo", ern::PresetSaveMode::Replace});
        test::require(std::holds_alternative<ern::SavedPresetResult>(outcome(view)) &&
                          view->presets->front().contents != preset &&
                          saved->presets->front().contents == preset,
                      "Explicit replacement failed or changed an already published preset list");
        view = request(app, ern::UndoEdit{});
        const auto &undone = std::get<ern::UndoneEdit>(outcome(view)).result;
        test::require(!undone.failure && undone.restored == 2 && !view->can_undo &&
                          fixture.bytes() == initial_memory,
                      "Undo did not restore memory through the application boundary");
        view = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
        view = request(app, ern::LoadPreset{preset});
        test::require(std::holds_alternative<ern::LoadedPresetResult>(outcome(view)) &&
                          view->review.relic_count == 0,
                      "Loading a preset failed to replace drafts without writing memory");
        view = request(app, ern::SavePreset{"../invalid"});
        test::require(std::get<ern::PresetStoreError>(outcome(view)) ==
                          ern::PresetStoreError::InvalidName,
                      "The application lost typed preset storage errors");
        view = request(app, ern::DeletePreset{"demo"});
        test::require(view->presets->empty() && saved->presets->size() == 1,
                      "Deleting a preset mutated a retained view");
        request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
        const auto applied = request(app, ern::ApplyDrafts{});
        require_applied(applied);
        fixture.put(test::Fixture::base + 0x308, ern::Address{0});
        const auto unloaded =
            await(app, [&](const auto &next) { return next.session != applied->session; });
        test::require(!unloaded->completion,
                      "An observed character unload retained the previous command result");
        test::require(!fixture.caller_access, "Memory access escaped onto the calling thread");
    }
    test::require(retained->items().find(0x80000000u | 102u) &&
                      retained->assets().item_names.at(102) == "Fixture relic",
                  "A retained view referenced destroyed application resources");
}

void stale_equipment_and_sessions() {
    Fixture fixture;
    ern::EditorApplication app;
    app.initialize(fixture.environment());
    app.start();
    auto reviewed = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
    fixture.put(test::Fixture::base + 0x3000 + 0x18,
                ern::Effects{{7000401, UINT32_MAX}, {7000601, UINT32_MAX}, {7001802, UINT32_MAX}});
    require_stale(request(app, ern::ApplyDrafts{}, reviewed));
    request(app, ern::DiscardDrafts{});
    reviewed = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
    fixture.put(test::Fixture::base + 0x1000 + ern::game_layout::equipped_relics,
                std::uint32_t{0xC0800001});
    require_stale(request(app, ern::ApplyDrafts{}, reviewed));
    test::require(fixture.writes == 0 && fixture.journal_size() == 0,
                  "A queued command accepted a reused inventory index with a new handle");
    request(app, ern::DiscardDrafts{});
    reviewed = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
    fixture.put(test::Fixture::base + 0x308, ern::Address{0});
    request(app, ern::RefreshInspection{});
    fixture.put(test::Fixture::base + 0x308, test::Fixture::base + 0x1000);
    const auto reloaded = request(app, ern::RefreshInspection{});
    test::require(reloaded->session != reviewed->session && reloaded->review.relic_count == 0,
                  "An observed unload retained drafts when owner addresses were reused");
    require_stale(request(app, ern::ApplyDrafts{}, reviewed));
    test::require(fixture.writes == 0 && fixture.journal_size() == 0,
                  "Stale equipment or a stale session changed memory or the journal");
}

void discard_rebases_current_equipment() {
    for (const int change : {0, 1, 2}) {
        Fixture fixture;
        ern::EditorApplication app;
        app.initialize(fixture.environment());
        app.start();
        auto view = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
        const auto handle = view->drafts[0]->original.handle;
        if (change == 0)
            fixture.put(test::Fixture::base + 0x3000 + ern::game_layout::item_effects,
                        std::uint32_t{7000402});
        else
            fixture.put(test::Fixture::base + 0x1000 + ern::game_layout::equipped_relics,
                        change == 1 ? handle ^ 0x10000u : 0u);
        view = request(app, ern::RefreshInspection{});
        test::require(view->review.blocked &&
                          view->review.blocked->rejection.reason == ern::EditRejectReason::Stale,
                      "External equipment change did not preserve and block the dirty draft");
        const auto unchanged_memory = fixture.bytes();
        const auto discarded = request(app, ern::DiscardDrafts{}, view);
        const auto &equipped = std::get<ern::EquippedRelics>(discarded->inspection);
        test::require(std::holds_alternative<std::monostate>(outcome(discarded)) &&
                          discarded->review.relic_count == 0 && !discarded->review.blocked &&
                          (equipped.slots[0]
                               ? discarded->drafts[0] &&
                                     discarded->drafts[0]->original == *equipped.slots[0] &&
                                     discarded->drafts[0]->effects == equipped.slots[0]->effects
                               : !discarded->drafts[0]),
                      "Discard published a clean draft for the previous relic or empty slot");
        if (equipped.slots[0]) {
            const auto selected =
                request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400}, discarded);
            test::require(std::holds_alternative<std::monostate>(outcome(selected)) &&
                              selected->review.relic_count == 1,
                          "The first selection after Discard rejected its freshly published draft");
        }
        test::require(fixture.bytes() == unchanged_memory && fixture.writes == 0 &&
                          fixture.journal_size() == 0,
                      "Discard or its next selection changed memory or the journal");
    }

    Fixture fixture;
    ern::EditorApplication app;
    app.initialize(fixture.environment());
    app.start();
    auto view = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
    const auto session = view->session;
    const auto original = view->drafts[0]->original;
    const auto record_pointer = test::Fixture::base + 0x100 + ern::game_layout::inventory_entries +
                                original.inventory_index() * ern::game_layout::inventory_stride;
    fixture.put(record_pointer, ern::Address{1});
    view = request(app, ern::RefreshInspection{});
    test::require(std::holds_alternative<ern::InspectionMessage>(view->inspection) &&
                      view->session == session && view->review.relic_count == 1,
                  "An unreadable inventory unexpectedly unloaded or discarded the draft");
    view = request(app, ern::DiscardDrafts{}, view);
    test::require(std::holds_alternative<std::monostate>(outcome(view)) &&
                      view->review.relic_count == 0 && view->drafts[0] &&
                      view->drafts[0]->effects == original.effects,
                  "Discard lost its intent while equipment was unreadable");
    fixture.put(record_pointer, test::Fixture::base + 0x3000);
    fixture.put(test::Fixture::base + 0x3000 + ern::game_layout::item_effects,
                std::uint32_t{7000402});
    view = request(app, ern::RefreshInspection{});
    const auto &equipped = std::get<ern::EquippedRelics>(view->inspection);
    test::require(view->session == session && view->review.relic_count == 0 &&
                      view->drafts[0]->original == *equipped.slots[0] &&
                      view->drafts[0]->effects == equipped.slots[0]->effects &&
                      fixture.writes == 0 && fixture.journal_size() == 0,
                  "A discarded unreadable draft did not rebase when equipment became readable");
}

void blocked_io_and_fresh_checks() {
    Fixture fixture;
    const auto source = fixture.directory / "regulation.bin";
    {
        const auto bytes = test::regulation::rolled_relics();
        std::ofstream file(source, std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    std::mutex gate;
    std::condition_variable changed;
    bool blocked = false, entered = false;
    std::atomic<bool> refuse = false;
    auto environment = fixture.environment();
    environment.rules = ern::FileRuleSource{
        source, [&](const auto &path) -> std::variant<ern::FileContent, ern::FileError> {
            {
                std::unique_lock lock(gate);
                if (blocked) {
                    entered = true;
                    changed.notify_all();
                    changed.wait_for(lock, 3s, [&] { return !blocked; });
                }
            }
            return refuse ? std::variant<ern::FileContent, ern::FileError>{ern::FileError{
                                "Injected source failure"}}
                          : ern::read_file(path);
        }};
    ern::EditorApplication app;
    app.initialize(std::move(environment));
    app.start();
    const auto reviewed = request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
    {
        std::lock_guard lock(gate);
        blocked = true;
    }
    const auto operation = app.submit(*reviewed, ern::ApplyDrafts{});
    bool reader_blocked;
    {
        std::unique_lock lock(gate);
        reader_blocked = changed.wait_for(lock, 2s, [&] { return entered; });
    }
    auto drawing = std::async(std::launch::async, [&] {
        for (int i = 0; i < 100; ++i)
            test::require(app.view()->items().find(0x80000000u | 102u) != nullptr,
                          "Published rules disappeared");
        app.log("A hook can enqueue a log while storage is blocked");
        return !app.submit(*reviewed, ern::ReloadPresets{}).has_value();
    });
    const bool responsive = drawing.wait_for(1s) == std::future_status::ready;
    {
        std::lock_guard lock(gate);
        blocked = false;
    }
    changed.notify_all();
    const bool refused_second = drawing.get();
    test::require(operation && reader_blocked && responsive && refused_second,
                  "Blocked storage stalled drawing/logging or accepted overlapping commands");
    const auto applied = await(app, [&](const auto &view) {
        return !app.pending() && view.completion && view.completion->operation == *operation;
    });
    require_applied(applied);
    const auto &effects = applied->drafts[0]->effects;
    const auto effect = std::find_if(effects.rows.begin(), effects.rows.end(),
                                     [](const auto &row) { return row.effect == 7000400u; });
    test::require(effect != effects.rows.end(),
                  "The applied effect disappeared after canonical ordering");
    const auto staged =
        request(app, ern::SetEffect{0,
                                    {static_cast<std::size_t>(effect - effects.rows.begin()),
                                     ern::EffectField::Effect},
                                    7000401});
    test::require(staged->review.relic_count == 1 && !staged->review.blocked,
                  "The source-failure test did not begin with a valid pending edit");
    const auto before = fixture.bytes();
    const auto journal_size = fixture.journal_size();
    refuse = true;
    const auto rejected = request(app, ern::ApplyDrafts{}, staged);
    const auto &result = std::get<ern::AppliedDrafts>(outcome(rejected)).result;
    test::require(result.failure &&
                      std::get<ern::EditRejected>(result.failure->failure).reason ==
                          ern::EditRejectReason::RulesUnavailable &&
                      fixture.bytes() == before && fixture.writes == 1 &&
                      fixture.journal_size() == journal_size,
                  "A failed fresh source check authorized a cached edit or journal entry");
    refuse = false;
    test::require(request(app, ern::RefreshInspection{})->editing_supported(),
                  "A recovered source could not unlock the unchanged rules");
    test::require(!fixture.caller_access, "Storage commands used memory on the calling thread");
}

void boundary_failures() {
    Fixture fixture;
    {
        auto environment = fixture.environment();
        environment.journal_name = "missing/edits.jsonl";
        ern::EditorApplication app;
        app.initialize(std::move(environment));
        app.start();
        request(app, ern::SetEffect{0, {0, ern::EffectField::Effect}, 7000400});
        const auto rejected = request(app, ern::ApplyDrafts{});
        const auto &result = std::get<ern::AppliedDrafts>(outcome(rejected)).result;
        test::require(result.failure &&
                          std::get<ern::EditRejected>(result.failure->failure).reason ==
                              ern::EditRejectReason::JournalOpen &&
                          fixture.writes == 0,
                      "A journal failure crossed the application write boundary");
    }
    {
        std::ofstream file(fixture.directory / "presets");
        file << "This is a file, not a preset directory.";
    }
    {
        ern::EditorApplication app;
        app.initialize(fixture.environment());
        app.start();
        const auto view = idle(app);
        test::require(view->preset_error == ern::PresetStoreError::Read && view->presets->empty(),
                      "An unreadable preset directory was presented as an empty success");
    }
    {
        auto environment = fixture.environment();
        environment.locate_roots = []() -> std::variant<ern::RelicRoots, std::string> {
            throw std::runtime_error("Injected startup failure");
        };
        ern::EditorApplication app;
        app.initialize(std::move(environment));
        app.start();
        const auto stopped =
            await(app, [](const auto &view) { return !view.application_error.empty(); });
        test::require(app.pending() && !stopped->editing_supported() &&
                          !app.submit(*stopped, ern::RefreshInspection{}),
                      "A stopped worker accepted commands or retained editing authority");
    }
}
} // namespace

int main() {
    return test::run(
        "application workflows, immutable views, stale commands, nonblocking IO and boundary failures",
        [] {
            unsupported_preset_filenames();
            workflows();
            stale_equipment_and_sessions();
            discard_rebases_current_equipment();
            blocked_io_and_fresh_checks();
            boundary_failures();
        });
}
