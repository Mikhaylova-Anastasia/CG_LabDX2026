#pragma once
#include "Common.h"
#include "InputDevice.h"
#include "ObjLoader.h"
#include "GBuffer.h"
#include <unordered_map>

struct GeometryConstants
{
    DirectX::XMFLOAT4X4 WorldViewProj;
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT2   Tiling;
    DirectX::XMFLOAT2   UVOffset;
};

struct DirectionalLightGPU
{
    DirectX::XMFLOAT3 Direction;
    float Intensity = 1.0f;

    DirectX::XMFLOAT3 Color;
    float pad0 = 0.0f;
};

struct PointLightGPU
{
    DirectX::XMFLOAT3 Position;
    float Range = 1.0f;

    DirectX::XMFLOAT3 Color;
    float Intensity = 1.0f;
};

struct SpotLightGPU
{
    DirectX::XMFLOAT3 Position;
    float Range = 1.0f;

    DirectX::XMFLOAT3 Direction;
    float SpotPower = 16.0f;

    DirectX::XMFLOAT3 Color;
    float Intensity = 1.0f;
};

struct LightConstants
{
    DirectX::XMFLOAT3 EyePosW;
    float pad0 = 0.0f;

    DirectionalLightGPU DirLight;
    PointLightGPU PointLights[8];
    SpotLightGPU SpotLight;

    DirectX::XMFLOAT3 AmbientColor;
    float pad1 = 0.0f;
};

class RenderingSystem
{
public:
    RenderingSystem(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT rtvDescriptorSize,
        UINT cbvSrvUavDescriptorSize,
        UINT width,
        UINT height);

    void BuildResources();
    void OnResize(UINT width, UINT height);

    void Update(float totalTime, float deltaTime, const InputDevice& input);
    void Draw(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);


    static const int OriginalPointLightCount = 2;
    static const int MaxShotLights = 6;

    struct ShotLight
    {
        bool Active = false;
        bool Flying = false;

        DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Target = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 Velocity = { 0.0f, 0.0f, 0.0f };

        DirectX::XMFLOAT3 Color = { 1.0f, 0.75f, 0.2f };
        float Range = 12.0f;
        float Intensity = 2.5f;
    };

private:
    void BuildMeshGeometry();
    void BuildTextureResources();
    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignatures();
    void BuildPSOs();

    void LoadTexture_WIC(const std::wstring& filePath,
        ComPtr<ID3D12Resource>& tex,
        ComPtr<ID3D12Resource>& upload);

    void CreateTextureSrv(UINT srvIndex, ID3D12Resource* tex);

    std::unordered_map<std::string, std::string> ParseMtlDiffuseMaps(const std::wstring& mtlPath);

    void UpdateCamera(const InputDevice& input, float dt);
    void UpdateObjectRotation(const InputDevice& input);
    void UpdateGeometryCB(float totalTime);
    void UpdateLightCB(float totalTime);

    DirectX::XMMATRIX GetSceneWorldMatrix() const;

    bool RayIntersectsTriangle(
        DirectX::FXMVECTOR rayOrigin,
        DirectX::FXMVECTOR rayDir,
        DirectX::FXMVECTOR v0,
        DirectX::FXMVECTOR v1,
        DirectX::FXMVECTOR v2,
        float& outT) const;

    bool RaycastScene(
        const DirectX::XMFLOAT3& rayOrigin,
        const DirectX::XMFLOAT3& rayDir,
        DirectX::XMFLOAT3& outHitPos) const;

    void TryShootLight(const InputDevice& input);
    void UpdateShotLights(float deltaTime);

    void DrawGeometryPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);
    void DrawLightingPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv);

private:
    ID3D12Device* mDevice = nullptr;
    ID3D12GraphicsCommandList* mInitCmdList = nullptr;

    UINT mRtvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;
    UINT mWidth = 0;
    UINT mHeight = 0;

    GBuffer mGBuffer;

    ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    ComPtr<ID3D12DescriptorHeap> mGBufferRtvHeap;

    ComPtr<ID3D12RootSignature> mGeometryRootSig;
    ComPtr<ID3D12RootSignature> mLightingRootSig;

    ComPtr<ID3D12PipelineState> mGeometryPSO;
    ComPtr<ID3D12PipelineState> mLightingPSO;

    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Resource> mIndexBuffer;
    ComPtr<ID3D12Resource> mVBUpload;
    ComPtr<ID3D12Resource> mIBUpload;

    D3D12_VERTEX_BUFFER_VIEW mVBV = {};
    D3D12_INDEX_BUFFER_VIEW  mIBV = {};
    UINT mIndexCount = 0;

    std::vector<ObjSubmesh> mDrawSubmeshes;
    std::vector<UINT> mSubmeshSrvIndex;

    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploads;

    UINT mModelTextureCount = 0;
    UINT mGBufferSrvStartIndex = 0;


    std::vector<VertexPosNormalTex> mCpuVertices;
    std::vector<uint32_t> mCpuIndices;

    ComPtr<ID3D12Resource> mGeometryCB;
    ComPtr<ID3D12Resource> mLightingCB;

    GeometryConstants mGeometryData{};
    LightConstants mLightingData{};

    DirectX::XMFLOAT4X4 mProj{};
    DirectX::XMFLOAT3 mCameraPos = { 0.0f, 1.5f, -2.0f };
    float mYaw = 0.0f;
    float mPitch = 0.0f;


    ShotLight mShotLights[MaxShotLights]{};
    int mNextShotLight = 0;

    float mObjectYaw = 0.0f;
    float mObjectPitch = 0.0f;
};