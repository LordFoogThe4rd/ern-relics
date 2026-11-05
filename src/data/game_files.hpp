#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ern::game {
using Bytes = std::vector<std::uint8_t>;
using View = std::span<const std::uint8_t>;
inline void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
inline View slice(View data, std::size_t at, std::size_t size) {
    require(at <= data.size() && size <= data.size() - at,
            "A game-file offset or size is outside the file.");
    return data.subspan(at, size);
}
template <typename T> T number(View data, std::size_t at) {
    T value;
    const auto bytes = slice(data, at, sizeof(T));
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}
inline bool starts(View data, std::string_view prefix) {
    return data.size() >= prefix.size() &&
           std::memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}
std::string utf8(std::wstring_view text);
std::string string(View data, std::size_t offset, bool wide = false);
Bytes decrypt_aes(View data, View key, View iv = {});
Bytes decompress(View data, const std::filesystem::path &game_directory);
using Files = std::unordered_map<std::string, Bytes>;
Files binder(View data, const std::filesystem::path &game_directory,
             const std::function<bool(std::string_view)> &select);

struct GameFiles {
    Files files;
    std::vector<std::string> warnings;
};
// Try logical loose paths first, so ME3's active overrides take precedence.
// Read only requested entries from packed game archives. Nothing is exported.
GameFiles read_game_files(const std::filesystem::path &directory,
                          std::span<const std::string> paths);
} // namespace ern::game
