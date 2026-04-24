#include "ParticleSystem.h"
#include <array>
#include <vector>
#include <cstring>
#include <algorithm>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    struct ParticleRenderConstants
    {
        XMFLOAT4X4 ViewProj;
        XMFLOAT4 CameraPosAndSize;
    };
}

ParticleSystem::ParticleSystem()
    : mRng(std::random_device{}()), mDist(0.0f, 1.0f)
{
    mConstants.EmitterPositionAndSpawnRadius = XMFLOAT4(0.0f, 2.0f, 0.0f, 0.35f);
    mConstants.EmitterVelocityAndDeltaTime = XMFLOAT4(0.0f, 4.5f, 0.0f, 0.0f);
    mConstants.SimParams = XMFLOAT4(-9.81f, 0.0f, 1.8f, 0.0f);
    mConstants.Counts = XMUINT4(mMaxParticles, 0, 0, 0);
}

ParticleSystem::~ParticleSystem()
{
}

void ParticleSystem::Initialize(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    UINT maxParticles)
{
    mDevice = device;
    mInitCmdList = cmdList;
    mMaxParticles = maxParticles;
    mConstants.Counts.x = maxParticles;

    mSrvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    BuildBuffers();
    BuildRootSignatures();
    CompileShaders();
    BuildPSOs();
    CreateParticleTexture();
}

void ParticleSystem::BuildBuffers()
{
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);

    const UINT64 particleBufferSize = sizeof(Particle) * mMaxParticles;
    const UINT64 counterBufferSize = sizeof(UINT);

    for (int i = 0; i < 2; ++i)
    {
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
            particleBufferSize,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&mParticleBuffers[i])));

        auto counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
            counterBufferSize,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&mParticleCounterBuffers[i])));
    }

    auto readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(counterBufferSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &readbackHeap,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mCounterReadback)));

    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(counterBufferSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mCounterResetUpload)));

    {
        void* mapped = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(mCounterResetUpload->Map(0, &readRange, &mapped));
        std::memset(mapped, 0, sizeof(UINT));
        mCounterResetUpload->Unmap(0, nullptr);
    }

    ResetCounter(mInitCmdList, mParticleCounterBuffers[0].Get());
    ResetCounter(mInitCmdList, mParticleCounterBuffers[1].Get());

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 8;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSrvHeap)));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.Buffer.NumElements = mMaxParticles;
    uavDesc.Buffer.StructureByteStride = sizeof(Particle);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Buffer.NumElements = mMaxParticles;
    srvDesc.Buffer.StructureByteStride = sizeof(Particle);
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());

    
    mDevice->CreateUnorderedAccessView(
        mParticleBuffers[0].Get(),
        mParticleCounterBuffers[0].Get(),
        &uavDesc,
        cpu);

    
    cpu.Offset(1, mSrvDescriptorSize);
    mDevice->CreateUnorderedAccessView(
        mParticleBuffers[1].Get(),
        mParticleCounterBuffers[1].Get(),
        &uavDesc,
        cpu);

    
    cpu.Offset(1, mSrvDescriptorSize);
    mDevice->CreateShaderResourceView(mParticleBuffers[0].Get(), &srvDesc, cpu);

   
    cpu.Offset(1, mSrvDescriptorSize);

    cpu.Offset(1, mSrvDescriptorSize);
    mDevice->CreateUnorderedAccessView(
        mParticleBuffers[1].Get(),
        mParticleCounterBuffers[1].Get(),
        &uavDesc,
        cpu);

    
    cpu.Offset(1, mSrvDescriptorSize);
    mDevice->CreateUnorderedAccessView(
        mParticleBuffers[0].Get(),
        mParticleCounterBuffers[0].Get(),
        &uavDesc,
        cpu);

   
    cpu.Offset(1, mSrvDescriptorSize);
    mDevice->CreateShaderResourceView(mParticleBuffers[1].Get(), &srvDesc, cpu);

    
}

void ParticleSystem::BuildQuadGeometry()
{
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

    QuadVertex vertices[4] =
    {
        { XMFLOAT2(-1.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT2(-1.0f,  1.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT2(1.0f,  1.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT2(1.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) }
    };

    uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    {
        const UINT vbSize = sizeof(vertices);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mQuadVB)));

        void* mapped = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(mQuadVB->Map(0, &readRange, &mapped));
        std::memcpy(mapped, vertices, vbSize);
        mQuadVB->Unmap(0, nullptr);

        mQuadVBView.BufferLocation = mQuadVB->GetGPUVirtualAddress();
        mQuadVBView.StrideInBytes = sizeof(QuadVertex);
        mQuadVBView.SizeInBytes = vbSize;
    }

    {
        const UINT ibSize = sizeof(indices);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mQuadIB)));

        void* mapped = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(mQuadIB->Map(0, &readRange, &mapped));
        std::memcpy(mapped, indices, ibSize);
        mQuadIB->Unmap(0, nullptr);

        mQuadIBView.BufferLocation = mQuadIB->GetGPUVirtualAddress();
        mQuadIBView.Format = DXGI_FORMAT_R16_UINT;
        mQuadIBView.SizeInBytes = ibSize;
    }
}

void ParticleSystem::BuildRootSignatures()
{
    {
        CD3DX12_DESCRIPTOR_RANGE uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);

        CD3DX12_ROOT_PARAMETER rootParams[2];
        rootParams[0].InitAsConstants(sizeof(ParticleConstants) / 4, 0);
        rootParams[1].InitAsDescriptorTable(1, &uavRange);

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParams, 0, nullptr);

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,
            &error);

        if (FAILED(hr))
        {
            if (error) OutputDebugStringA((const char*)error->GetBufferPointer());
            ThrowIfFailed(hr);
        }

        ThrowIfFailed(mDevice->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mComputeRootSig)));
    }

    {
        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);

        CD3DX12_ROOT_PARAMETER rootParams[2];
        rootParams[0].InitAsConstants(sizeof(ParticleRenderConstants) / 4, 0, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
            2,
            rootParams,
            1,
            &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeRootSignature(
            &rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,
            &error);

        if (FAILED(hr))
        {
            if (error) OutputDebugStringA((const char*)error->GetBufferPointer());
            ThrowIfFailed(hr);
        }

        ThrowIfFailed(mDevice->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mGraphicsRootSig)));
    }
}

void ParticleSystem::CompileShaders()
{
    auto compile = [&](const wchar_t* path, const char* entry, const char* target, ComPtr<ID3DBlob>& out)
        {
            ComPtr<ID3DBlob> errors;

            HRESULT hr = D3DCompileFromFile(
                path,
                nullptr,
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entry,
                target,
                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                0,
                &out,
                &errors);

            if (FAILED(hr))
            {
                std::wstring msg = L"Shader compile failed:\n";
                msg += path;
                msg += L"\n\nEntry: ";
                msg += std::wstring(entry, entry + strlen(entry));
                msg += L"\nTarget: ";
                msg += std::wstring(target, target + strlen(target));

                MessageBoxW(nullptr, msg.c_str(), L"Shader file/path error", MB_OK | MB_ICONERROR);

                if (errors)
                {
                    OutputDebugStringA((const char*)errors->GetBufferPointer());
                    MessageBoxA(nullptr, (const char*)errors->GetBufferPointer(), "Particle shader compile error", MB_OK | MB_ICONERROR);
                }

                ThrowIfFailed(hr);
            }
        };

    compile(L"Shaders\\ParticleUpdate.hlsl", "CSMain", "cs_5_0", mUpdateCS);
    compile(L"Shaders\\ParticleEmit.hlsl", "CSMain", "cs_5_0", mEmitCS);
    compile(L"Shaders\\ParticleVS.hlsl", "VSMain", "vs_5_0", mParticleVS);
    compile(L"C:\\Users\\0\\source\\repos\\DX12\\x64\\Debug\\Shaders\\ParticleGS.hlsl", "GSMain", "gs_5_0", mParticleGS);
    compile(L"Shaders\\ParticlePS.hlsl", "PSMain", "ps_5_0", mParticlePS);
}

void ParticleSystem::BuildPSOs()
{
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = mComputeRootSig.Get();
        desc.CS = { mUpdateCS->GetBufferPointer(), mUpdateCS->GetBufferSize() };

        ThrowIfFailed(mDevice->CreateComputePipelineState(
            &desc,
            IID_PPV_ARGS(&mUpdatePSO)));
    }

    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = mComputeRootSig.Get();
        desc.CS = { mEmitCS->GetBufferPointer(), mEmitCS->GetBufferSize() };

        ThrowIfFailed(mDevice->CreateComputePipelineState(
            &desc,
            IID_PPV_ARGS(&mEmitPSO)));
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.InputLayout = { nullptr, 0 };
        desc.pRootSignature = mGraphicsRootSig.Get();

        desc.VS = { mParticleVS->GetBufferPointer(), mParticleVS->GetBufferSize() };
        desc.GS = { mParticleGS->GetBufferPointer(), mParticleGS->GetBufferSize() };
        desc.PS = { mParticlePS->GetBufferPointer(), mParticlePS->GetBufferSize() };

        CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
        rast.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState = rast;

        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        D3D12_DEPTH_STENCIL_DESC ds = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState = ds;

        desc.SampleMask = UINT_MAX;

        // ÂÀÆÍÎ
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.SampleDesc.Count = 1;

        ThrowIfFailed(mDevice->CreateGraphicsPipelineState(
            &desc,
            IID_PPV_ARGS(&mRenderPSO)));
    }
}
void ParticleSystem::CreateParticleTexture()
{
    const UINT size = 32;
    std::vector<uint8_t> pixels(size * size * 4);

    for (UINT y = 0; y < size; ++y)
    {
        for (UINT x = 0; x < size; ++x)
        {
            const float dx = (x - size / 2.0f) / (size / 2.0f);
            const float dy = (y - size / 2.0f) / (size / 2.0f);
            const float dist = sqrtf(dx * dx + dy * dy);
            const float alpha = (dist < 1.0f) ? 255.0f : 0.0f;

            const UINT idx = (y * size + x) * 4;
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<uint8_t>(alpha);
        }
    }

    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1);
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mParticleTexture)));

    const UINT64 uploadSize = GetRequiredIntermediateSize(mParticleTexture.Get(), 0, 1);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mParticleTextureUpload)));

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = pixels.data();
    subData.RowPitch = size * 4;
    subData.SlicePitch = size * size * 4;
    UpdateSubresources(mInitCmdList, mParticleTexture.Get(), mParticleTextureUpload.Get(), 0, 0, 1, &subData);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleTexture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mInitCmdList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());

    cpu.Offset(3, mSrvDescriptorSize);
    mDevice->CreateShaderResourceView(mParticleTexture.Get(), &srvDesc, cpu);

    cpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    cpu.Offset(7, mSrvDescriptorSize);
    mDevice->CreateShaderResourceView(mParticleTexture.Get(), &srvDesc, cpu);
}

void ParticleSystem::ResetCounter(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* counterResource)
{
    auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
        counterResource,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &toCopyDest);

    cmdList->CopyBufferRegion(counterResource, 0, mCounterResetUpload.Get(), 0, sizeof(UINT));

    auto backToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
        counterResource,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COMMON);
    cmdList->ResourceBarrier(1, &backToCommon);
}

void ParticleSystem::FinalizePreviousFrame()
{
    if (!mPendingSwap)
        return;

    UINT* mapped = nullptr;
    CD3DX12_RANGE readRange(0, sizeof(UINT));
    ThrowIfFailed(mCounterReadback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
    mAliveCount = *mapped;
    mCounterReadback->Unmap(0, nullptr);

    mCurrentBufferIndex = 1 - mCurrentBufferIndex;
    mPendingSwap = false;
}

void ParticleSystem::RunUpdateComputeShader(ID3D12GraphicsCommandList* cmdList)
{
    if (mAliveCount == 0)
        return;

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(mComputeRootSig.Get());
    cmdList->SetPipelineState(mUpdatePSO.Get());
    cmdList->SetComputeRoot32BitConstants(0, sizeof(ParticleConstants) / 4, &mConstants, 0);

    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    if (mCurrentBufferIndex == 0)
        handle.Offset(0, mSrvDescriptorSize);
    else
        handle.Offset(4, mSrvDescriptorSize); 

    cmdList->SetComputeRootDescriptorTable(1, handle);
    cmdList->Dispatch((mAliveCount + 63) / 64, 1, 1);
}

void ParticleSystem::RunEmitComputeShader(ID3D12GraphicsCommandList* cmdList)
{
    if (mConstants.Counts.y == 0)
        return;

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(mComputeRootSig.Get());
    cmdList->SetPipelineState(mEmitPSO.Get());
    cmdList->SetComputeRoot32BitConstants(0, sizeof(ParticleConstants) / 4, &mConstants, 0);

    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    if (mCurrentBufferIndex == 0)
        handle.Offset(0, mSrvDescriptorSize); 
    else
        handle.Offset(4, mSrvDescriptorSize); 

    cmdList->SetComputeRootDescriptorTable(1, handle);
    cmdList->Dispatch((mConstants.Counts.y + 63) / 64, 1, 1);
}

void ParticleSystem::RenderParticles(
    ID3D12GraphicsCommandList* cmdList,
    const XMFLOAT4X4& viewProj,
    const XMFLOAT3& cameraPos)
{
    UINT drawCount = mAliveCount + mConstants.Counts.y;
    if (drawCount == 0)
        return;

    if (drawCount > mMaxParticles)
        drawCount = mMaxParticles;

    ParticleRenderConstants constants = {};
    XMMATRIX vp = XMLoadFloat4x4(&viewProj);
    XMStoreFloat4x4(&constants.ViewProj, XMMatrixTranspose(vp));
    constants.CameraPosAndSize = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetGraphicsRootSignature(mGraphicsRootSig.Get());
    cmdList->SetPipelineState(mRenderPSO.Get());
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(ParticleRenderConstants) / 4, &constants, 0);

    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    if (mCurrentBufferIndex == 0)
        handle.Offset(6, mSrvDescriptorSize); // SRV B + TEX
    else
        handle.Offset(2, mSrvDescriptorSize); // SRV A + TEX

    cmdList->SetGraphicsRootDescriptorTable(1, handle);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->DrawInstanced(drawCount, 1, 0, 0);
}

void ParticleSystem::Update(float deltaTime, const XMFLOAT3& emitterPos, const XMFLOAT3& emitterVel)
{
    FinalizePreviousFrame();

    mConstants.EmitterPositionAndSpawnRadius = XMFLOAT4(
        emitterPos.x,
        emitterPos.y,
        emitterPos.z,
        mConstants.EmitterPositionAndSpawnRadius.w);

    mConstants.EmitterVelocityAndDeltaTime = XMFLOAT4(
        emitterVel.x,
        emitterVel.y,
        emitterVel.z,
        deltaTime);

    mConstants.SimParams.y += deltaTime;
    mConstants.Counts.z = mAliveCount;

    mSpawnAccumulator += mSpawnRate * deltaTime;

    UINT wantedSpawn = static_cast<UINT>(mSpawnAccumulator);
    UINT available = (mAliveCount < mMaxParticles) ? (mMaxParticles - mAliveCount) : 0;
    UINT numToSpawn = (wantedSpawn < available) ? wantedSpawn : available;

    mSpawnAccumulator -= static_cast<float>(numToSpawn);
    mConstants.Counts.y = numToSpawn;
}

void ParticleSystem::Render(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv,
    const XMFLOAT4X4& viewProj,
    const XMFLOAT3& cameraPos)
{
    const UINT nextIndex = 1 - mCurrentBufferIndex;

    ResetCounter(cmdList, mParticleCounterBuffers[nextIndex].Get());

    std::array<CD3DX12_RESOURCE_BARRIER, 4> toUav =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(mParticleBuffers[mCurrentBufferIndex].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(mParticleCounterBuffers[mCurrentBufferIndex].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(mParticleBuffers[nextIndex].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(mParticleCounterBuffers[nextIndex].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    cmdList->ResourceBarrier((UINT)toUav.size(), toUav.data());

    RunUpdateComputeShader(cmdList);

    auto uavBarrier0 = CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[mCurrentBufferIndex].Get());
    auto uavBarrier1 = CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[nextIndex].Get());
    auto uavBarrier2 = CD3DX12_RESOURCE_BARRIER::UAV(mParticleCounterBuffers[nextIndex].Get());
    cmdList->ResourceBarrier(1, &uavBarrier0);
    cmdList->ResourceBarrier(1, &uavBarrier1);
    cmdList->ResourceBarrier(1, &uavBarrier2);

    RunEmitComputeShader(cmdList);

    auto uavBarrier3 = CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[nextIndex].Get());
    auto uavBarrier4 = CD3DX12_RESOURCE_BARRIER::UAV(mParticleCounterBuffers[nextIndex].Get());
    cmdList->ResourceBarrier(1, &uavBarrier3);
    cmdList->ResourceBarrier(1, &uavBarrier4);

    auto nextCounterToCopySrc = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleCounterBuffers[nextIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->ResourceBarrier(1, &nextCounterToCopySrc);

    cmdList->CopyBufferRegion(
        mCounterReadback.Get(),
        0,
        mParticleCounterBuffers[nextIndex].Get(),
        0,
        sizeof(UINT));

    auto nextCounterBackToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleCounterBuffers[nextIndex].Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    cmdList->ResourceBarrier(1, &nextCounterBackToCommon);

    auto nextToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleBuffers[nextIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &nextToSrv);

    cmdList->OMSetRenderTargets(1, &backBufferRtv, TRUE, &depthDsv);
    RenderParticles(cmdList, viewProj, cameraPos);

    auto nextBackToCommon = CD3DX12_RESOURCE_BARRIER::Transition(
        mParticleBuffers[nextIndex].Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    cmdList->ResourceBarrier(1, &nextBackToCommon);

    std::array<CD3DX12_RESOURCE_BARRIER, 2> backToCommon =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(mParticleBuffers[mCurrentBufferIndex].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(mParticleCounterBuffers[mCurrentBufferIndex].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON)
    };
    cmdList->ResourceBarrier((UINT)backToCommon.size(), backToCommon.data());

    mPendingSwap = true;
}

void ParticleSystem::OnResize(UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;
}

void ParticleSystem::BuildResources()
{
}