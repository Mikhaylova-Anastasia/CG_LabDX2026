#include "GBuffer.h"

void GBuffer::Initialize(
    ID3D12Device* device,
    UINT width,
    UINT height,
    UINT rtvDescriptorSize,
    UINT srvDescriptorSize)
{
    mWidth = width;
    mHeight = height;
    mRtvDescriptorSize = rtvDescriptorSize;
    mSrvDescriptorSize = srvDescriptorSize;

    BuildResources(device);
}

void GBuffer::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    mAlbedo.Reset();
    mNormal.Reset();
    mPosition.Reset();

    BuildResources(device);
}

void GBuffer::BuildResources(ID3D12Device* device)
{
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    auto makeDesc = [&](DXGI_FORMAT format)
        {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Alignment = 0;
            desc.Width = mWidth;
            desc.Height = mHeight;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            return desc;
        };

    D3D12_RESOURCE_DESC albedoDesc = makeDesc(DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_RESOURCE_DESC normalDesc = makeDesc(DXGI_FORMAT_R16G16B16A16_FLOAT);
    D3D12_RESOURCE_DESC positionDesc = makeDesc(DXGI_FORMAT_R16G16B16A16_FLOAT);

    D3D12_CLEAR_VALUE albedoClear = {};
    albedoClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    albedoClear.Color[0] = 0.0f;
    albedoClear.Color[1] = 0.0f;
    albedoClear.Color[2] = 0.0f;
    albedoClear.Color[3] = 1.0f;

    D3D12_CLEAR_VALUE hdrClear = {};
    hdrClear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrClear.Color[0] = 0.0f;
    hdrClear.Color[1] = 0.0f;
    hdrClear.Color[2] = 0.0f;
    hdrClear.Color[3] = 1.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &albedoDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &albedoClear,
        IID_PPV_ARGS(&mAlbedo)));

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &normalDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &hdrClear,
        IID_PPV_ARGS(&mNormal)));

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &positionDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &hdrClear,
        IID_PPV_ARGS(&mPosition)));
}

void GBuffer::CreateDescriptors(
    ID3D12Device* device,
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart,
    D3D12_CPU_DESCRIPTOR_HANDLE srvStart,
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuStart)
{
    mAlbedoRtv = rtvStart;
    mNormalRtv.ptr = rtvStart.ptr + mRtvDescriptorSize;
    mPositionRtv.ptr = rtvStart.ptr + 2ull * mRtvDescriptorSize;

    mAlbedoSrvCpu = srvStart;
    mNormalSrvCpu.ptr = srvStart.ptr + mSrvDescriptorSize;
    mPositionSrvCpu.ptr = srvStart.ptr + 2ull * mSrvDescriptorSize;

    mAlbedoSrvGpu = srvGpuStart;
    mNormalSrvGpu.ptr = srvGpuStart.ptr + mSrvDescriptorSize;
    mPositionSrvGpu.ptr = srvGpuStart.ptr + 2ull * mSrvDescriptorSize;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateRenderTargetView(mAlbedo.Get(), &rtvDesc, mAlbedoRtv);

    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    device->CreateRenderTargetView(mNormal.Get(), &rtvDesc, mNormalRtv);
    device->CreateRenderTargetView(mPosition.Get(), &rtvDesc, mPositionRtv);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device->CreateShaderResourceView(mAlbedo.Get(), &srvDesc, mAlbedoSrvCpu);

    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    device->CreateShaderResourceView(mNormal.Get(), &srvDesc, mNormalSrvCpu);
    device->CreateShaderResourceView(mPosition.Get(), &srvDesc, mPositionSrvCpu);
}

void GBuffer::TransitionToRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
    D3D12_RESOURCE_BARRIER barriers[3] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mAlbedo.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mNormal.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mPosition.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET)
    };

    cmdList->ResourceBarrier(3, barriers);
}

void GBuffer::TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList)
{
    D3D12_RESOURCE_BARRIER barriers[3] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mAlbedo.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mNormal.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),

        CD3DX12_RESOURCE_BARRIER::Transition(
            mPosition.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };

    cmdList->ResourceBarrier(3, barriers);
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmdList)
{
    const float black[4] = { 0, 0, 0, 1 };
    cmdList->ClearRenderTargetView(mAlbedoRtv, black, 0, nullptr);
    cmdList->ClearRenderTargetView(mNormalRtv, black, 0, nullptr);
    cmdList->ClearRenderTargetView(mPositionRtv, black, 0, nullptr);
}