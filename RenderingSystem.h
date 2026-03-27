#pragma once
#include "Common.h"
#include "InputDevice.h"
#include "ObjLoader.h"
#include "GBuffer.h"
#include <unordered_map>

struct GeometryConstants
{
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT4X4 ViewProj;
    DirectX::XMFLOAT2   Tiling;
    DirectX::XMFLOAT2   UVOffset;
    DirectX::XMFLOAT3   EyePosW;
    float               TessMin = 1.0f;
    float               TessMax = 8.0f;
    float               TessMaxDistance = 8.0f;
    float               DisplacementScale = 0.05f;
    float               NormalMapFlipY = 0.0f;
    DirectX::XMFLOAT3   pad0{};
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
    PointLightGPU PointLights[2];
    SpotLightGPU SpotLight;

    DirectX::XMFLOAT3 AmbientColor;
    float pad1 = 0.0f;
};

enum class RenderMode
{
    Sponza = 0,
    Tessellation = 1
};

struct SceneMesh
{
    std::wstring ObjPath;
    std::wstring AssetDir;

    ObjMeshData CpuMesh;
    std::vector<ObjSubmesh> DrawSubmeshes;
    std::vector<UINT> SubmeshBaseSrv;

    ComPtr<ID3D12Resource> VertexBuffer;
    ComPtr<ID3D12Resource> IndexBuffer;
    ComPtr<ID3D12Resource> VBUpload;
    ComPtr<ID3D12Resource> IBUpload;

    D3D12_VERTEX_BUFFER_VIEW VBV = {};
    D3D12_INDEX_BUFFER_VIEW  IBV = {};

    DirectX::XMFLOAT4X4 World{};

    bool UseTessellation = false;
    float TessMin = 1.0f;
    float TessMax = 8.0f;
    float TessMaxDistance = 8.0f;
    float DisplacementScale = 0.05f;
    float NormalMapFlipY = 0.0f;
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

private:
    void BuildSceneGeometry();
    void BuildSceneTextures();
    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignatures();
    void BuildPSOs();

    void LoadTexture_WIC(const std::wstring& filePath,
        ComPtr<ID3D12Resource>& tex,
        ComPtr<ID3D12Resource>& upload);

    void CreateSolidTextureRGBA(UINT rgba,
        ComPtr<ID3D12Resource>& tex,
        ComPtr<ID3D12Resource>& upload);

    void CreateTextureSrv(UINT srvIndex, ID3D12Resource* tex);

    void UpdateCamera(const InputDevice& input, float dt);
    void UpdateObjectRotation(const InputDevice& input);
    void UpdateGeometryCB(const SceneMesh& scene);
    void UpdateLightCB(float totalTime);

    void DrawSceneGeometryPass(
        ID3D12GraphicsCommandList* cmdList,
        const SceneMesh& scene,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);

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
    ComPtr<ID3D12PipelineState> mTessPSO;
    ComPtr<ID3D12PipelineState> mLightingPSO;

    SceneMesh mSponzaScene;
    SceneMesh mTessScene;
    RenderMode mMode = RenderMode::Sponza;

    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<ComPtr<ID3D12Resource>> mTextureUploads;

    UINT mModelTextureCount = 0;
    UINT mGBufferSrvStartIndex = 0;

    ComPtr<ID3D12Resource> mGeometryCB;
    ComPtr<ID3D12Resource> mLightingCB;

    GeometryConstants mGeometryData{};
    LightConstants mLightingData{};

    DirectX::XMFLOAT4X4 mProj{};
    DirectX::XMFLOAT3 mCameraPos = { 0.0f, 1.5f, -2.0f };
    float mYaw = 0.0f;
    float mPitch = 0.0f;

    float mObjectYaw = 0.0f;
    float mObjectPitch = 0.0f;
};