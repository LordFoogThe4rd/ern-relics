#include "app/input.hpp"
#include "app/editor_application.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <MinHook.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>
#include <optional>

namespace ern {
namespace {
using Microsoft::WRL::ComPtr;
using StateFn = HRESULT(STDMETHODCALLTYPE *)(IDirectInputDevice8W *, DWORD, void *);
using DataFn = HRESULT(STDMETHODCALLTYPE *)(IDirectInputDevice8W *, DWORD, DIDEVICEOBJECTDATA *,
                                            DWORD *, DWORD);
using WarpFn = BOOL(WINAPI *)(int, int);
using ClipFn = BOOL(WINAPI *)(const RECT *);
std::array<StateFn, 2> original_state{};
std::array<DataFn, 2> original_data{};
WarpFn original_warp = nullptr;
ClipFn original_clip = nullptr;
std::atomic<HWND> captured_window = nullptr;
std::atomic<bool> state_capture_logged = false;
std::atomic<bool> warp_capture_logged = false;
std::optional<RECT> previous_clip;

bool capture_active() {
    const auto window = captured_window.load(std::memory_order_relaxed);
    return window && GetForegroundWindow() == window;
}

template <std::size_t Index>
HRESULT STDMETHODCALLTYPE state_hook(IDirectInputDevice8W *device, DWORD size, void *output) {
    const auto result = original_state[Index](device, size, output);
    if (SUCCEEDED(result) && output && capture_active() &&
        (size == 256 || size == sizeof(DIMOUSESTATE) || size == sizeof(DIMOUSESTATE2))) {
        std::memset(output, 0, size);
        if (!state_capture_logged.exchange(true))
            application().log("DirectInput device state captured while the overlay is open");
    }
    return result;
}

template <std::size_t Index>
HRESULT STDMETHODCALLTYPE data_hook(IDirectInputDevice8W *device, DWORD size,
                                    DIDEVICEOBJECTDATA *data, DWORD *count, DWORD flags) {
    const auto result = original_data[Index](device, size, data, count, flags);
    if (SUCCEEDED(result) && count && capture_active()) {
        DIDEVCAPS caps{};
        caps.dwSize = sizeof(caps);
        if (SUCCEEDED(device->GetCapabilities(&caps))) {
            const auto type = GET_DIDEVICE_TYPE(caps.dwDevType);
            if (type == DI8DEVTYPE_MOUSE || type == DI8DEVTYPE_KEYBOARD)
                *count = 0;
        }
    }
    return result;
}

BOOL WINAPI warp_hook(int x, int y) {
    if (capture_active()) {
        if (!warp_capture_logged.exchange(true))
            application().log("Game cursor warping suppressed while the overlay is open");
        return TRUE;
    }
    return original_warp(x, y);
}

BOOL WINAPI clip_hook(const RECT *rect) { return original_clip(capture_active() ? nullptr : rect); }
} // namespace

void set_input_capture(bool enabled, HWND window) {
    const auto previous =
        captured_window.exchange(enabled ? window : nullptr, std::memory_order_relaxed);
    if (enabled && GetForegroundWindow() == window && !previous_clip) {
        RECT clip{};
        if (GetClipCursor(&clip))
            previous_clip = clip;
        if (original_clip)
            original_clip(nullptr);
    } else if (!enabled && previous) {
        if (original_clip && previous_clip && GetForegroundWindow() == window)
            original_clip(&*previous_clip);
        previous_clip.reset();
    }
}

bool install_input_hooks() {
    ComPtr<IDirectInput8W> input;
    auto result =
        DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                           reinterpret_cast<void **>(input.GetAddressOf()), nullptr);
    if (FAILED(result)) {
        application().log("DirectInput probe initialization failed");
        return false;
    }
    std::vector<void *> targets;
    const auto hook = [&targets](void *target, void *detour, void **original) {
        if (std::find(targets.begin(), targets.end(), target) != targets.end())
            return true;
        if (MH_CreateHook(target, detour, original) != MH_OK)
            return false;
        targets.push_back(target);
        return MH_QueueEnableHook(target) == MH_OK;
    };
    const std::array<const GUID *, 2> devices{&GUID_SysMouse, &GUID_SysKeyboard};
    for (std::size_t i = 0; i < devices.size(); ++i) {
        ComPtr<IDirectInputDevice8W> device;
        if (FAILED(input->CreateDevice(*devices[i], &device, nullptr))) {
            for (const auto target : targets)
                MH_RemoveHook(target);
            application().log("DirectInput device probe failed");
            return false;
        }
        const auto vtable = *reinterpret_cast<void ***>(device.Get());
        const auto state_detour = i == 0 ? reinterpret_cast<void *>(state_hook<0>)
                                         : reinterpret_cast<void *>(state_hook<1>);
        const auto data_detour = i == 0 ? reinterpret_cast<void *>(data_hook<0>)
                                        : reinterpret_cast<void *>(data_hook<1>);
        if (!hook(vtable[9], state_detour, reinterpret_cast<void **>(&original_state[i])) ||
            !hook(vtable[10], data_detour, reinterpret_cast<void **>(&original_data[i]))) {
            application().log("Cannot prepare DirectInput capture hooks");
            for (const auto target : targets)
                MH_RemoveHook(target);
            return false;
        }
    }
    const auto user32 = GetModuleHandleW(L"user32.dll");
    if (!hook(reinterpret_cast<void *>(GetProcAddress(user32, "SetCursorPos")),
              reinterpret_cast<void *>(warp_hook), reinterpret_cast<void **>(&original_warp)) ||
        !hook(reinterpret_cast<void *>(GetProcAddress(user32, "ClipCursor")),
              reinterpret_cast<void *>(clip_hook), reinterpret_cast<void **>(&original_clip))) {
        application().log("Cannot prepare cursor capture hooks");
        for (const auto target : targets)
            MH_RemoveHook(target);
        return false;
    }
    if (MH_ApplyQueued() != MH_OK) {
        for (const auto target : targets) {
            MH_DisableHook(target);
            MH_RemoveHook(target);
        }
        application().log("Cannot enable input capture hooks");
        return false;
    }
    application().log("DirectInput and cursor capture hooks ready");
    return true;
}
} // namespace ern
