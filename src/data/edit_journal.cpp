#include "data/edit_journal.hpp"
#include "data/rotating_file.hpp"
#include "relics/effect_encoding.hpp"
#include <chrono>
#include <sstream>

namespace ern {
std::optional<JournalError> record_edit(const std::filesystem::path &path, std::size_t slot,
                                        const Relic &before, Effects replacement) {
    std::ostringstream journal;
    journal << "{\"time\":"
            << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
            << ",\"slot\":" << slot + 1 << ",\"inventory_index\":" << before.inventory_index()
            << ",\"item_id\":" << before.item_id << ",\"before\":[";
    const auto original_fields = encode_effects(before.effects);
    const auto replacement_fields = encode_effects(replacement);
    for (std::size_t i = 0; i < original_fields.size(); ++i)
        journal << (i ? "," : "") << original_fields[i];
    journal << "],\"after\":[";
    for (std::size_t i = 0; i < replacement_fields.size(); ++i)
        journal << (i ? "," : "") << replacement_fields[i];
    journal << "]}\n";
    if (!journal)
        return JournalError::Write;
    if (const auto error = append_rotating_file(path, journal.str()))
        return *error == FileAppendError::Open ? JournalError::Open : JournalError::Write;
    return std::nullopt;
}
} // namespace ern
