#include "app/process_guard.hpp"
#include "support/check.hpp"
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {
using test::require;
struct Scan {
    bool snapshot_fails = false;
    std::optional<DWORD> first_error;
    std::optional<unsigned int> fail_next_at;
    std::vector<std::wstring> names{L"nightreign.exe", L"steam.exe"};
    std::size_t position = 0;
    unsigned int first_calls = 0, next_calls = 0, error_calls = 0, closes = 0;
    DWORD last_error = ERROR_SUCCESS;
};
Scan *current = nullptr;
HANDLE snapshot_handle() { return reinterpret_cast<HANDLE>(std::uintptr_t{0x1234}); }

HANDLE WINAPI snapshot(DWORD flags, DWORD process) {
    require(flags == TH32CS_SNAPPROCESS && process == 0,
            "Startup scanned a different snapshot type or process");
    return current->snapshot_fails ? INVALID_HANDLE_VALUE : snapshot_handle();
}
void copy_entry(PROCESSENTRY32W *entry) {
    require(entry->dwSize == sizeof(*entry), "Process entry size was not initialized");
    require(current->names[current->position].size() < std::size(entry->szExeFile),
            "Synthetic process name is too long");
    wcscpy_s(entry->szExeFile, current->names[current->position].c_str());
}
BOOL WINAPI first(HANDLE handle, PROCESSENTRY32W *entry) {
    require(handle == snapshot_handle(), "First process used the wrong snapshot");
    ++current->first_calls;
    if (current->first_error || current->names.empty()) {
        current->last_error = current->first_error.value_or(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    copy_entry(entry);
    return TRUE;
}
BOOL WINAPI next(HANDLE handle, PROCESSENTRY32W *entry) {
    require(handle == snapshot_handle(), "Next process used the wrong snapshot");
    ++current->next_calls;
    if (current->fail_next_at == current->next_calls) {
        current->last_error = ERROR_ACCESS_DENIED;
        return FALSE;
    }
    if (++current->position == current->names.size()) {
        current->last_error = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    copy_entry(entry);
    return TRUE;
}
DWORD WINAPI error() {
    ++current->error_calls;
    return current->last_error;
}
BOOL WINAPI close(HANDLE handle) {
    require(handle == snapshot_handle(), "Closed the wrong process snapshot");
    ++current->closes;
    // Closing a handle must not overwrite the scan's terminal error decision.
    current->last_error = ERROR_INVALID_HANDLE;
    return TRUE;
}
bool blocked(Scan &scan) {
    current = &scan;
    return ern::eac_running_or_unavailable({snapshot, first, next, error, close});
}

void failures_block() {
    Scan unavailable;
    unavailable.snapshot_fails = true;
    require(blocked(unavailable) && unavailable.first_calls == 0 && unavailable.closes == 0,
            "Snapshot creation failure allowed startup or closed an invalid handle");
    for (const auto first_error : {ERROR_ACCESS_DENIED, ERROR_NO_MORE_FILES}) {
        Scan unreadable;
        unreadable.first_error = first_error;
        require(blocked(unreadable) && unreadable.first_calls == 1 && unreadable.next_calls == 0 &&
                    unreadable.closes == 1,
                "First-entry failure allowed startup or leaked the snapshot");
    }
    Scan empty;
    empty.names.clear();
    require(blocked(empty) && empty.closes == 1,
            "An empty process snapshot was treated as a completed scan");
    for (const auto failing_call : {1u, 2u}) {
        Scan interrupted;
        interrupted.fail_next_at = failing_call;
        require(blocked(interrupted) && interrupted.next_calls == failing_call &&
                    interrupted.error_calls == 1 && interrupted.closes == 1,
                "An interrupted process walk allowed startup or lost its terminal error");
    }
}

void complete_scans() {
    Scan clear;
    require(!blocked(clear) && clear.first_calls == 1 && clear.next_calls == clear.names.size() &&
                clear.error_calls == 1 && clear.closes == 1,
            "A completed clean scan stayed blocked or read its error after closing");
    for (const auto *name :
         {L"easyanticheat.exe", L"EASYANTICHEAT.EXE", L"EasyAntiCheat_EOS.exe"}) {
        Scan present;
        present.names.push_back(name);
        require(blocked(present) && present.next_calls == 2 && present.error_calls == 0 &&
                    present.closes == 1,
                "Case-insensitive EAC detection failed or leaked the snapshot");
    }
    Scan unrelated;
    unrelated.names = {L"my-easyanticheat.exe", L"easyanticheat_eos.exe.bak"};
    require(!blocked(unrelated) && unrelated.closes == 1,
            "A filename containing an EAC name was incorrectly blocked");
}
} // namespace

int main() {
    return test::run("startup process scan completion, failures, and EAC names", [] {
        failures_block();
        complete_scans();
    });
}
