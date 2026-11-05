#include "data/rotating_file.hpp"
#include "support/check.hpp"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    test::require(bool(file), "Could not read a retained file");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    return test::run("file size boundaries, retained records and rotation failures", [] {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("rotating-file-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto path = directory / L"\u6d4b\u8bd5.log";
        auto previous = path;
        previous += ".1";
        auto oldest = path;
        oldest += ".2";
        auto excess = path;
        excess += ".3";
        const std::string first = std::string(ern::rotating_file_limit - 2, 'A') + '\n';
        test::require(!ern::append_rotating_file(path, first) &&
                          !ern::append_rotating_file(path, "\n") &&
                          std::filesystem::file_size(path) == ern::rotating_file_limit &&
                          !std::filesystem::exists(previous),
                      "A file at exactly 1 MiB rotated or used text-mode byte counts");
        const std::string second(ern::rotating_file_limit, 'B');
        const std::string third(ern::rotating_file_limit, 'C');
        test::require(!ern::append_rotating_file(path, second) &&
                          read_file(previous) == first + '\n' && read_file(path) == second,
                      "Rotation split a record or lost the previous file");
        test::require(!ern::append_rotating_file(path, third) &&
                          read_file(oldest) == first + '\n' && read_file(previous) == second &&
                          read_file(path) == third,
                      "A later append lost the backup order or existing file size");
        test::require(!ern::append_rotating_file(path, "last\n") && read_file(oldest) == second &&
                          read_file(previous) == third && read_file(path) == "last\n" &&
                          !std::filesystem::exists(excess),
                      "Rotation did not replace the oldest of two backups");
        std::size_t retained = 0;
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            ++retained;
            test::require(entry.file_size() <= ern::rotating_file_limit,
                          "A retained file exceeded 1 MiB");
        }
        test::require(retained == 3, "Rotation retained more than three files");
        test::require(
            ern::append_rotating_file(path, std::string(ern::rotating_file_limit + 1, 'X')) ==
                    ern::FileAppendError::Write &&
                read_file(path) == "last\n" && read_file(previous) == third &&
                read_file(oldest) == second,
            "An oversized record changed retained history");
        const auto handle = CreateFileW(oldest.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        test::require(handle != INVALID_HANDLE_VALUE, "Could not lock the oldest backup");
        const auto failed = ern::append_rotating_file(path, second);
        CloseHandle(handle);
        test::require(failed == ern::FileAppendError::Write && read_file(path) == "last\n" &&
                          read_file(previous) == third && read_file(oldest) == second,
                      "A blocked rotation appended beyond the limit or damaged history");
        test::require(!ern::append_rotating_file(path, second) && read_file(path) == second &&
                          read_file(previous) == "last\n" && read_file(oldest) == third,
                      "Rotation could not recover after a backup was unlocked");
        test::require(ern::append_rotating_file(directory / "missing" / "log", "entry\n") ==
                          ern::FileAppendError::Open,
                      "An unavailable directory did not report an open failure");
        for (const auto &file : {path, previous, oldest})
            std::filesystem::remove(file);
        std::filesystem::remove(directory);
    });
}
