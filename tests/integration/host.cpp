#include "app/host_protocol.hpp"
#include "app/overlay.hpp"
#include "support/synthetic_rules.hpp"
#include "support/synthetic_assets.hpp"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
extern "C" __declspec(dllexport) void ern_host_set_location(ern::HostLocation location);
namespace {
alignas(16) std::array<std::byte, 0x2000> player{};
alignas(16) std::array<std::byte, 0x1000> items{};
alignas(16) std::array<std::byte, 0x100> game{};
alignas(16) std::array<std::byte, 0x17500> world{};
alignas(16) std::array<std::array<std::byte, 0x100>, ern::relic_slot_count> entries{};
ern::Address item_pointer = reinterpret_cast<ern::Address>(items.data());
ern::Address game_pointer = reinterpret_cast<ern::Address>(game.data());
ern::Address world_pointer = reinterpret_cast<ern::Address>(world.data());
std::mutex fixture_memory;
bool fixture_range(ern::Address address, std::size_t size) {
    const std::array<std::pair<ern::Address, std::size_t>, 8> ranges{
        {{reinterpret_cast<ern::Address>(player.data()), sizeof(player)},
         {reinterpret_cast<ern::Address>(items.data()), sizeof(items)},
         {reinterpret_cast<ern::Address>(game.data()), sizeof(game)},
         {reinterpret_cast<ern::Address>(world.data()), sizeof(world)},
         {reinterpret_cast<ern::Address>(entries.data()), sizeof(entries)},
         {reinterpret_cast<ern::Address>(&item_pointer), sizeof(item_pointer)},
         {reinterpret_cast<ern::Address>(&game_pointer), sizeof(game_pointer)},
         {reinterpret_cast<ern::Address>(&world_pointer), sizeof(world_pointer)}}};
    return std::any_of(ranges.begin(), ranges.end(), [&](const auto &range) {
        return address >= range.first && address - range.first <= range.second &&
               size <= range.second - (address - range.first);
    });
}
bool fixture_read(ern::Address address, std::span<std::byte> output) {
    std::lock_guard lock(fixture_memory);
    if (!fixture_range(address, output.size()))
        return false;
    std::memcpy(output.data(), reinterpret_cast<const void *>(address), output.size());
    return true;
}
bool fixture_write(ern::Address address, std::span<const std::byte> input) {
    std::lock_guard lock(fixture_memory);
    if (!fixture_range(address, input.size()))
        return false;
    std::memcpy(reinterpret_cast<void *>(address), input.data(), input.size());
    return true;
}
const auto fixture_rules = synthetic_rules();
const auto fixture_assets = synthetic_assets();
ern::HostFixture fixture{ern::fixture_magic,
                         {reinterpret_cast<ern::Address>(&item_pointer),
                          reinterpret_cast<ern::Address>(&game_pointer),
                          reinterpret_cast<ern::Address>(&world_pointer)},
                         &fixture_rules,
                         &fixture_assets,
                         fixture_read,
                         fixture_write};
UINT pending_width = 0;
UINT pending_height = 0;
using ReadyFn = BOOL (*)();
using StatsFn = BOOL (*)(ern::OverlayStats *);

template <typename T> void put(void *destination, const T &value) {
    std::memcpy(destination, &value, sizeof(value));
}
void check(HRESULT result, const char *operation) {
    if (FAILED(result))
        throw std::runtime_error(operation);
}

void initialize_fixture(bool ui_preview) {
    put(game.data() + 8, reinterpret_cast<ern::Address>(player.data()));
    put(world.data() + 0x174E8, reinterpret_cast<ern::Address>(player.data()));
    const std::array<std::array<std::uint32_t, 3>, 3> effects =
        ui_preview ? std::array<std::array<std::uint32_t, 3>, 3>{{{7000400, 7000601, 7001801},
                                                                  {7000401, 7000602, 7001802},
                                                                  {7000402, 7000601, 7001802}}}
                   : std::array<std::array<std::uint32_t, 3>, 3>{{{7000402, 7000602, 7001802},
                                                                  {7100110, 7120100, UINT32_MAX},
                                                                  {10000, 7000402, UINT32_MAX}}};
    const std::array<std::uint32_t, 3> ids =
        ui_preview ? std::array<std::uint32_t, 3>{0x800000CAu, 0x80000069u, 0x8000006Fu}
                   : std::array<std::uint32_t, 3>{0x800F4AE4u, 0x800007D0u, 0x80002710u};
    for (std::uint16_t i = 0; i < 3; ++i) {
        put(player.data() + 0x2F4 + i * 4, std::uint16_t(i + 1));
        put(items.data() + 8 + (i + 1) * 8, reinterpret_cast<ern::Address>(entries[i].data()));
        put(entries[i].data() + 0x0C, ids[i]);
        put(entries[i].data() + 0x18, effects[i]);
        put(entries[i].data() + 0x40,
            std::array<std::uint32_t, 3>{UINT32_MAX, UINT32_MAX, UINT32_MAX});
    }
    for (std::uint16_t i = 3; i < ern::relic_slot_count; ++i) {
        put(player.data() + 0x2F4 + i * 4, std::uint16_t(i + 1));
        put(items.data() + 8 + (i + 1) * 8, reinterpret_cast<ern::Address>(entries[i].data()));
        put(entries[i].data() + 0x0C, std::uint32_t(0x80000000u | 2000000u));
        put(entries[i].data() + 0x18, std::array<std::uint32_t, 3>{7000401, 7000602, 7001802});
        put(entries[i].data() + 0x40, std::array<std::uint32_t, 3>{UINT32_MAX, UINT32_MAX, 900001});
    }
}

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_KEYDOWN && wparam == VK_F6) {
        static bool outside = false;
        outside = !outside;
        ern_host_set_location(outside ? ern::HostLocation::Expedition
                                      : ern::HostLocation::Roundtable);
        return 0;
    }
    if (message == WM_SIZE && wparam != SIZE_MINIMIZED) {
        pending_width = LOWORD(lparam);
        pending_height = HIWORD(lparam);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

class HostGraphics {
  public:
    void initialize(HWND window) {
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create host DXGI factory");
        auto result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
        if (FAILED(result)) {
            ComPtr<IDXGIAdapter> warp;
            check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "Find WARP adapter");
            result = D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
        }
        check(result, "Create host DX12 device");
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)), "Create host queue");
        create_chain(factory.Get(), window, 1200, 800);
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = 3;
        check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&heap_)), "Create host RTV heap");
        check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator_)),
              "Create host allocator");
        check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(),
                                         nullptr, IID_PPV_ARGS(&commands_)),
              "Create host commands");
        check(commands_->Close(), "Close host commands");
        check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
              "Create host fence");
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_)
            throw std::runtime_error("Create host event");
        create_buffers();
        pending_width = pending_height = 0;
    }
    void resize(UINT width, UINT height) {
        wait();
        buffers_.clear();
        check(chain_->ResizeBuffers(3, width, height, DXGI_FORMAT_UNKNOWN, 0),
              "Resize host swap chain");
        create_buffers();
    }
    void recreate(HWND window) {
        wait();
        check(allocator_->Reset(), "Reset host allocator before recreation");
        check(commands_->Reset(allocator_.Get(), nullptr), "Reset host commands before recreation");
        check(commands_->Close(), "Close host commands before recreation");
        buffers_.clear();
        chain_.Reset();
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create replacement DXGI factory");
        create_chain(factory.Get(), window, 960, 640);
        create_buffers();
    }
    void reject_replacement(HWND window) {
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create failed-replacement factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        check(chain_->GetDesc1(&desc), "Read existing swap-chain description");
        ComPtr<IDXGISwapChain1> replacement;
        // A live flip-model chain still owns this window, so creation must fail.
        const auto result = factory->CreateSwapChainForHwnd(queue_.Get(), window, &desc, nullptr,
                                                            nullptr, &replacement);
        if (SUCCEEDED(result) || replacement)
            throw std::runtime_error("Replacement unexpectedly accepted an occupied window");
    }
    void render() {
        wait();
        check(allocator_->Reset(), "Reset host allocator");
        check(commands_->Reset(allocator_.Get(), nullptr), "Reset host commands");
        const auto index = chain_->GetCurrentBackBufferIndex();
        auto rtv = heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(index) *
                   device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers_[index].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands_->ResourceBarrier(1, &barrier);
        const float color[]{0.025f, 0.03f, 0.045f, 1.f};
        commands_->ClearRenderTargetView(rtv, color, 0, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commands_->ResourceBarrier(1, &barrier);
        check(commands_->Close(), "Close host commands");
        ID3D12CommandList *lists[]{commands_.Get()};
        queue_->ExecuteCommandLists(1, lists);
        check(chain_->Present(1, 0), "Present host frame");
        check(queue_->Signal(fence_.Get(), ++fence_value_), "Signal host fence");
    }
    void wait() {
        if (fence_ && fence_->GetCompletedValue() < fence_value_) {
            check(fence_->SetEventOnCompletion(fence_value_, event_), "Set host fence event");
            if (WaitForSingleObject(event_, 5000) != WAIT_OBJECT_0)
                throw std::runtime_error("Host GPU timeout");
        }
    }
    ~HostGraphics() {
        if (event_)
            CloseHandle(event_);
    }

  private:
    void create_chain(IDXGIFactory4 *factory, HWND window, UINT width, UINT height) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 3;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> base_chain;
        check(factory->CreateSwapChainForHwnd(queue_.Get(), window, &desc, nullptr, nullptr,
                                              &base_chain),
              "Create host swap chain");
        check(base_chain.As(&chain_), "Query host swap chain 3");
        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    }
    void create_buffers() {
        buffers_.resize(3);
        auto rtv = heap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < 3; ++i) {
            check(chain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i])), "Get host back buffer");
            device_->CreateRenderTargetView(buffers_[i].Get(), nullptr, rtv);
            rtv.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
    }
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<IDXGISwapChain3> chain_;
    ComPtr<ID3D12DescriptorHeap> heap_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    std::vector<ComPtr<ID3D12Resource>> buffers_;
    HANDLE event_ = nullptr;
    UINT64 fence_value_ = 0;
};
} // namespace

extern "C" __declspec(dllexport) const ern::HostFixture *ern_host_fixture() { return &fixture; }
extern "C" __declspec(dllexport) void ern_host_set_location(ern::HostLocation location) {
    std::lock_guard lock(fixture_memory);
    put(game.data() + 0xD8, std::uint8_t(location == ern::HostLocation::Expedition ? 1 : 0));
    put(world.data() + 0x174E8, location == ern::HostLocation::Loading
                                    ? ern::Address(0)
                                    : reinterpret_cast<ern::Address>(player.data()));
    world_pointer = location == ern::HostLocation::Unavailable
                        ? 0
                        : reinterpret_cast<ern::Address>(world.data());
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int show) {
    const bool smoke_test =
        std::wstring_view(arguments).find(L"--smoke-test") != std::wstring_view::npos;
    const bool self_test =
        smoke_test || std::wstring_view(arguments).find(L"--self-test") != std::wstring_view::npos;
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto directory = std::filesystem::path(executable).parent_path();
    std::ofstream report(directory / (self_test ? "host-test.txt" : "host-session.txt"));
    try {
        initialize_fixture(!self_test);
        if (std::wstring_view(arguments).find(L"--outside-roundtable") != std::wstring_view::npos)
            ern_host_set_location(ern::HostLocation::Expedition);
        const auto dll = LoadLibraryW((directory / "ern-relics.dll").c_str());
        if (!dll)
            throw std::runtime_error("Cannot load ern-relics.dll");
        const auto ready_fn = reinterpret_cast<ReadyFn>(GetProcAddress(dll, "ern_overlay_ready"));
        const auto stats_fn = reinterpret_cast<StatsFn>(GetProcAddress(dll, "ern_query_stats"));
        const auto edit_cycle_fn =
            reinterpret_cast<ReadyFn>(GetProcAddress(dll, "ern_test_fixture_edit_cycle"));
        if (!ready_fn || !stats_fn || (!smoke_test && !edit_cycle_fn))
            throw std::runtime_error("DLL is missing required host exports");
        if (smoke_test && edit_cycle_fn)
            throw std::runtime_error("Packaged DLL contains integration fixture exports");
        const auto deadline = GetTickCount64() + 15000;
        while (!ready_fn() && GetTickCount64() < deadline)
            Sleep(20);
        if (!ready_fn())
            throw std::runtime_error("Overlay hooks did not become ready. Check ern-relics.log");
        WNDCLASSW wc{};
        wc.lpfnWndProc = host_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ERNRelicsHost";
        RegisterClassW(&wc);
        const auto window = CreateWindowExW(0, wc.lpszClassName, L"ERN Relics DX12 test host",
                                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1220,
                                            850, nullptr, nullptr, instance, nullptr);
        if (!window)
            throw std::runtime_error("Cannot create host window");
        HostGraphics graphics;
        graphics.initialize(window);
        ShowWindow(window, self_test ? SW_HIDE : show);
        MSG message{};
        unsigned int frames = 0;
        bool closed = false;
        bool reopened = false;
        std::uint64_t before_recreate = 0;
        std::uint64_t before_failed_replacement = 0;
        while (message.message != WM_QUIT) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (message.message == WM_QUIT)
                break;
            if (pending_width && pending_height) {
                graphics.resize(pending_width, pending_height);
                pending_width = pending_height = 0;
            }
            graphics.render();
            ++frames;
            if (self_test) {
                ern::OverlayStats stats{};
                stats_fn(&stats);
                if (frames == 20) {
                    if (stats.frames == 0 || !stats.relics_readable || !stats.visible)
                        throw std::runtime_error("DLL did not render readable fixture data");
                    SendMessageW(window, WM_KEYDOWN, VK_F8, 0);
                    SendMessageW(window, WM_KEYUP, VK_F8, 0);
                }
                if (frames == 30) {
                    closed = !stats.visible;
                    SendMessageW(window, WM_KEYDOWN, VK_F8, 0);
                    SendMessageW(window, WM_KEYUP, VK_F8, 0);
                }
                if (frames == 40) {
                    reopened = stats.visible;
                    graphics.resize(960, 640);
                }
                if (frames == 46) {
                    before_failed_replacement = stats.frames;
                    graphics.reject_replacement(window);
                }
                if (frames == 50 && stats.frames < before_failed_replacement + 4)
                    throw std::runtime_error("Overlay stopped after a rejected replacement");
                if (frames == 55) {
                    before_recreate = stats.frames;
                    auto recreation =
                        std::async(std::launch::async, [&] { graphics.recreate(window); });
                    while (recreation.wait_for(std::chrono::milliseconds(0)) !=
                           std::future_status::ready) {
                        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&message);
                            DispatchMessageW(&message);
                        }
                        Sleep(1);
                    }
                    recreation.get();
                }
                if (frames == 60 && stats.frames < before_recreate + 5)
                    throw std::runtime_error(
                        "Overlay stopped rendering after swap-chain recreation");
                if (frames == 65) {
                    SendMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
                    SendMessageW(window, WM_KEYUP, VK_ESCAPE, 0);
                }
                if (frames == 75) {
                    SendMessageW(window, WM_KEYDOWN, VK_F8, 0);
                    SendMessageW(window, WM_KEYUP, VK_F8, 0);
                }
                if (frames >= 100) {
                    stats_fn(&stats);
                    if (!closed || !reopened || stats.resizes == 0 || stats.toggles < 4 ||
                        !stats.visible || !stats.relics_readable || stats.frames < 50)
                        throw std::runtime_error(
                            "Overlay hotkey, Escape, resize, or fixture test failed");
                    if (!smoke_test && !edit_cycle_fn())
                        throw std::runtime_error(
                            "Runtime edit, journal, verification, or undo test failed");
                    report
                        << (smoke_test
                                ? "PASS: packaged production DLL loaded without fixture exports, synthetic metadata rendered, F8 and Escape toggled overlay, swap chain resized and recreated, rejected replacement preserved rendering\n"
                                : "PASS: actual DLL loaded, DX12 frames rendered, local or synthetic metadata loaded, F8 and Escape toggled overlay, swap chain resized and recreated, rejected replacement preserved rendering, expedition/loading/unavailable writes rejected without journaling, Undo blocked outside Roundtable, unique writes rejected, normal and Deep effects and curses edited through session batches and Undo verified\n")
                        << "frames=" << stats.frames << " toggles=" << stats.toggles
                        << " resizes=" << stats.resizes << '\n';
                    report.flush();
                    graphics.wait();
                    DestroyWindow(window);
                    return 0;
                }
            }
            Sleep(self_test ? 2 : 8);
        }
        graphics.wait();
        if (self_test)
            throw std::runtime_error("Self-test closed before completing integration checks");
        return 0;
    } catch (const std::exception &error) {
        report << "FAIL: " << error.what() << '\n';
        report.flush();
        if (!self_test)
            MessageBoxA(nullptr, error.what(), "ERN Relics test host", MB_OK | MB_ICONERROR);
        return 1;
    }
}
