#include "app/dx12_renderer.hpp"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <stdexcept>
#include <string>

namespace ern {
namespace {
void check(HRESULT result, const char *operation) {
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) +
                                 " failed: " + std::to_string(static_cast<unsigned long>(result)));
}

} // namespace
bool Dx12Renderer::wait_for(UINT64 value) const {
    const auto &r = *this;
    if (value == 0 || r.fence->GetCompletedValue() >= value)
        return true;
    if (FAILED(r.fence->SetEventOnCompletion(value, r.fence_event.get())))
        return false;
    return WaitForSingleObject(r.fence_event.get(), 2000) == WAIT_OBJECT_0;
}

Dx12Renderer::Dx12Renderer(IDXGISwapChain *chain, ID3D12CommandQueue *captured_queue) {
    auto &r = *this;
    DXGI_SWAP_CHAIN_DESC desc{};
    check(chain->GetDesc(&desc), "GetDesc");
    check(chain->GetDevice(IID_PPV_ARGS(&r.device)), "Get D3D12 device");
    r.queue = captured_queue;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = desc.BufferCount;
    check(r.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&r.rtv_heap)), "Create RTV heap");
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    check(r.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&r.srv_heap)), "Create SRV heap");
    r.frames.resize(desc.BufferCount);
    auto rtv = r.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    const auto stride = r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    DXGI_FORMAT format = desc.BufferDesc.Format;
    if (format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
        format = DXGI_FORMAT_B8G8R8A8_UNORM;
    for (UINT i = 0; i < desc.BufferCount; ++i) {
        auto &frame = r.frames[i];
        check(chain->GetBuffer(i, IID_PPV_ARGS(&frame.buffer)), "Get back buffer");
        frame.rtv = rtv;
        D3D12_RENDER_TARGET_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        r.device->CreateRenderTargetView(frame.buffer.Get(), &view, rtv);
        rtv.ptr += stride;
        check(r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&frame.allocator)),
              "Create allocator");
    }
    check(r.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      r.frames[0].allocator.Get(), nullptr,
                                      IID_PPV_ARGS(&r.commands)),
          "Create command list");
    check(r.commands->Close(), "Close initial command list");
    check(r.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.fence)), "Create fence");
    r.fence_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!r.fence_event)
        throw std::runtime_error("Create fence event failed");
    ImGui_ImplDX12_InitInfo init{};
    init.Device = r.device.Get();
    init.CommandQueue = r.queue.Get();
    init.NumFramesInFlight = static_cast<int>(desc.BufferCount);
    init.RTVFormat = format;
    init.SrvDescriptorHeap = r.srv_heap.Get();
    init.UserData = &r;
    init.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *cpu,
                                   D3D12_GPU_DESCRIPTOR_HANDLE *gpu) {
        auto *state = static_cast<Dx12Renderer *>(info->UserData);
        *cpu = state->srv_heap->GetCPUDescriptorHandleForHeapStart();
        *gpu = state->srv_heap->GetGPUDescriptorHandleForHeapStart();
    };
    init.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE,
                                  D3D12_GPU_DESCRIPTOR_HANDLE) {};
    if (!ImGui_ImplDX12_Init(&init))
        throw std::runtime_error("ImGui DX12 initialization failed");
    r.backend_ready = true;
}
Dx12Renderer::~Dx12Renderer() {
    if (backend_ready)
        ImGui_ImplDX12_Shutdown();
}
void Dx12Renderer::wait_idle() const {
    if (!wait_for(next_fence))
        throw std::runtime_error("Timed out waiting for overlay GPU work");
}
void Dx12Renderer::render(IDXGISwapChain *chain, const std::function<void()> &draw) {
    auto &r = *this;
    ComPtr<IDXGISwapChain3> chain3;
    check(chain->QueryInterface(IID_PPV_ARGS(&chain3)), "Get swap chain 3");
    auto &frame = r.frames.at(chain3->GetCurrentBackBufferIndex());
    // Keep ImGui's transient buffers safe even when hidden frames change the
    // swap-chain index without advancing ImGui's frame index.
    if (!wait_for(r.next_fence))
        throw std::runtime_error("Frame fence timed out");
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().MouseDrawCursor = true;
    ImGui::NewFrame();
    draw();
    ImGui::Render();
    check(frame.allocator->Reset(), "Reset allocator");
    check(r.commands->Reset(frame.allocator.Get(), nullptr), "Reset command list");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.buffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    r.commands->ResourceBarrier(1, &barrier);
    r.commands->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
    ID3D12DescriptorHeap *heaps[] = {r.srv_heap.Get()};
    r.commands->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), r.commands.Get());
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    r.commands->ResourceBarrier(1, &barrier);
    check(r.commands->Close(), "Close command list");
    ID3D12CommandList *lists[] = {r.commands.Get()};
    r.queue->ExecuteCommandLists(1, lists);
    ++r.next_fence;
    check(r.queue->Signal(r.fence.Get(), r.next_fence), "Signal fence");
}
} // namespace ern
