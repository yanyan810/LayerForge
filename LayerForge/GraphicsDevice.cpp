#include "GraphicsDevice.h"

#include <format>

using Microsoft::WRL::ComPtr;

namespace {
std::string HrMessage(const char* action, HRESULT hr) {
    return std::format("{} failed (0x{:08X}).", action, static_cast<unsigned>(hr));
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}
}

bool GraphicsDevice::Initialize(HWND window, uint32_t width, uint32_t height, std::string& error) {
    window_ = window;
    width_ = width;
    height_ = height;

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif

    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) { error = HrMessage("CreateDXGIFactory2", hr); return false; }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; factory_->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) break;
        adapter.Reset();
    }
    if (!device_) {
        hr = factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        if (SUCCEEDED(hr)) hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    }
    if (FAILED(hr) || !device_) { error = HrMessage("D3D12CreateDevice", hr); return false; }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_));
    if (FAILED(hr)) { error = HrMessage("CreateCommandQueue", hr); return false; }

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = width;
    swapDesc.Height = height;
    swapDesc.Format = BackBufferFormat();
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = FrameCount;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory_->CreateSwapChainForHwnd(commandQueue_.Get(), window, &swapDesc, nullptr, nullptr, &swapChain1);
    if (SUCCEEDED(hr)) hr = swapChain1.As(&swapChain_);
    if (FAILED(hr)) { error = HrMessage("CreateSwapChainForHwnd", hr); return false; }
    factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = FrameCount;
    hr = device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_));
    if (FAILED(hr)) { error = HrMessage("Create RTV heap", hr); return false; }
    rtvIncrement_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 256;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device_->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap_));
    if (FAILED(hr)) { error = HrMessage("Create SRV heap", hr); return false; }
    srvIncrement_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (auto& allocator : allocators_) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) { error = HrMessage("CreateCommandAllocator", hr); return false; }
    }
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(&commandList_));
    if (FAILED(hr)) { error = HrMessage("CreateCommandList", hr); return false; }
    commandList_->Close();

    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) { error = HrMessage("CreateFence", hr); return false; }
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) { error = "CreateEvent failed."; return false; }
    return CreateRenderTargets(error);
}

bool GraphicsDevice::CreateRenderTargets(std::string& error) {
    auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t i = 0; i < FrameCount; ++i) {
        HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
        if (FAILED(hr)) { error = HrMessage("Get swap-chain buffer", hr); return false; }
        device_->CreateRenderTargetView(backBuffers_[i].Get(), nullptr, handle);
        handle.ptr += rtvIncrement_;
    }
    return true;
}

void GraphicsDevice::ReleaseRenderTargets() { for (auto& buffer : backBuffers_) buffer.Reset(); }

uint32_t GraphicsDevice::AllocateSrv() { return nextSrv_ < 256 ? nextSrv_++ : UINT32_MAX; }
D3D12_CPU_DESCRIPTOR_HANDLE GraphicsDevice::CpuSrv(uint32_t index) const { auto h = srvHeap_->GetCPUDescriptorHandleForHeapStart(); h.ptr += static_cast<SIZE_T>(index) * srvIncrement_; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE GraphicsDevice::GpuSrv(uint32_t index) const { auto h = srvHeap_->GetGPUDescriptorHandleForHeapStart(); h.ptr += static_cast<UINT64>(index) * srvIncrement_; return h; }

bool GraphicsDevice::CreateTexture(const ImageData& image, Texture& texture, std::string& error) {
    if (!image.IsValid()) { error = "Decoded image data is invalid."; return false; }
    WaitForGpu();

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = image.width;
    desc.Height = image.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) { error = HrMessage("Create image texture", hr); return false; }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0, uploadSize = 0;
    device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &uploadSize);
    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> upload;
    hr = device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
    if (FAILED(hr)) { error = HrMessage("Create upload buffer", hr); return false; }

    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    const size_t sourceStride = static_cast<size_t>(image.width) * 4;
    for (uint32_t y = 0; y < image.height; ++y) memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch, image.rgbaPixels.data() + static_cast<size_t>(y) * sourceStride, sourceStride);
    upload->Unmap(0, nullptr);

    allocators_[frameIndex_]->Reset();
    commandList_->Reset(allocators_[frameIndex_].Get(), nullptr);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = resource.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = upload.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; source.PlacedFootprint = footprint;
    commandList_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    auto barrier = Transition(resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList_->ResourceBarrier(1, &barrier);
    commandList_->Close();
    ID3D12CommandList* lists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);
    WaitForGpu();

    uint32_t index = texture.descriptorIndex;
    if (index == UINT32_MAX) index = AllocateSrv();
    if (index == UINT32_MAX) { error = "The SRV descriptor heap is full."; return false; }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    auto cpu = CpuSrv(index);
    device_->CreateShaderResourceView(resource.Get(), &srv, cpu);
    texture.resource = std::move(resource);
    texture.cpuHandle = cpu;
    texture.gpuHandle = GpuSrv(index);
    texture.descriptorIndex = index;
    texture.width = image.width;
    texture.height = image.height;
    return true;
}

void GraphicsDevice::BeginFrame() {
    allocators_[frameIndex_]->Reset();
    commandList_->Reset(allocators_[frameIndex_].Get(), nullptr);
    auto barrier = Transition(backBuffers_[frameIndex_].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList_->ResourceBarrier(1, &barrier);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += static_cast<SIZE_T>(frameIndex_) * rtvIncrement_;
    constexpr float clear[] = { 0.055f, 0.065f, 0.08f, 1.0f };
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList_->ClearRenderTargetView(rtv, clear, 0, nullptr);
    ID3D12DescriptorHeap* heaps[] = { srvHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, heaps);
}

void GraphicsDevice::EndFrame() {
    auto barrier = Transition(backBuffers_[frameIndex_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList_->ResourceBarrier(1, &barrier);
    commandList_->Close();
    ID3D12CommandList* lists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, lists);
    swapChain_->Present(1, 0);
    MoveToNextFrame();
}

void GraphicsDevice::MoveToNextFrame() {
    const uint64_t signal = ++fenceValue_;
    commandQueue_->Signal(fence_.Get(), signal);
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    if (fence_->GetCompletedValue() < signal) {
        fence_->SetEventOnCompletion(signal, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void GraphicsDevice::WaitForGpu() {
    if (!commandQueue_ || !fence_) return;
    const uint64_t signal = ++fenceValue_;
    commandQueue_->Signal(fence_.Get(), signal);
    if (fence_->GetCompletedValue() < signal) { fence_->SetEventOnCompletion(signal, fenceEvent_); WaitForSingleObject(fenceEvent_, INFINITE); }
}

void GraphicsDevice::Resize(uint32_t width, uint32_t height) {
    if (!swapChain_ || width == 0 || height == 0 || (width == width_ && height == height_)) return;
    WaitForGpu(); ReleaseRenderTargets();
    if (SUCCEEDED(swapChain_->ResizeBuffers(FrameCount, width, height, BackBufferFormat(), 0))) {
        width_ = width; height_ = height; frameIndex_ = swapChain_->GetCurrentBackBufferIndex(); std::string ignored; CreateRenderTargets(ignored);
    }
}

void GraphicsDevice::Shutdown() {
    WaitForGpu(); ReleaseRenderTargets();
    if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
    commandList_.Reset(); for (auto& a : allocators_) a.Reset(); fence_.Reset(); srvHeap_.Reset(); rtvHeap_.Reset(); swapChain_.Reset(); commandQueue_.Reset(); device_.Reset(); factory_.Reset();
}
