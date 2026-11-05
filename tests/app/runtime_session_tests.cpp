#include "app/runtime.hpp"
#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"

namespace {
enum class Transition { Unload, TitleScreen, Player, Inventory, Game };

void change_context(test::Fixture &memory, Transition transition) {
    const auto copy = [&](std::size_t from, std::size_t to, std::size_t count) {
        std::memmove(memory.bytes.data() + to, memory.bytes.data() + from, count);
    };
    switch (transition) {
    case Transition::Player:
        copy(0x1000, 0x2000, 0x400);
        memory.put(test::Fixture::base + 0x308, test::Fixture::base + 0x2000);
        break;
    case Transition::Inventory:
        copy(0x100, 0x400, 0x40);
        memory.put(memory.roots.item_manager_global, test::Fixture::base + 0x400);
        break;
    case Transition::Game:
        copy(0x300, 0x600, 0xE0);
        memory.put(memory.roots.game_data_global, test::Fixture::base + 0x600);
        break;
    case Transition::Unload:
        memory.put(test::Fixture::base + 0x308, ern::Address{0});
        break;
    case Transition::TitleScreen:
        // The real game retains player data at the title screen, with empty handles.
        memory.put(test::Fixture::base + 0x8000 + ern::game_layout::live_player, ern::Address{0});
        for (std::size_t slot = 0; slot < ern::relic_slot_count; ++slot)
            memory.put(test::Fixture::base + 0x1000 + ern::game_layout::equipped_relics + slot * 4,
                       std::uint32_t{0});
        break;
    }
}
} // namespace

int main() {
    return test::run("character context invalidates drafts and Undo, including during a batch", [] {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("runtime-session-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        test::Fixture memory;
        unsigned int writes = 0;
        bool switch_during_write = false;
        const auto environment = [&] {
            ern::RuntimeEnvironment value;
            value.directory = directory;
            value.layout_verified = true;
            value.rules = synthetic_rules();
            value.read = memory.reader();
            value.write = [&](ern::Address address, std::span<const std::byte> input) {
                ++writes;
                std::memcpy(memory.bytes.data() + address - test::Fixture::base, input.data(),
                            input.size());
                if (switch_during_write) {
                    switch_during_write = false;
                    change_context(memory, Transition::Player);
                }
                return true;
            };
            value.locate_roots = [&] {
                return std::variant<ern::RelicRoots, std::string>{memory.roots};
            };
            value.dlc = [] { return ern::DlcState::Unknown; };
            return value;
        };
        const auto pending = [](ern::Runtime &runtime) {
            return runtime.editor()
                .review(std::get_if<ern::EquippedRelics>(&runtime.inspection()),
                        runtime.validator(), runtime.rule_context())
                .relic_count;
        };
        for (const auto transition :
             {Transition::Unload, Transition::TitleScreen, Transition::Player,
              Transition::Inventory, Transition::Game}) {
            memory = test::Fixture{};
            ern::Runtime runtime;
            runtime.initialize(environment());
            runtime.refresh();
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            test::require(runtime.apply_drafts().applied == 1 && runtime.can_undo(),
                          "Initial edit did not create Undo");
            runtime.editor().set_effect(1, {0, ern::EffectField::Effect}, 7000400);
            runtime.refresh();
            test::require(runtime.can_undo() && pending(runtime) == 1,
                          "Same-context refresh lost pending work or Undo");
            const auto journal_size = std::filesystem::file_size(runtime.journal_path());
            const auto writes_before = writes;
            const auto loaded_memory = memory.bytes;
            change_context(memory, transition);
            runtime.refresh();
            if (transition == Transition::Unload || transition == Transition::TitleScreen) {
                test::require(!runtime.can_undo() && !runtime.editor().drafts()[1] &&
                                  !runtime.editing_supported(),
                              "Observed unload retained editing authority");
                // Allocators may reuse every address for the next character.
                memory.bytes = loaded_memory;
                runtime.refresh();
            }
            const auto before = memory.bytes;
            test::require(
                !runtime.can_undo() && pending(runtime) == 0,
                "Matching relic values carried a draft or Undo into a different player context");
            const auto unapplied = runtime.apply_drafts();
            const auto undone = runtime.undo();
            test::require(unapplied.applied == 0 && !undone.failure && undone.restored == 0 &&
                              memory.bytes == before && writes == writes_before &&
                              std::filesystem::file_size(runtime.journal_path()) == journal_size,
                          "An invalidated session changed memory or the journal");
            // New edits remain usable in the new context.
            runtime.editor().set_effect(1, {0, ern::EffectField::Effect}, 7000400);
            test::require(runtime.apply_drafts().applied == 1,
                          "Context invalidation permanently disabled editing");
        }
        for (const auto unreadable_address :
             {memory.roots.item_manager_global, memory.roots.game_data_global,
              test::Fixture::base + 0x308,
              test::Fixture::base + 0x8000 + ern::game_layout::live_player,
              test::Fixture::base + 0x1000 + ern::game_layout::equipped_relics}) {
            memory = test::Fixture{};
            bool unreadable = false;
            auto config = environment();
            config.read = [&](ern::Address address, std::span<std::byte> output) {
                if (unreadable && address == unreadable_address)
                    return false;
                return memory.read(address, output);
            };
            ern::Runtime runtime;
            runtime.initialize(std::move(config));
            runtime.refresh();
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            test::require(runtime.apply_drafts().applied == 1,
                          "Could not prepare transient read test");
            runtime.editor().set_effect(1, {0, ern::EffectField::Effect}, 7000400);
            const auto session = runtime.session();
            unreadable = true;
            runtime.refresh();
            test::require(!runtime.editing_supported() && runtime.can_undo() &&
                              pending(runtime) == 1 && runtime.session() == session,
                          "A failed memory read was mistaken for a confirmed character unload");
            unreadable = false;
            runtime.refresh();
            test::require(runtime.editing_supported() && runtime.can_undo() &&
                              pending(runtime) == 1,
                          "A transient read failure permanently lost the loaded session");
            const auto before = memory.bytes;
            const auto writes_before = writes;
            const auto journal_size = std::filesystem::file_size(runtime.journal_path());
            unreadable = true;
            const auto blocked = runtime.undo();
            test::require(
                blocked.restored == 0 && blocked.failure && runtime.can_undo() &&
                    pending(runtime) == 1 && runtime.session() == session &&
                    memory.bytes == before && writes == writes_before &&
                    std::filesystem::file_size(runtime.journal_path()) == journal_size,
                "An unreadable Undo preflight lost work or reached memory or the journal");
            unreadable = false;
            const auto retried = runtime.undo();
            test::require(!retried.failure && retried.restored == 1 && !runtime.can_undo() &&
                              pending(runtime) == 1 && runtime.session() == session,
                          "Undo could not retry after its owner pointer became readable again");
        }
        for (const auto transition : {Transition::Unload, Transition::Player}) {
            memory = test::Fixture{};
            ern::Runtime runtime;
            runtime.initialize(environment());
            runtime.refresh();
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            const auto journal_size = std::filesystem::file_size(runtime.journal_path());
            const auto writes_before = writes;
            change_context(memory, transition);
            const auto before = memory.bytes;
            const auto result = runtime.apply_drafts();
            test::require(
                result.applied == 0 && result.failure &&
                    std::get<ern::EditRejected>(result.failure->failure).reason ==
                        ern::EditRejectReason::Stale &&
                    writes == writes_before && memory.bytes == before && pending(runtime) == 0 &&
                    std::filesystem::file_size(runtime.journal_path()) == journal_size,
                "Preflight silently accepted or lost a character change before the next poll");
        }
        memory = test::Fixture{};
        {
            ern::Runtime runtime;
            runtime.initialize(environment());
            runtime.refresh();
            runtime.editor().set_effect(0, {0, ern::EffectField::Effect}, 7000400);
            test::require(runtime.apply_drafts().applied == 1, "Initial edit failed");
            const auto journal_size = std::filesystem::file_size(runtime.journal_path());
            const auto writes_before = writes;
            change_context(memory, Transition::Player);
            // No polling refresh before Undo: its cached authority must be checked again.
            const auto result = runtime.undo();
            test::require(result.restored == 0 && result.failure &&
                              std::get<ern::EditRejected>(result.failure->failure).reason ==
                                  ern::EditRejectReason::Stale &&
                              !runtime.can_undo() && writes == writes_before &&
                              std::filesystem::file_size(runtime.journal_path()) == journal_size,
                          "Undo authorized a new owner before the next poll");
        }
        memory = test::Fixture{};
        {
            ern::Runtime runtime;
            runtime.initialize(environment());
            runtime.refresh();
            for (std::size_t i = 0; i < 3; ++i)
                runtime.editor().set_effect(i, {0, ern::EffectField::Effect}, 7000400);
            const auto writes_before = writes;
            switch_during_write = true;
            const auto result = runtime.apply_drafts();
            test::require(
                result.applied == 0 && result.failure && result.failure->slot == 0 &&
                    std::get<ern::WriteFailure>(result.failure->failure).restoration ==
                        ern::Restoration::Unconfirmed &&
                    writes == writes_before + 1 && !runtime.can_undo() && pending(runtime) == 0,
                "A batch adopted a changed context or restored old drafts after invalidation");
        }
        for (const auto *name : {"ern-relics.log", "relic-edits.jsonl"})
            std::filesystem::remove(directory / name);
        std::filesystem::remove(directory);
    });
}
