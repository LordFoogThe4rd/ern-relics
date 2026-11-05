#include "app/process_guard.hpp"
#include <cwchar>

namespace ern {
bool eac_running_or_unavailable(const ProcessScanApi &api) {
    const auto snapshot = api.snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return true;
    struct Snapshot {
        HANDLE handle;
        decltype(&CloseHandle) close;
        ~Snapshot() { close(handle); }
    } owned{snapshot, api.close};
    PROCESSENTRY32W entry{sizeof(PROCESSENTRY32W)};
    if (!api.first(snapshot, &entry))
        return true;
    do {
        if (_wcsicmp(entry.szExeFile, L"easyanticheat_eos.exe") == 0 ||
            _wcsicmp(entry.szExeFile, L"easyanticheat.exe") == 0)
            return true;
    } while (api.next(snapshot, &entry));
    // Read the terminal error before closing the snapshot can change it.
    return api.error() != ERROR_NO_MORE_FILES;
}
} // namespace ern
