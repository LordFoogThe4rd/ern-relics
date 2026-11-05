#pragma once
#include "relics/types.hpp"
#include <filesystem>

namespace ern {
enum class JournalError { Open, Write };
std::optional<JournalError> record_edit(const std::filesystem::path &path, std::size_t slot,
                                        const Relic &before, Effects replacement);
} // namespace ern
