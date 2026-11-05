#pragma once

#include "app/active_regulation.hpp"
#include "data/asset_types.hpp"
#include "game/game_layout.hpp"
#include "game/relic_writer.hpp"

namespace ern {
struct FileRuleSource {
    std::filesystem::path path;
    RegulationRead read = read_file;
};
using RootLookup = std::function<std::variant<RelicRoots, std::string>()>;

// Bootstrap selects the adapters once. Runtime does not identify its host process.
struct RuntimeEnvironment {
    std::filesystem::path directory;
    std::filesystem::path game_directory;
    std::string executable_version;
    std::string journal_name = "relic-edits.jsonl";
    std::string source_label = "Active regulation.bin";
    int hotkey = 0x77;
    bool open_on_start = false;
    std::uint32_t equipped_offset = game_layout::equipped_relics;
    bool layout_verified = false;
    std::variant<FileRuleSource, RegulationRules> rules;
    GameAssets assets;
    MemoryRead read;
    MemoryWrite write;
    // Supplied by the native game adapter. Synthetic hosts use the direct memory writer.
    RelicCommit commit;
    std::function<std::optional<std::string>()> native_edit_error;
    std::string edit_lock;
    RootLookup locate_roots;
    std::function<DlcState()> dlc;
    std::function<void(std::string_view)> log = [](std::string_view) {};
};
} // namespace ern
