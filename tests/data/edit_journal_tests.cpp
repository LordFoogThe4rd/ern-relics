#include "data/edit_journal.hpp"
#include "data/rotating_file.hpp"
#include "support/check.hpp"
#include <chrono>
#include <fstream>
#include <iterator>
#include <locale>

namespace {
class FailedNumberFormatting : public std::num_put<char> {
    iter_type do_put(iter_type, std::ios_base &, char_type, long long) const override {
        throw std::runtime_error("Injected number formatting failure");
    }
};
struct ScopedFormattingFailure {
    std::locale original;
    ScopedFormattingFailure() {
        std::locale::global(std::locale(original, new FailedNumberFormatting));
    }
    ~ScopedFormattingFailure() { std::locale::global(original); }
};
std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    test::require(bool(file), "Could not read the edit journal");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    return test::run("journal records, legacy oversized history and formatting failures", [] {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("edit-journal-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto path = directory / "relic-edits.jsonl";
        const auto previous = directory / "relic-edits.jsonl.1";
        const ern::Relic before{0xC080000C, 34, {{1, 4}, {2, 5}, {3, 6}}};
        const ern::Effects after{{7, 10}, {8, 11}, {9, 12}};
        test::require(!ern::record_edit(path, 3, before, after), "Could not record an edit");
        const auto record = read_file(path);
        const auto complete_record = [](const std::string &value) {
            return value.starts_with("{\"time\":") &&
                   value.ends_with(",\"slot\":4,\"inventory_index\":12,\"item_id\":34,"
                                   "\"before\":[1,2,3,4,5,6],\"after\":[7,8,9,10,11,12]}\n");
        };
        test::require(complete_record(record), "The journal lost edit metadata or effect IDs");
        {
            std::ofstream journal(path, std::ios::binary | std::ios::app);
            const auto repeats = ern::rotating_file_limit / record.size() + 1;
            for (std::size_t i = 0; i < repeats; ++i)
                journal << record;
            test::require(bool(journal), "Could not prepare an oversized legacy journal");
        }
        const auto legacy = read_file(path);
        test::require(legacy.size() > ern::rotating_file_limit &&
                          !ern::record_edit(path, 3, before, after) &&
                          read_file(previous) == legacy && complete_record(read_file(path)),
                      "Rotation truncated legacy history or split the new journal record");
        const auto failed_path = directory / "failed.jsonl";
        {
            const ScopedFormattingFailure failure;
            test::require(ern::record_edit(failed_path, 3, before, after) ==
                                  ern::JournalError::Write &&
                              !std::filesystem::exists(failed_path),
                          "Failed journal formatting authorized an edit or wrote a partial record");
        }
        test::require(!ern::record_edit(failed_path, 3, before, after) &&
                          complete_record(read_file(failed_path)),
                      "A later edit could not recover from a formatting failure");
        for (const auto &file : {path, previous, failed_path})
            std::filesystem::remove(file);
        std::filesystem::remove(directory);
    });
}
