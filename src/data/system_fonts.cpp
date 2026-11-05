#include "data/system_fonts.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace ern {
namespace {
std::vector<std::uint8_t> font_bytes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size <= 0 || size > 16 * 1024 * 1024)
        return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file ? bytes : std::vector<std::uint8_t>{};
}
} // namespace
SystemFonts read_system_fonts() {
    wchar_t windows[32768]{};
    const auto size = GetWindowsDirectoryW(windows, 32768);
    if (!size || size >= 32768)
        return {};
    const auto directory = std::filesystem::path(windows) / "Fonts";
    return {font_bytes(directory / "segoeui.ttf"), font_bytes(directory / "georgia.ttf")};
}
} // namespace ern
