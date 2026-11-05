#include "data/preset_store.hpp"
#include "support/check.hpp"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <barrier>
#include <thread>
#include <algorithm>

int main() {
    return test::run("preset storage, corrupt files, names and I/O failures", [] {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("ern-preset-store-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const ern::PresetStore store{directory};
        const ern::Preset preset{{ern::Effects{{1, UINT32_MAX}, {2, UINT32_MAX}, {3, UINT32_MAX}},
                                  std::nullopt, std::nullopt}};
        test::require(std::get<std::vector<ern::SavedPreset>>(store.list()).empty(),
                      "A missing presets folder did not produce an empty list");
        test::require(store.save("../outside", preset) == ern::PresetStoreError::InvalidName &&
                          !std::filesystem::exists(directory),
                      "An invalid name created a folder or escaped preset storage");
        for (const auto *name : {"NUL", "con", "CoM1", "lpt9"})
            test::require(store.save(name, preset) == ern::PresetStoreError::InvalidName &&
                              !std::filesystem::exists(directory),
                          "A reserved Windows device stem reached filesystem creation");
        test::require(!store.save("Zulu", preset) && !store.save("Alpha", preset),
                      "Could not save presets");
        const ern::Preset replacement{
            {ern::Effects{{4, UINT32_MAX}, {5, UINT32_MAX}, {6, UINT32_MAX}}, std::nullopt,
             std::nullopt}};
        {
            std::ofstream file(directory / "Imported.JSON");
            file << ern::serialize_preset(preset);
            test::require(bool(file), "Could not write an imported uppercase-extension preset");
        }
        {
            const auto imported = std::get<std::vector<ern::SavedPreset>>(store.list());
            const auto entry =
                std::find_if(imported.begin(), imported.end(),
                             [](const auto &saved) { return saved.name == "Imported"; });
            test::require(
                entry != imported.end() && entry->contents == preset &&
                    store.save("imported", replacement) == ern::PresetStoreError::AlreadyExists,
                "An imported uppercase extension was hidden or lost its case-alias collision");
            test::require(
                !store.save(entry->name, replacement, ern::PresetSaveMode::Replace),
                "An imported uppercase-extension preset could not be explicitly replaced");
            const auto replaced = std::get<std::vector<ern::SavedPreset>>(store.list());
            const auto updated =
                std::find_if(replaced.begin(), replaced.end(),
                             [](const auto &saved) { return saved.name == "Imported"; });
            test::require(
                updated != replaced.end() && updated->contents == replacement &&
                    !store.remove(updated->name),
                "Replacing an imported preset lost its canonical stem or complete contents");
        }
        test::require(
            store.save("Alpha", replacement) == ern::PresetStoreError::AlreadyExists &&
                store.save("alpha", replacement) == ern::PresetStoreError::AlreadyExists &&
                std::get<std::vector<ern::SavedPreset>>(store.list())[0].contents == preset,
            "Creating a preset replaced an existing name or its Windows case alias");
        {
            std::barrier start{3};
            std::array<std::optional<ern::PresetStoreError>, 2> outcomes;
            std::jthread first([&] {
                start.arrive_and_wait();
                outcomes[0] = store.save("Race", preset);
            });
            std::jthread second([&] {
                start.arrive_and_wait();
                outcomes[1] = store.save("Race", replacement);
            });
            start.arrive_and_wait();
            first.join();
            second.join();
            test::require((!outcomes[0] && outcomes[1] == ern::PresetStoreError::AlreadyExists) ||
                              (!outcomes[1] && outcomes[0] == ern::PresetStoreError::AlreadyExists),
                          "Concurrent creators overwrote a preset or both reported success");
            const auto raced = std::get<std::vector<ern::SavedPreset>>(store.list())[1].contents;
            test::require(raced == (outcomes[0] ? replacement : preset),
                          "The rejected concurrent create changed the winning preset");
            test::require(!store.remove("Race"), "Could not remove the concurrent preset fixture");
        }
        {
            // Deny deletion/rename while permitting reads. This forces the commit step to fail.
            const auto path = directory / "Alpha.json";
            const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            test::require(handle != INVALID_HANDLE_VALUE, "Could not lock the existing preset");
            const auto failed = store.save("Alpha", replacement, ern::PresetSaveMode::Replace);
            CloseHandle(handle);
            test::require(failed == ern::PresetStoreError::Write &&
                              std::get<std::vector<ern::SavedPreset>>(store.list())[0].contents ==
                                  preset,
                          "A failed replacement damaged the existing preset");
            for (const auto &entry : std::filesystem::directory_iterator(directory))
                test::require(entry.path().extension() != ".tmp",
                              "Failed save left a temporary file");
        }
        test::require(!store.save("Alpha", replacement, ern::PresetSaveMode::Replace) &&
                          std::get<std::vector<ern::SavedPreset>>(store.list())[0].contents ==
                              replacement &&
                          !store.save("Alpha", preset, ern::PresetSaveMode::Replace),
                      "Atomic replacement did not publish the complete preset");
        {
            std::ofstream file(directory / "broken.json");
            file << "broken";
        }
        {
            std::ofstream file(directory / "large.json");
            file << std::string(8193, ' ');
        }
        {
            std::ofstream file(directory / "ignored.txt");
            file << "keep";
        }
        for (const auto *name : {L"\u6d4b\u8bd5.txt", L"\u6d4b\u8bd5.json"}) {
            std::ofstream file(directory / name);
            file << ern::serialize_preset(preset);
            test::require(bool(file), "Could not write a Unicode filename fixture");
        }
        const auto saved = std::get<std::vector<ern::SavedPreset>>(store.list());
        test::require(
            saved.size() == 4 && saved[0].name == "Alpha" && saved[0].contents == preset &&
                saved[1].name == "Zulu" && !saved[2].contents && !saved[3].contents,
            "Listing lost valid data, ordering, corrupt-file entries, or Unicode filtering");
        std::filesystem::create_directory(directory / "folder.json");
        test::require(store.save("folder", preset) == ern::PresetStoreError::Write &&
                          store.remove("folder") == ern::PresetStoreError::Delete,
                      "A failed write or non-file deletion lost its error category");
        test::require(!store.remove("Alpha") &&
                          !std::filesystem::exists(directory / "Alpha.json") &&
                          store.remove("Alpha") == ern::PresetStoreError::Delete &&
                          store.remove("../ignored") == ern::PresetStoreError::InvalidName,
                      "Deletion did not stay within its named preset");
        const ern::PresetStore invalid{directory / "ignored.txt"};
        test::require(std::get<ern::PresetStoreError>(invalid.list()) ==
                              ern::PresetStoreError::Read &&
                          invalid.save("test", preset) == ern::PresetStoreError::CreateFolder,
                      "Non-directory storage did not report its I/O failure");
        for (const auto *name :
             {"Zulu.json", "broken.json", "large.json", "ignored.txt", "folder.json"})
            std::filesystem::remove(directory / name);
        for (const auto *name : {L"\u6d4b\u8bd5.txt", L"\u6d4b\u8bd5.json"})
            std::filesystem::remove(directory / name);
        std::filesystem::remove(directory);
    });
}
