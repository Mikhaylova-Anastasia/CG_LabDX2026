#pragma once
#include "Common.h"

class GBuffer
{
public:
    void Initialize(
        ID3D12Device* device,
        UINT width,
        UINT height,
        UINT rtvDescriptorSize,
        UINT srvDescriptorSize);

    void OnResize(ID3D12Device* device, UINT width, UINT height);

    void CreateDescriptors(
        ID3D12Device* device,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvStart,
        D3D12_CPU_DESCRIPTOR_HANDLE srvStart,
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart);

    void TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList);
    void TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList);
    void Clear(ID3D12GraphicsCommandList* cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE GetAlbedoRtv() const { return mAlbedoRtv; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetNormalRtv() const { return mNormalRtv; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetPositionRtv() const { return mPositionRtv; }

    D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoSrvGpu() const { return mAlbedoSrvGpu; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSrvGpu() const { return mNormalSrvGpu; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetPositionSrvGpu() const { return mPositionSrvGpu; }

private:
    void BuildResources(ID3D12Device* device);

private:
    UINT mWidth = 0;
    UINT mHeight = 0;
    UINT mRtvDescriptorSize = 0;
    UINT mSrvDescriptorSize = 0;

    ComPtr<ID3D12Resource> mAlbedo;
    ComPtr<ID3D12Resource> mNormal;
    ComPtr<ID3D12Resource> mPosition;

    D3D12_CPU_DESCRIPTOR_HANDLE mAlbedoRtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE mNormalRtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE mPositionRtv{};

    D3D12_CPU_DESCRIPTOR_HANDLE mAlbedoSrvCpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE mNormalSrvCpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE mPositionSrvCpu{};

    D3D12_GPU_DESCRIPTOR_HANDLE mAlbedoSrvGpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE mNormalSrvGpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE mPositionSrvGpu{};
};