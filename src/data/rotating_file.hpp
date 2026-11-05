#pragma once
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

namespace ern {
inline constexpr std::size_t rotating_file_limit = 1024 * 1024;
enum class FileAppendError { Open, Write };

// One writer per path. Rotate before an append would exceed 1 MiB, retaining .1 and .2.
std::optional<FileAppendError> append_rotating_file(const std::filesystem::path &path,
                                                    std::string_view record);
} // namespace ern
