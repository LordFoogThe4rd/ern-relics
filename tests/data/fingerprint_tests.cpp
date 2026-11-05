#include "support/check.hpp"
#include "data/fingerprint.hpp"
#include "data/regulation.hpp"

#include <chrono>
#include <fstream>

using test::require;

int main() {
    return test::run("file fingerprints and unreadable rules", [] {
        const auto temporary =
            std::filesystem::temp_directory_path() /
            ("ern-relics-rules-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directories(temporary),
                "Could not create isolated file-test directory");
        {
            std::ofstream file(temporary / "keep.json");
            file << "abc";
        }
        require(ern::sha256_file(temporary / "keep.json") ==
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "File fingerprint is not SHA-256");
        require(std::holds_alternative<ern::FileError>(ern::read_regulation(
                    std::get<ern::FileContent>(ern::read_file(temporary / "keep.json")).bytes, {})),
                "Mismatched game data accepted");
        require(std::holds_alternative<ern::FileError>(ern::read_file(temporary / "missing.bin")),
                "Missing game data accepted");
        std::filesystem::remove(temporary / "keep.json");
        std::filesystem::remove(temporary);
    });
}
