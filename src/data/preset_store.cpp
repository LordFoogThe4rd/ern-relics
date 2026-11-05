#include "data/preset_store.hpp"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>

namespace ern {
namespace {
std::optional<PresetStoreError> save_atomic(const std::filesystem::path &destination,
                                            std::string_view contents, PresetSaveMode mode) {
    static std::atomic<std::uint64_t> sequence = 0;
    std::filesystem::path temporary;
    HANDLE handle;
    for (;;) {
        temporary = destination;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                     std::to_wstring(sequence.fetch_add(1)) + L".tmp";
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        if (GetLastError() != ERROR_FILE_EXISTS) {
            return PresetStoreError::Write;
        }
    }
    struct PendingFile {
        HANDLE handle;
        const std::filesystem::path &path;
        ~PendingFile() {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            DeleteFileW(path.c_str());
        }
    } pending{handle, temporary};
    DWORD written = 0;
    if (!WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &written,
                   nullptr) ||
        written != contents.size() || !FlushFileBuffers(handle))
        return PresetStoreError::Write;
    const bool closed = CloseHandle(handle) != FALSE;
    pending.handle = INVALID_HANDLE_VALUE;
    if (!closed)
        return PresetStoreError::Write;
    // Same-directory rename only. A failed replacement leaves the old preset untouched.
    const DWORD flags =
        MOVEFILE_WRITE_THROUGH | (mode == PresetSaveMode::Replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (MoveFileExW(temporary.c_str(), destination.c_str(), flags))
        return std::nullopt;
    const auto error = GetLastError();
    if (mode == PresetSaveMode::Create &&
        (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)) {
        std::error_code status_error;
        if (std::filesystem::symlink_status(destination, status_error).type() ==
                std::filesystem::file_type::regular &&
            !status_error)
            return PresetStoreError::AlreadyExists;
    }
    return PresetStoreError::Write;
}
} // namespace

std::variant<std::vector<SavedPreset>, PresetStoreError> PresetStore::list() const {
    std::vector<SavedPreset> saved;
    std::error_code error;
    if (!std::filesystem::exists(directory_, error)) {
        if (error)
            return PresetStoreError::Read;
        return saved;
    }
    for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end;
         it.increment(error)) {
        const auto path = it->path();
        if (_wcsicmp(path.extension().c_str(), L".json") != 0)
            continue;
        const auto stem = path.stem();
        if (std::any_of(stem.native().begin(), stem.native().end(),
                        [](auto ch) { return ch > 0x7f; }))
            continue;
        const auto name = stem.string();
        if (!valid_preset_name(name) || !it->is_regular_file(error))
            continue;
        std::ifstream file(path, std::ios::binary);
        std::string text(8193, '\0');
        file.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(file.gcount()));
        saved.push_back(
            {name, file.bad() || text.size() > 8192 ? std::nullopt : parse_preset(text)});
    }
    if (error)
        return PresetStoreError::Read;
    std::sort(saved.begin(), saved.end(),
              [](const auto &a, const auto &b) { return a.name < b.name; });
    return saved;
}

std::optional<PresetStoreError> PresetStore::save(std::string_view name, const Preset &preset,
                                                  PresetSaveMode mode) const {
    if (!valid_preset_name(name))
        return PresetStoreError::InvalidName;
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error)
        return PresetStoreError::CreateFolder;
    return save_atomic(directory_ / (std::string(name) + ".json"), serialize_preset(preset), mode);
}

std::optional<PresetStoreError> PresetStore::remove(std::string_view name) const {
    if (!valid_preset_name(name))
        return PresetStoreError::InvalidName;
    const auto path = directory_ / (std::string(name) + ".json");
    std::error_code error;
    const auto type = std::filesystem::symlink_status(path, error).type();
    if (error || type != std::filesystem::file_type::regular ||
        !std::filesystem::remove(path, error) || error)
        return PresetStoreError::Delete;
    return std::nullopt;
}
} // namespace ern
