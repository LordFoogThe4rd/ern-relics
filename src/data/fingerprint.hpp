#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <cstdint>
#include <variant>
#include <vector>

namespace ern {
struct FileError {
    std::string message;
};
struct FileContent {
    std::filesystem::path resolved_path;
    std::string sha256;
    std::vector<std::uint8_t> bytes;
};
// Resolve the opened handle, including any redirection performed by ME3.
std::variant<FileContent, FileError> read_file(const std::filesystem::path &path);
std::optional<std::string> sha256_file(const std::filesystem::path &path);
} // namespace ern
