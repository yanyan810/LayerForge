#pragma once

#include "ImageData.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>

class GraphicsDevice {
public:
    static constexpr uint32_t FrameCount = 2;

    struct Texture {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        uint32_t descriptorIndex = UINT32_MAX;
        uint32_t width = 0;
        uint32_t height = 0;
        [[nodiscard]] bool IsValid() const noexcept { return resource != nullptr; }
    };

    bool Initialize(HWND window, uint32_t width, uint32_t height, std::string& error);
    void Shutdown();
    void Resize(uint32_t width, uint32_t height);
    bool CreateTexture(const ImageData& image, Texture& texture, std::string& error);
    bool PrepareTexture(const ImageData& image, Texture& texture, std::string& error);
    bool CommitTexture(Texture&& prepared, Texture& destination, std::string& error);
    void BeginFrame();
    void EndFrame();
    void WaitForGpu();

    [[nodiscard]] ID3D12Device* Device() const { return device_.Get(); }
    [[nodiscard]] ID3D12CommandQueue* CommandQueue() const { return commandQueue_.Get(); }
    [[nodiscard]] ID3D12GraphicsCommandList* CommandList() const { return commandList_.Get(); }
    [[nodiscard]] ID3D12DescriptorHeap* SrvHeap() const { return srvHeap_.Get(); }
    [[nodiscard]] DXGI_FORMAT BackBufferFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }
    uint32_t AllocateSrv();
    D3D12_CPU_DESCRIPTOR_HANDLE CpuSrv(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuSrv(uint32_t index) const;

private:
    bool CreateRenderTargets(std::string& error);
    void ReleaseRenderTargets();
    void MoveToNextFrame();

    HWND window_ = nullptr;
    uint32_t width_ = 0, height_ = 0, frameIndex_ = 0;
    uint32_t rtvIncrement_ = 0, srvIncrement_ = 0, nextSrv_ = 0;
    uint64_t fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;
    Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> backBuffers_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, FrameCount> allocators_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
};
