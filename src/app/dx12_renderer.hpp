#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <functional>
#include <memory>
#include <vector>

namespace ern {
class Dx12Renderer {
  public:
    Dx12Renderer(IDXGISwapChain *chain, ID3D12CommandQueue *captured_queue);
    ~Dx12Renderer();
    Dx12Renderer(const Dx12Renderer &) = delete;
    Dx12Renderer &operator=(const Dx12Renderer &) = delete;
    void render(IDXGISwapChain *chain, const std::function<void()> &draw);
    void wait_idle() const;

  private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> buffer;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    };
    struct CloseEvent {
        void operator()(void *event) const { CloseHandle(event); }
    };
    bool wait_for(UINT64 value) const;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12DescriptorHeap> rtv_heap, srv_heap;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    std::unique_ptr<void, CloseEvent> fence_event;
    UINT64 next_fence = 0;
    std::vector<Frame> frames;
    bool backend_ready = false;
};
} // namespace ern
