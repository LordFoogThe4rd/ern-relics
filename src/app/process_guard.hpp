#pragma once

#include <Windows.h>
#include <TlHelp32.h>

namespace ern {
// The startup guard uses these calls only to inspect a process snapshot.
struct ProcessScanApi {
    decltype(&CreateToolhelp32Snapshot) snapshot = CreateToolhelp32Snapshot;
    decltype(&Process32FirstW) first = Process32FirstW;
    decltype(&Process32NextW) next = Process32NextW;
    decltype(&GetLastError) error = GetLastError;
    decltype(&CloseHandle) close = CloseHandle;
};

// Startup is blocked by EAC or an incomplete scan. Only a completed scan clears it.
bool eac_running_or_unavailable(const ProcessScanApi &api = {});
} // namespace ern
