#include "data/rotating_file.hpp"
#include <Windows.h>
#include <fstream>

namespace ern {
std::optional<FileAppendError> append_rotating_file(const std::filesystem::path &path,
                                                    std::string_view record) {
    if (record.size() > rotating_file_limit)
        return FileAppendError::Write;
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file)
        return FileAppendError::Open;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return FileAppendError::Write;
    if (size > rotating_file_limit - record.size()) {
        file.close();
        if (!file)
            return FileAppendError::Write;
        auto previous = path;
        previous += ".1";
        auto oldest = path;
        oldest += ".2";
        if (std::filesystem::exists(previous, error)) {
            if (!std::filesystem::is_regular_file(previous, error) || error ||
                !MoveFileExW(previous.c_str(), oldest.c_str(), MOVEFILE_REPLACE_EXISTING))
                return FileAppendError::Write;
        } else if (error)
            return FileAppendError::Write;
        if (!MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING))
            return FileAppendError::Write;
        file.open(path, std::ios::binary | std::ios::app);
        if (!file)
            return FileAppendError::Open;
    }
    file.write(record.data(), static_cast<std::streamsize>(record.size()));
    file.flush();
    file.close();
    return file ? std::nullopt : std::optional{FileAppendError::Write};
}
} // namespace ern
