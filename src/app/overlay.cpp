#include "app/overlay.hpp"
#include "app/editor_application.hpp"
#include "app/dx12_renderer.hpp"
#include "app/render_lifecycle.hpp"
#include "ui/ui.hpp"
#include "app/input.hpp"

#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace ern {
namespace {
using Microsoft::WRL::ComPtr;
using PresentFn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *, UINT, UINT,
                                                const DXGI_PRESENT_PARAMETERS *);
using ResizeFn = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT,
                                              UINT);
using CreateSwapFn = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory *, IUnknown *,
                                                  DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **);
using CreateHwndFn = HRESULT(STDMETHODCALLTYPE *)(IDXGIFactory2 *, IUnknown *, HWND,
                                                  const DXGI_SWAP_CHAIN_DESC1 *,
                                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *,
                                                  IDXGIOutput *, IDXGISwapChain1 **);

PresentFn original_present = nullptr;
Present1Fn original_present1 = nullptr;
ResizeFn original_resize = nullptr;
CreateSwapFn original_create = nullptr;
CreateHwndFn original_create_hwnd = nullptr;
std::atomic<bool> ready = false;

struct SwapChainQueue {
    HWND window;
    ComPtr<ID3D12CommandQueue> queue;
};

struct Renderer {
    std::recursive_mutex mutex;
    std::map<IDXGISwapChain *, SwapChainQueue> queues;
    RenderLifecycle lifecycle;
    std::unique_ptr<Dx12Renderer> graphics;
    HWND window = nullptr;
    WNDPROC previous_proc = nullptr;
    ImGuiContext *imgui = nullptr;
    EditorUi ui;
    bool open = true;
    OverlayStats stats{};
};

Renderer &renderer() {
    static auto *state = new Renderer;
    return *state;
}

void check(HRESULT result, const char *operation) {
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) +
                                 " failed: " + std::to_string(static_cast<unsigned long>(result)));
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto &r = renderer();
    WNDPROC previous = nullptr;
    {
        std::lock_guard lock(r.mutex);
        previous = r.previous_proc;
        const bool system_shortcut = (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
                                     (wparam == VK_F4 || wparam == VK_TAB);
        if (r.imgui && r.lifecycle.phase() != RenderLifecycle::Phase::Failed && !system_shortcut) {
            ImGui::SetCurrentContext(r.imgui);
            const bool is_keydown = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            if (is_keydown && wparam == static_cast<WPARAM>(application().view()->hotkey) &&
                (lparam & (1LL << 30)) == 0) {
                r.open = !r.open;
                set_input_capture(r.open, window);
                ++r.stats.toggles;
                application().log(r.open ? "Overlay opened" : "Overlay closed");
                return 0;
            }
            if ((message == WM_KEYUP || message == WM_SYSKEYUP) &&
                wparam == static_cast<WPARAM>(application().view()->hotkey))
                return 0;
            if (is_keydown && wparam == VK_ESCAPE && r.open &&
                !ImGui::IsPopupOpen(nullptr,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                r.open = false;
                set_input_capture(false, window);
                ++r.stats.toggles;
                return 0;
            }
            if (r.open && ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
                return 1;
            if (r.open &&
                ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
                 (message >= WM_KEYFIRST && message <= WM_KEYLAST) || message == WM_INPUT))
                return 0;
        }
    }
    return previous ? CallWindowProcW(previous, window, message, wparam, lparam)
                    : DefWindowProcW(window, message, wparam, lparam);
}

SwapChainId chain_id(IDXGISwapChain *chain) { return {reinterpret_cast<std::uintptr_t>(chain)}; }
WindowId window_id(HWND window) { return {reinterpret_cast<std::uintptr_t>(window)}; }
void stop_renderer(Renderer &r, std::string_view reason) {
    r.lifecycle.fail();
    r.open = false;
    set_input_capture(false, r.window);
    application().log(reason);
}
void reset_resources(Renderer &r) {
    if (r.graphics) {
        r.graphics->wait_idle();
        r.graphics.reset();
    }
    r.lifecycle.release_active();
}

bool initialize_renderer(Renderer &r, IDXGISwapChain *chain) {
    const auto view = application().view();
    if (!view)
        return false;
    const auto found = r.queues.find(chain);
    if (found == r.queues.end())
        return false;
    DXGI_SWAP_CHAIN_DESC desc{};
    check(chain->GetDesc(&desc), "GetDesc");
    if (r.imgui)
        ImGui::SetCurrentContext(r.imgui);
    DWORD pid = 0;
    GetWindowThreadProcessId(desc.OutputWindow, &pid);
    if (pid != GetCurrentProcessId() || desc.BufferDesc.Width < 64 || desc.BufferDesc.Height < 64)
        return false;
    if (r.window && r.window != desc.OutputWindow) {
        set_input_capture(false, r.window);
        if (GetWindowLongPtrW(r.window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(window_proc))
            SetWindowLongPtrW(r.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(r.previous_proc));
        ImGui_ImplWin32_Shutdown();
        r.window = nullptr;
    }
    if (!r.imgui) {
        r.imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(r.imgui);
        r.open = view->open_on_start;
    }
    if (!r.window) {
        if (!ImGui_ImplWin32_Init(desc.OutputWindow))
            throw std::runtime_error("ImGui Win32 initialization failed");
        SetLastError(0);
        const auto previous = SetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC,
                                                reinterpret_cast<LONG_PTR>(window_proc));
        if (previous == 0 && GetLastError() != 0)
            throw std::runtime_error("Window subclass failed");
        r.previous_proc = reinterpret_cast<WNDPROC>(previous);
        r.window = desc.OutputWindow;
    }
    RECT client{};
    if (!GetClientRect(desc.OutputWindow, &client))
        throw std::runtime_error("Cannot read the overlay display size");
    r.ui.initialize(*view, static_cast<float>(client.bottom - client.top));
    r.graphics = std::make_unique<Dx12Renderer>(chain, found->second.queue.Get());
    if (!r.lifecycle.activate(chain_id(chain), window_id(desc.OutputWindow)))
        throw std::runtime_error("Renderer activation interrupted by swap-chain replacement");
    application().log("DX12 overlay initialized: " + std::to_string(desc.BufferDesc.Width) + "x" +
                      std::to_string(desc.BufferDesc.Height));
    return true;
}

void render(IDXGISwapChain *chain) {
    auto &r = renderer();
    std::lock_guard lock(r.mutex);
    if (!ready || r.lifecycle.phase() == RenderLifecycle::Phase::Failed ||
        r.lifecycle.phase() == RenderLifecycle::Phase::Replacing)
        return;
    try {
        if (const auto active = r.lifecycle.active(); active && active->chain != chain_id(chain))
            return;
        if (r.lifecycle.phase() == RenderLifecycle::Phase::Waiting &&
            !initialize_renderer(r, chain))
            return;
        const auto view = application().view();
        if (!view)
            return;
        ImGui::SetCurrentContext(r.imgui);
        r.stats.visible = r.open;
        set_input_capture(r.open, r.window);
        r.stats.relics_readable = std::holds_alternative<EquippedRelics>(view->inspection);
        if (!r.open)
            return;
        std::optional<EditorAction> action;
        r.graphics->render(chain, [&] {
            r.ui.draw(*view, application().pending(), action, r.open);
            set_input_capture(r.open, r.window);
        });
        if (action)
            application().submit(*view, std::move(*action));
        ++r.stats.frames;
    } catch (const std::exception &error) {
        stop_renderer(r, std::string("Overlay stopped: ") + error.what());
    }
}

thread_local bool presenting = false;
HRESULT STDMETHODCALLTYPE present_hook(IDXGISwapChain *chain, UINT sync, UINT flags) {
    const bool nested = presenting;
    presenting = true;
    if (!nested && (flags & DXGI_PRESENT_TEST) == 0)
        render(chain);
    const auto result = original_present(chain, sync, flags);
    presenting = nested;
    return result;
}
HRESULT STDMETHODCALLTYPE present1_hook(IDXGISwapChain1 *chain, UINT sync, UINT flags,
                                        const DXGI_PRESENT_PARAMETERS *parameters) {
    const bool nested = presenting;
    presenting = true;
    if (!nested && (flags & DXGI_PRESENT_TEST) == 0)
        render(chain);
    const auto result = original_present1(chain, sync, flags, parameters);
    presenting = nested;
    return result;
}
HRESULT STDMETHODCALLTYPE resize_hook(IDXGISwapChain *chain, UINT count, UINT width, UINT height,
                                      DXGI_FORMAT format, UINT flags) {
    auto &r = renderer();
    std::lock_guard lock(r.mutex);
    if (const auto active = r.lifecycle.active(); active && active->chain == chain_id(chain)) {
        ImGui::SetCurrentContext(r.imgui);
        try {
            reset_resources(r);
            ++r.stats.resizes;
        } catch (const std::exception &error) {
            stop_renderer(r, error.what());
        }
    }
    return original_resize(chain, count, width, height, format, flags);
}

struct SwapChainCreation {
    Renderer &state;
    SwapChainCreation(const SwapChainCreation &) = delete;
    SwapChainCreation &operator=(const SwapChainCreation &) = delete;
    SwapChainCreation(Renderer &r, HWND window) : state(r) {
        std::lock_guard lock(state.mutex);
        if (state.lifecycle.begin_replacement(window_id(window))) {
            try {
                ImGui::SetCurrentContext(state.imgui);
                reset_resources(state);
            } catch (const std::exception &error) {
                stop_renderer(state,
                              std::string("Overlay stopped during swap-chain replacement: ") +
                                  error.what());
            }
        }
    }
    ~SwapChainCreation() {
        std::lock_guard lock(state.mutex);
        state.lifecycle.end_replacement();
    }
};

void remember_queue(Renderer &r, IUnknown *device, IDXGISwapChain *chain, HWND window) {
    if (!chain || !device)
        return;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))) ||
        queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return;
    std::erase_if(r.queues, [window](const auto &entry) { return entry.second.window == window; });
    r.queues[chain] = {window, queue};
    application().log("Captured the swap chain's direct command queue");
}
HRESULT STDMETHODCALLTYPE create_hook(IDXGIFactory *factory, IUnknown *device,
                                      DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **chain) {
    auto &r = renderer();
    const auto window = desc ? desc->OutputWindow : nullptr;
    // DXGI may send window messages. Keep input unlocked while the original runs.
    SwapChainCreation creation(r, window);
    const auto result = original_create(factory, device, desc, chain);
    if (SUCCEEDED(result) && chain) {
        std::lock_guard lock(r.mutex);
        remember_queue(r, device, *chain, window);
    }
    return result;
}
HRESULT STDMETHODCALLTYPE create_hwnd_hook(IDXGIFactory2 *factory, IUnknown *device, HWND window,
                                           const DXGI_SWAP_CHAIN_DESC1 *desc,
                                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                           IDXGIOutput *output, IDXGISwapChain1 **chain) {
    auto &r = renderer();
    SwapChainCreation creation(r, window);
    const auto result =
        original_create_hwnd(factory, device, window, desc, fullscreen, output, chain);
    if (SUCCEEDED(result) && chain) {
        std::lock_guard lock(r.mutex);
        remember_queue(r, device, *chain, window);
    }
    return result;
}
} // namespace

bool install_overlay() {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"ERNRelicsDX12Probe";
    RegisterClassW(&wc);
    const auto window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16,
                                        nullptr, nullptr, instance, nullptr);
    if (!window) {
        application().log("Cannot create DX12 probe window");
        return false;
    }
    std::array<void *, 5> targets{};
    bool installed = false;
    try {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<IDXGISwapChain1> chain;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create probe factory");
        auto result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        if (FAILED(result)) {
            ComPtr<IDXGIAdapter> warp;
            check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "Find WARP adapter");
            result = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        }
        check(result, "Create probe device");
        D3D12_COMMAND_QUEUE_DESC qdesc{};
        qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue)), "Create probe queue");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = desc.Height = 16;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        check(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc, nullptr, nullptr, &chain),
              "Create probe swap chain");
        const auto swap_vtable = *reinterpret_cast<void ***>(chain.Get());
        const auto factory_vtable = *reinterpret_cast<void ***>(factory.Get());
        targets = {swap_vtable[8], swap_vtable[22], swap_vtable[13], factory_vtable[10],
                   factory_vtable[15]};
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
            throw std::runtime_error("MinHook initialization failed");
        const std::array<void *, 5> detours{
            reinterpret_cast<void *>(present_hook), reinterpret_cast<void *>(present1_hook),
            reinterpret_cast<void *>(resize_hook), reinterpret_cast<void *>(create_hook),
            reinterpret_cast<void *>(create_hwnd_hook)};
        const std::array<void **, 5> originals{reinterpret_cast<void **>(&original_present),
                                               reinterpret_cast<void **>(&original_present1),
                                               reinterpret_cast<void **>(&original_resize),
                                               reinterpret_cast<void **>(&original_create),
                                               reinterpret_cast<void **>(&original_create_hwnd)};
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (MH_CreateHook(targets[i], detours[i], originals[i]) != MH_OK ||
                MH_QueueEnableHook(targets[i]) != MH_OK)
                throw std::runtime_error("Cannot install DX12 hook " + std::to_string(i));
        }
        if (MH_ApplyQueued() != MH_OK)
            throw std::runtime_error("Cannot enable DX12 hooks");
        if (!install_input_hooks())
            throw std::runtime_error("Input capture initialization failed");
        ready = true;
        installed = true;
        application().log("DX12 hooks ready. Load this DLL early through ME3.");
    } catch (const std::exception &error) {
        application().log(error.what());
        for (const auto target : targets) {
            if (target) {
                MH_DisableHook(target);
                MH_RemoveHook(target);
            }
        }
    }
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, instance);
    return installed;
}

bool overlay_ready() { return ready && application().view() != nullptr; }
OverlayStats overlay_stats() {
    std::lock_guard lock(renderer().mutex);
    auto stats = renderer().stats;
    stats.visible = renderer().open;
    return stats;
}
} // namespace ern
