#pragma once
#include "Common.h"
#include <cstring>
#include <chrono>

// Header-only to avoid changing the existing Visual Studio project.
// The host waits for the GPU after every frame and before Resize.
class TemporalAA
{
public:
    bool Enabled = true;
    bool SuperGhosting = false;
    bool Valid = false;
    UINT Frame = 0;
    XMFLOAT2 JitterUV{};

    void Reset()
    {
        Valid = false;
        Frame = 0;
        JitterUV = XMFLOAT2(0, 0);
        mGhostNext = 0;
        mGhostCount = 0;
    }

    XMMATRIX JitterProjection(CXMMATRIX projection, UINT width, UINT height)
    {
        XMFLOAT4X4 p;
        XMStoreFloat4x4(&p, projection);
        JitterUV = XMFLOAT2(0, 0);
        
        if (Enabled && !SuperGhosting && width && height)
        {
            UINT index = Frame % 8 + 1;
            float x = Halton(index, 2) - 0.5f;
            float y = Halton(index, 3) - 0.5f;
            p._31 += 2.0f * x / width;
            p._32 += 2.0f * y / height;
            JitterUV = XMFLOAT2(x / width, -y / height);
        }
        return XMLoadFloat4x4(&p);
    }

    void Initialize(ID3D12Device* device, UINT width, UINT height)
    {
        mDevice = device;
        mRtvStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        mSrvStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.NumDescriptors = 4;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mRtvHeap)));
        heap.NumDescriptors = 7;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mSrvHeap)));

        CD3DX12_ROOT_PARAMETER params[3];
        CD3DX12_DESCRIPTOR_RANGE range;
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
        params[0].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsConstants(12, 0);
        CD3DX12_DESCRIPTOR_RANGE ghostRange;
        ghostRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
        params[2].InitAsDescriptorTable(1, &ghostRange, D3D12_SHADER_VISIBILITY_PIXEL);
        CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        CD3DX12_ROOT_SIGNATURE_DESC rs(3, params, 1, &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ComPtr<ID3DBlob> blob, errors;
        ThrowIfFailed(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors));
        ThrowIfFailed(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&mRoot)));

        ComPtr<ID3DBlob> vs, resolve, display;
        auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& result)
            {
                errors.Reset();
                HRESULT hr = D3DCompileFromFile(L"Shaders/TemporalAA.hlsl", nullptr, nullptr,
                    entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &result, &errors);
                if (FAILED(hr) && errors)
                    MessageBoxA(nullptr, (const char*)errors->GetBufferPointer(), "TAA shader", MB_OK);
                ThrowIfFailed(hr);
            };
        compile("VSMain", "vs_5_0", vs);
        compile("PSResolve", "ps_5_0", resolve);
        compile("PSDisplay", "ps_5_0", display);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = mRoot.Get();
        pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS = { resolve->GetBufferPointer(), resolve->GetBufferSize() };
        pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pso.DepthStencilState.DepthEnable = FALSE;
        pso.DepthStencilState.StencilEnable = FALSE;
        pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pso.SampleDesc.Count = 1;
        ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mResolvePSO)));
        pso.PS = { display->GetBufferPointer(), display->GetBufferSize() };
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mDisplayPSO)));
        Resize(width, height);
    }

    void Resize(UINT width, UINT height)
    {
        if (!width || !height) return;
        mWidth = width;
        mHeight = height;
        Reset();
        mHistory = 0;
        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        for (UINT i = 0; i < 4; ++i)
        {
            mTextures[i].Reset();
            auto format = (i == 1) ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
                width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            D3D12_CLEAR_VALUE clear{};
            clear.Format = desc.Format;
            if (i == 0)
            {
                clear.Color[0] = 0.07f;
                clear.Color[1] = 0.07f;
                clear.Color[2] = 0.09f;
                clear.Color[3] = 1.0f;
            }
            ThrowIfFailed(mDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&mTextures[i])));
            mDevice->CreateRenderTargetView(mTextures[i].Get(), nullptr, Rtv(i));
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = desc.Format;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            for (UINT table = 0; table < 2; ++table)
            {
                if (i >= 2 && i != 2 + table) continue;
                UINT slot = table * 3 + (i < 2 ? i : 2);
                auto cpu = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
                cpu.ptr += SIZE_T(slot) * mSrvStep;
                mDevice->CreateShaderResourceView(mTextures[i].Get(), &srv, cpu);
            }
        }
       
        
        mGhostFrames.Reset();
        auto ghostDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
            width, height, static_cast<UINT16>(GhostFrames), 1);
        ThrowIfFailed(mDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &ghostDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&mGhostFrames)));
        D3D12_SHADER_RESOURCE_VIEW_DESC ghostSrv{};
        ghostSrv.Format = ghostDesc.Format;
        ghostSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        ghostSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ghostSrv.Texture2DArray.MipLevels = 1;
        ghostSrv.Texture2DArray.ArraySize = GhostFrames;
        auto ghostHandle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
        ghostHandle.ptr += SIZE_T(6) * mSrvStep;
        mDevice->CreateShaderResourceView(mGhostFrames.Get(), &ghostSrv, ghostHandle);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE MotionRtv() const { return Rtv(1); }
    D3D12_CPU_DESCRIPTOR_HANDLE ColorRtv() const { return Rtv(0); }

    void Begin(ID3D12GraphicsCommandList* cmd)
    {
       
        if (!Valid)
        {
            const float zero[4] = {};
            for (UINT i = 2; i < 4; ++i)
            {
                Transition(cmd, i, false);
                cmd->ClearRenderTargetView(Rtv(i), zero, 0, nullptr);
                Transition(cmd, i, true);
            }
        }
        Transition(cmd, 0, false);
        Transition(cmd, 1, false);
        const float zero[4] = {};
        cmd->ClearRenderTargetView(MotionRtv(), zero, 0, nullptr);
    }

    void Resolve(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE backBuffer)
    {
        Transition(cmd, 0, true);
        Transition(cmd, 1, true);
        UINT write = 1 - mHistory;
        Transition(cmd, 2 + write, false);
        ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootSignature(mRoot.Get());
        auto table = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
        table.ptr += UINT64(mHistory * 3) * mSrvStep;
        cmd->SetGraphicsRootDescriptorTable(0, table);
        auto ghostTable = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
        ghostTable.ptr += UINT64(6) * mSrvStep;
        cmd->SetGraphicsRootDescriptorTable(2, ghostTable);
        const bool ghostActive = Enabled && SuperGhosting;
        struct Constants
        {
            XMFLOAT2 InvSize;
            XMFLOAT2 Jitter;
            UINT History;
            UINT UseHistory;
            UINT Display;
            float CurrentWeight;
            UINT GhostMode;
            UINT GhostCount;
            UINT Padding0;
            UINT Padding1;
        } data{ XMFLOAT2(1.0f / mWidth, 1.0f / mHeight), JitterUV,
            mHistory, (Enabled && Valid && !ghostActive) ? 1u : 0u, write, 0.1f,
            ghostActive ? 1u : 0u, 0u, 0u, 0u };
        static_assert(sizeof(Constants) == 48, "TAA root constant layout");
        cmd->SetGraphicsRoot32BitConstants(1, 12, &data, 0);
        auto target = Rtv(2 + write);
        cmd->OMSetRenderTargets(1, &target, FALSE, nullptr);
        cmd->SetPipelineState(mResolvePSO.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
        Transition(cmd, 2 + write, true);
        if (ghostActive)
        {
            CaptureGhostFrame(cmd, 2 + write);
            data.GhostCount = mGhostCount;
            cmd->SetGraphicsRoot32BitConstants(1, 12, &data, 0);
        }
        cmd->OMSetRenderTargets(1, &backBuffer, FALSE, nullptr);
        table = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
        table.ptr += UINT64(write * 3) * mSrvStep;
        cmd->SetGraphicsRootDescriptorTable(0, table);
        cmd->SetPipelineState(mDisplayPSO.Get());
        cmd->DrawInstanced(3, 1, 0, 0);
        mHistory = write;
        Valid = true;
        ++Frame;
    }

private:
    enum : UINT { GhostFrames = 10 };

    void CaptureGhostFrame(ID3D12GraphicsCommandList* cmd, UINT sourceIndex)
    {
      
        const auto now = std::chrono::steady_clock::now();
        if (mGhostCount != 0 &&
            now - mGhostLastCapture < std::chrono::milliseconds(50)) return;
       
        D3D12_RESOURCE_BARRIER before[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(mTextures[sourceIndex].Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(mGhostFrames.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
        };
        cmd->ResourceBarrier(2, before);
        CD3DX12_TEXTURE_COPY_LOCATION source(mTextures[sourceIndex].Get(), 0);
        // One mip per slice: the subresource index is exactly the slice index.
        CD3DX12_TEXTURE_COPY_LOCATION destination(mGhostFrames.Get(), mGhostNext);
        cmd->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        D3D12_RESOURCE_BARRIER after[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(mTextures[sourceIndex].Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(mGhostFrames.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        };
        cmd->ResourceBarrier(2, after);
        mGhostNext = (mGhostNext + 1) % GhostFrames;
        if (mGhostCount < GhostFrames) ++mGhostCount;
        mGhostLastCapture = now;
    }

    static float Halton(UINT index, UINT base)
    {
        float result = 0, fraction = 1;
        while (index) { fraction /= base; result += fraction * (index % base); index /= base; }
        return result;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT index) const
    {
        auto handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += SIZE_T(index) * mRtvStep;
        return handle;
    }
    void Transition(ID3D12GraphicsCommandList* cmd, UINT index, bool toRead)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mTextures[index].Get(),
            toRead ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            toRead ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &barrier);
    }
    ID3D12Device* mDevice = nullptr;
    UINT mWidth = 0, mHeight = 0, mRtvStep = 0, mSrvStep = 0, mHistory = 0;
    // 0: current LDR color in FP16; 1: reprojection/depth; 2/3: history RGB + view depth.
    ComPtr<ID3D12Resource> mTextures[4];
    ComPtr<ID3D12Resource> mGhostFrames;
    UINT mGhostNext = 0, mGhostCount = 0;
    std::chrono::steady_clock::time_point mGhostLastCapture{};
    ComPtr<ID3D12DescriptorHeap> mRtvHeap, mSrvHeap;
    ComPtr<ID3D12RootSignature> mRoot;
    ComPtr<ID3D12PipelineState> mResolvePSO, mDisplayPSO;
};
