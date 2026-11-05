#include "app/relic_edit_queue.hpp"
#include "app/runtime.hpp"
#include "data/rotating_file.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include <fstream>
#include <thread>

using test::require;
int main() {
    return test::run("Apply and Undo use the same owned, journaled native commit", [] {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("relic-commit-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto fill_journal = [&] {
            std::ofstream journal(directory / "relic-edits.jsonl", std::ios::binary);
            journal << std::string(ern::rotating_file_limit - 1, ' ') << '\n';
            require(bool(journal), "Could not prepare a full journal");
        };
        test::Fixture memory;
        ern::RelicEditQueue queue;
        std::vector<ern::RelicEditRequest> committed;
        const auto read = memory.reader();
        std::optional<ern::Effects> active =
            std::get<ern::RelicSnapshot>(ern::read_equipped(read, memory.roots, 0x2F4))
                .relics.slots[0]
                ->effects;
        const auto original = *active;
        const auto worker_thread = std::this_thread::get_id();
        const auto write = [&](ern::Address address, std::span<const std::byte> bytes) {
            require(std::this_thread::get_id() != worker_thread && !active,
                    "Inventory write ran on the application worker or before detachment");
            std::memcpy(memory.bytes.data() + address - test::Fixture::base, bytes.data(),
                        bytes.size());
            return true;
        };
        std::jthread game([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                queue.process([&](ern::RelicEditRequest request) {
                    require(std::filesystem::file_size(directory / "relic-edits.jsonl") > 0,
                            "Native commit ran before the journal was flushed");
                    committed.push_back(request);
                    const auto refresh = [&](ern::RelicEquipStep step) {
                        if (step == ern::RelicEquipStep::Unequip)
                            active.reset();
                        else
                            active = std::get<ern::RelicSnapshot>(
                                         ern::read_equipped(read, memory.roots, 0x2F4))
                                         .relics.slots[request.slot]
                                         ->effects;
                        return ern::RelicEquipResult::Complete;
                    };
                    return ern::replace_equipped(
                        read, write, request.roots, request.equipped_offset, request.owner,
                        request.slot, request.expected, request.replacement, *request.rules,
                        request.context, refresh);
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        {
            ern::RuntimeEnvironment config;
            config.directory = directory;
            config.layout_verified = true;
            config.rules = synthetic_rules();
            config.read = read;
            config.write = write;
            config.locate_roots = [&] {
                return std::variant<ern::RelicRoots, std::string>{memory.roots};
            };
            config.dlc = [] { return ern::DlcState::Unknown; };
            config.commit = [&](ern::RelicEditRequest request) {
                require(std::this_thread::get_id() == worker_thread,
                        "Runtime ownership crossed into the native callback");
                return queue.submit(std::move(request));
            };
            ern::Runtime runtime;
            runtime.initialize(std::move(config));
            runtime.refresh();
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            fill_journal();
            require(runtime.apply_drafts().applied == 1 && active &&
                        active->rows[0].effect == 7000400 && runtime.can_undo() &&
                        std::filesystem::file_size(directory / "relic-edits.jsonl.1") ==
                            ern::rotating_file_limit,
                    "Apply bypassed the commit, lost Undo, or failed to rotate the journal");
            fill_journal();
            const auto undone = runtime.undo();
            require(!undone.failure && undone.restored == 1 && active == original &&
                        !runtime.can_undo() && committed.size() == 2 &&
                        std::filesystem::file_size(directory / "relic-edits.jsonl.2") ==
                            ern::rotating_file_limit,
                    "Undo bypassed the commit or failed after journal rotation");
        }
        game.request_stop();
        game.join();
        require(committed[0].rules && committed[1].rules &&
                    committed[0].expected.effects == original &&
                    committed[1].expected.effects == committed[0].replacement &&
                    committed[1].replacement == original,
                "Prepared edits borrowed Runtime state or lost their before/after values");
        std::filesystem::remove(directory / "relic-edits.jsonl");
        std::filesystem::remove(directory / "relic-edits.jsonl.1");
        std::filesystem::remove(directory / "relic-edits.jsonl.2");
        std::filesystem::remove(directory);
    });
}
