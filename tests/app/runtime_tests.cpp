#include "app/runtime.hpp"
#include "data/rotating_file.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/regulation_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include <fstream>

int main() {
    return test::run("runtime adapters, one source check per write and journal rejection", [] {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("runtime-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto source = directory / "regulation.bin";
        const auto bytes = test::regulation::rolled_relics();
        {
            std::ofstream file(source, std::ios::binary);
            file.write(reinterpret_cast<const char *>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
        }
        test::Fixture memory;
        unsigned int writes = 0, foreground_reads = 0;
        const auto owner = std::this_thread::get_id();
        std::atomic<bool> refuse_source = false;
        const auto environment = [&] {
            ern::RuntimeEnvironment value;
            value.directory = directory;
            value.executable_version = "Injected memory fixture";
            value.layout_verified = true;
            value.rules = synthetic_rules();
            value.read = memory.reader();
            value.write = [&](ern::Address address, std::span<const std::byte> input) {
                ++writes;
                test::require(std::filesystem::file_size(directory / "relic-edits.jsonl") > 0,
                              "Memory changed before the backup journal was written");
                std::memcpy(memory.bytes.data() + address - test::Fixture::base, input.data(),
                            input.size());
                return true;
            };
            value.locate_roots = [&] {
                return std::variant<ern::RelicRoots, std::string>{memory.roots};
            };
            value.dlc = [] { return ern::DlcState::Unknown; };
            return value;
        };
        {
            auto config = environment();
            config.rules =
                ern::FileRuleSource{source,
                                    [&](const std::filesystem::path &path)
                                        -> std::variant<ern::FileContent, ern::FileError> {
                                        if (std::this_thread::get_id() == owner)
                                            ++foreground_reads;
                                        if (refuse_source.load())
                                            return ern::FileError{"Injected source read failure"};
                                        return ern::read_file(path);
                                    }};
            ern::Runtime runtime;
            runtime.initialize(std::move(config));
            runtime.refresh();
            test::require(runtime.editing_supported(),
                          "Configured adapters did not enable fixture editing");
            const auto before_poll = foreground_reads;
            for (int i = 0; i < 100; ++i)
                runtime.poll();
            test::require(foreground_reads == before_poll,
                          "Ordinary polling performed a foreground file read");
            for (std::size_t slot = 0; slot < 3; ++slot)
                runtime.editor().set_effect(slot, {0, ern::EffectField::Effect}, 7000400);
            const auto before_batch = foreground_reads;
            const auto applied = runtime.apply_drafts();
            test::require(
                !applied.failure && applied.applied == 3 && writes == 3 &&
                    foreground_reads - before_batch == 3,
                "A batch skipped final source checks or repeated them during memory refresh");
            const auto before_undo = foreground_reads;
            const auto undone = runtime.undo();
            test::require(!undone.failure && undone.restored == 3 &&
                              foreground_reads - before_undo == 3,
                          "Undo did not check the source again for every restored relic");
            const auto before_failure = memory.bytes;
            const auto journal_size = std::filesystem::file_size(runtime.journal_path());
            const auto before_writes = writes;
            refuse_source.store(true);
            const ern::Effects replacement{
                {7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
            for (const auto field : {ern::EffectField::Effect, ern::EffectField::Curse})
                for (std::size_t row = 0; row < replacement.rows.size(); ++row)
                    runtime.editor().set_effect(0, {row, field},
                                                ern::effect_at(replacement, {row, field}));
            const auto rejected = runtime.apply_drafts();
            test::require(rejected.failure &&
                              std::get<ern::EditRejected>(rejected.failure->failure).reason ==
                                  ern::EditRejectReason::RulesUnavailable,
                          "A refused source was not reported as unavailable rules");
            test::require(memory.bytes == before_failure && writes == before_writes,
                          "A failed fresh source check authorized a cached edit");
            test::require(std::filesystem::file_size(runtime.journal_path()) == journal_size,
                          "A failed fresh source check wrote the journal");
        }
        memory = test::Fixture{};
        writes = 0;
        {
            auto config = environment();
            config.journal_name = "missing/edits.jsonl";
            ern::Runtime runtime;
            runtime.initialize(std::move(config));
            runtime.refresh();
            const auto before = memory.bytes;
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            const auto rejected = runtime.apply_drafts();
            test::require(std::get<ern::EditRejected>(rejected.failure->failure).reason ==
                                  ern::EditRejectReason::JournalOpen &&
                              writes == 0 && memory.bytes == before,
                          "A journal open failure reached the memory adapter");
        }
        {
            auto config = environment();
            config.journal_name = "blocked-edits.jsonl";
            const auto blocked = directory / config.journal_name;
            {
                std::ofstream journal(blocked, std::ios::binary);
                journal << std::string(ern::rotating_file_limit - 1, ' ') << '\n';
                test::require(bool(journal), "Could not prepare a full journal");
            }
            std::filesystem::create_directory(directory / "blocked-edits.jsonl.1");
            ern::Runtime runtime;
            runtime.initialize(std::move(config));
            runtime.refresh();
            const auto before = memory.bytes;
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            const auto rejected = runtime.apply_drafts();
            test::require(std::get<ern::EditRejected>(rejected.failure->failure).reason ==
                                  ern::EditRejectReason::JournalWrite &&
                              writes == 0 && memory.bytes == before &&
                              std::filesystem::file_size(blocked) == ern::rotating_file_limit &&
                              std::filesystem::is_directory(directory / "blocked-edits.jsonl.1"),
                          "A journal rotation failure reached memory or damaged existing files");
        }
        {
            auto config = environment();
            config.layout_verified = false;
            ern::Runtime runtime;
            runtime.initialize(std::move(config));
            runtime.refresh();
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            const auto rejected = runtime.apply_drafts();
            test::require(!runtime.editing_supported() && writes == 0 &&
                              std::get<ern::EditRejected>(rejected.failure->failure).reason ==
                                  ern::EditRejectReason::RulesUnavailable,
                          "Supplied rules bypassed the executable/layout compatibility gate");
        }
        for (const bool poll_before_apply : {false, true}) {
            memory = test::Fixture{};
            writes = 0;
            std::optional<std::string> native_error;
            auto config = environment();
            config.journal_name = "callback-health.jsonl";
            config.native_edit_error = [&] { return native_error; };
            ern::Runtime runtime;
            runtime.initialize(std::move(config));
            runtime.refresh();
            test::require(runtime.editing_supported(), "A healthy callback locked editing");
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            const auto before = memory.bytes;
            native_error = "Equipment callback was replaced";
            if (poll_before_apply) {
                runtime.poll();
                test::require(!runtime.editing_supported() &&
                                  std::string(runtime.editing_lock_reason()) == *native_error,
                              "Idle polling did not publish a lost callback's error");
            }
            const auto blocked = runtime.apply_drafts();
            test::require(blocked.failure && writes == 0 && memory.bytes == before &&
                              std::get<ern::EditRejected>(blocked.failure->failure).detail ==
                                  *native_error &&
                              !std::filesystem::exists(runtime.journal_path()),
                          "A callback lost before Apply reached the journal or memory writer");
            native_error.reset();
            runtime.refresh();
            const auto applied = runtime.apply_drafts();
            test::require(!applied.failure && applied.applied == 1 && runtime.can_undo(),
                          "Healthy dispatch did not allow Apply after the test restored it");
            const auto journal_size = std::filesystem::file_size(runtime.journal_path());
            const auto edited = memory.bytes;
            native_error = "Equipment callback was replaced";
            const auto undo_blocked = runtime.undo();
            test::require(undo_blocked.failure && undo_blocked.restored == 0 &&
                              runtime.can_undo() && memory.bytes == edited &&
                              std::filesystem::file_size(runtime.journal_path()) == journal_size,
                          "A lost callback wrote during Undo or discarded its restoration history");
            native_error.reset();
            runtime.refresh();
            const auto restored = runtime.undo();
            test::require(!restored.failure && restored.restored == 1 && memory.bytes == before,
                          "Undo did not restore the baseline after dispatch became available");
            std::filesystem::remove(runtime.journal_path());
        }
        for (const auto *name : {"ern-relics.log", "regulation.bin", "relic-edits.jsonl",
                                 "blocked-edits.jsonl", "blocked-edits.jsonl.1"})
            std::filesystem::remove(directory / name);
        std::filesystem::remove(directory);
    });
}
