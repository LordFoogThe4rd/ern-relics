#include "app/overlay.hpp"
#include "app/bootstrap.hpp"
#include "app/editor_application.hpp"

#include <Windows.h>

namespace {
DWORD WINAPI start(void *module) {
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&start), &pinned);
    try {
        if (ern::bootstrap_application(static_cast<HMODULE>(module))) {
            if (ern::install_overlay())
                ern::application().start();
            else
                ern::application().flush_startup_log();
        }
    } catch (const std::exception &error) {
        ern::application().log(std::string("Initialization failed: ") + error.what());
        ern::application().flush_startup_log();
    }
    return 0;
}
} // namespace

extern "C" __declspec(dllexport) BOOL ern_overlay_ready() { return ern::overlay_ready(); }
extern "C" __declspec(dllexport) BOOL ern_query_stats(ern::OverlayStats *output) {
    if (!output)
        return FALSE;
    *output = ern::overlay_stats();
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        const auto thread = CreateThread(nullptr, 0, start, module, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
