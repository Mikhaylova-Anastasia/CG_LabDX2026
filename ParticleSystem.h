#pragma once
#include "Common.h"
#include <random>
#include <vector>

struct Particle
{
    DirectX::XMFLOAT3 Position;
    float Age;

    DirectX::XMFLOAT3 Velocity;
    float Lifetime;

    DirectX::XMFLOAT3 Color;
    float Size;

    DirectX::XMFLOAT3 StartColor;
    float Rotation;

    DirectX::XMFLOAT3 EndColor;
    uint32_t Active;
};

struct ParticleConstants
{
    DirectX::XMFLOAT4 EmitterPositionAndSpawnRadius; 
    DirectX::XMFLOAT4 EmitterVelocityAndDeltaTime;   
    DirectX::XMFLOAT4 SimParams;                     
    DirectX::XMUINT4 Counts;                       
};

class ParticleSystem
{
public:
    ParticleSystem();
    ~ParticleSystem();

    void Initialize(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT maxParticles);

    void Update(float deltaTime, const DirectX::XMFLOAT3& emitterPos, const DirectX::XMFLOAT3& emitterVel);

    void Render(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv,
        const DirectX::XMFLOAT4X4& viewProj,
        const DirectX::XMFLOAT3& cameraPos);

    void OnResize(UINT width, UINT height);
    void BuildResources();

private:
    struct QuadVertex
    {
        DirectX::XMFLOAT2 Corner;
        DirectX::XMFLOAT2 Tex;
    };

private:
    void BuildBuffers();
    void BuildQuadGeometry();
    void BuildRootSignatures();
    void BuildPSOs();
    void CompileShaders();
    void CreateParticleTexture();

    void ResetCounter(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* counterResource);

    void FinalizePreviousFrame();

    void RunUpdateComputeShader(ID3D12GraphicsCommandList* cmdList);
    void RunEmitComputeShader(ID3D12GraphicsCommandList* cmdList);
    void RenderParticles(
        ID3D12GraphicsCommandList* cmdList,
        const DirectX::XMFLOAT4X4& viewProj,
        const DirectX::XMFLOAT3& cameraPos);

private:
    ID3D12Device* mDevice = nullptr;
    ID3D12GraphicsCommandList* mInitCmdList = nullptr;

    UINT mMaxParticles = 4096;
    UINT mWidth = 1280;
    UINT mHeight = 720;

    UINT mSrvDescriptorSize = 0;
    UINT mRtvDescriptorSize = 0;

  
    Microsoft::WRL::ComPtr<ID3D12Resource> mParticleBuffers[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> mParticleCounterBuffers[2];

    Microsoft::WRL::ComPtr<ID3D12Resource> mCounterReadback;
    Microsoft::WRL::ComPtr<ID3D12Resource> mCounterResetUpload;

    UINT mCurrentBufferIndex = 0;
    UINT mAliveCount = 0;
    bool mPendingSwap = false;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mComputeRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGraphicsRootSig;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> mUpdatePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mEmitPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mRenderPSO;

    Microsoft::WRL::ComPtr<ID3DBlob> mUpdateCS;
    Microsoft::WRL::ComPtr<ID3DBlob> mEmitCS;
    Microsoft::WRL::ComPtr<ID3DBlob> mParticleVS;
    Microsoft::WRL::ComPtr<ID3DBlob> mParticleGS;
    Microsoft::WRL::ComPtr<ID3DBlob> mParticlePS;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;

    Microsoft::WRL::ComPtr<ID3D12Resource> mParticleTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> mParticleTextureUpload;

    Microsoft::WRL::ComPtr<ID3D12Resource> mQuadVB;
    Microsoft::WRL::ComPtr<ID3D12Resource> mQuadIB;
    D3D12_VERTEX_BUFFER_VIEW mQuadVBView = {};
    D3D12_INDEX_BUFFER_VIEW mQuadIBView = {};

    ParticleConstants mConstants{};

    float mSpawnRate = 500.0f;
    float mSpawnAccumulator = 0.0f;

    std::mt19937 mRng;
    std::uniform_real_distribution<float> mDist;
};