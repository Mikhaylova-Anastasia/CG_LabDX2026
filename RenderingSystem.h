#pragma once
#include "Common.h"
#include "InputDevice.h"
#include "ObjLoader.h"
#include "GBuffer.h"
#include <unordered_map>
#include <array>
#include <memory>
#include <vector>

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

static const UINT ShadowCascadeCount = 4;

struct LightConstants
{
    DirectX::XMFLOAT3 EyePosW;
    float pad0 = 0.0f;

    DirectionalLightGPU DirLight;
    PointLightGPU PointLights[2];
    SpotLightGPU SpotLight;

    DirectX::XMFLOAT3 AmbientColor;
    float pad1 = 0.0f;

    DirectX::XMFLOAT4 CameraForward;

    DirectX::XMFLOAT4X4 ShadowViewProj[ShadowCascadeCount];
    DirectX::XMFLOAT4 CascadeSplits;
    DirectX::XMFLOAT2 ShadowMapSize;
    int UseBeckmann = 0;       // 0 = GGX, 1 = Beckmann
    float pad2 = 0.0f;
};
enum class RenderMode
{
    Sponza = 0,
    Tessellation = 1,
    Optimization = 2,
    ShadowTest = 3
};

struct SceneMesh
{
    std::wstring ObjPath;
    std::wstring AssetDir;

    ObjMeshData CpuMesh;
    std::vector<ObjSubmesh> DrawSubmeshes;
    std::vector<UINT> SubmeshBaseSrv;

    Microsoft::WRL::ComPtr<ID3D12Resource> VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> IndexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> VBUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> IBUpload;

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

struct BoundingSphere
{
    DirectX::XMFLOAT3 Center = { 0.0f, 0.0f, 0.0f };
    float Radius = 1.0f;
};

struct SceneObject
{
    DirectX::XMFLOAT4X4 World{};
    BoundingSphere Bounds{};

    DirectX::XMFLOAT3 BasePosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };

    float BaseYaw = 0.0f;
    float AnimationTime = 0.0f;
    float AnimationAccumulatedDt = 0.0f;
    float BobAmplitude = 0.25f;
    float BobSpeed = 1.0f;
    float RotationSpeed = 0.5f;
    float AnimationPhase = 0.0f;

    int UpdateRate = 1;
    int FrameCounter = 0;
};

struct OctreeNode
{
    DirectX::XMFLOAT3 Center = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 Extents = { 1.0f, 1.0f, 1.0f };

    std::vector<int> ObjectIndices;
    std::array<std::unique_ptr<OctreeNode>, 8> Children{};
    bool IsLeaf = true;
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
    void BuildShadowResources();

    void LoadTexture_WIC(const std::wstring& filePath,
        Microsoft::WRL::ComPtr<ID3D12Resource>& tex,
        Microsoft::WRL::ComPtr<ID3D12Resource>& upload);

    void LoadTexture_DDS(const std::wstring& filePath,
        Microsoft::WRL::ComPtr<ID3D12Resource>& tex,
        Microsoft::WRL::ComPtr<ID3D12Resource>& upload);

    void LoadIblResources();

    void CreateSolidTextureRGBA(UINT rgba,
        Microsoft::WRL::ComPtr<ID3D12Resource>& tex,
        Microsoft::WRL::ComPtr<ID3D12Resource>& upload);

    void CreateTextureSrv(UINT srvIndex, ID3D12Resource* tex);
    void CreateTextureCubeSrv(UINT srvIndex, ID3D12Resource* tex);
    void CreateShadowTextureArraySrv(UINT srvIndex);

    void UpdateCamera(const InputDevice& input, float dt);
    void UpdateObjectRotation(const InputDevice& input);
    void UpdateGeometryCB(const SceneMesh& scene);
    void UpdateGeometryCBWithWorld(const SceneMesh& scene, DirectX::CXMMATRIX world);
    void UpdateOptimizationGeometryCB(UINT objectIndex, DirectX::CXMMATRIX world);
    void UpdateLightCB(float totalTime);
    void UpdateShadowCascades();
    void UpdateShadowGeometryCB(UINT cascadeIndex, UINT objectIndex, DirectX::CXMMATRIX world, DirectX::CXMMATRIX lightViewProj);

    void DrawSceneGeometryPass(
        ID3D12GraphicsCommandList* cmdList,
        const SceneMesh& scene,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);

    void DrawOptimizationGeometryPass(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDsv);

    void DrawShadowPass(ID3D12GraphicsCommandList* cmdList);
    void DrawSceneIntoShadowMap(ID3D12GraphicsCommandList* cmdList, const SceneMesh& scene, DirectX::CXMMATRIX lightViewProj, UINT cascadeIndex);
    void DrawOptimizationIntoShadowMap(ID3D12GraphicsCommandList* cmdList, DirectX::CXMMATRIX lightViewProj, UINT cascadeIndex);
    void DrawLightingPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv);

private:
    void ResetCameraForMode(RenderMode mode);

public:

    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjMatrix() const;
    DirectX::XMMATRIX GetViewProjMatrix() const;
    DirectX::XMFLOAT4X4 GetViewProjFloat4x4() const;
    DirectX::XMFLOAT3 GetCameraPosition() const;

private:
    void BuildOptimizationSceneObjects();
    void UpdateOptimizationSceneAnimation(float deltaTime);
    int GetAnimationUpdateRate(float distanceSq) const;
    void RebuildOptimizationObjectWorld(SceneObject& obj);
    void BuildOptimizationOctree();
    std::unique_ptr<OctreeNode> BuildOctreeNode(
        const DirectX::XMFLOAT3& center,
        const DirectX::XMFLOAT3& extents,
        const std::vector<int>& objectIndices,
        int depth);

    std::array<DirectX::XMFLOAT4, 6> ExtractFrustumPlanes(DirectX::CXMMATRIX viewProj) const;
    bool SphereInsideFrustum(const BoundingSphere& sphere, const std::array<DirectX::XMFLOAT4, 6>& planes) const;
    bool AabbInsideFrustum(
        const DirectX::XMFLOAT3& center,
        const DirectX::XMFLOAT3& extents,
        const std::array<DirectX::XMFLOAT4, 6>& planes) const;

    void CollectVisibleObjectsLinear(const std::array<DirectX::XMFLOAT4, 6>& planes, std::vector<int>& outVisible) const;
    void CollectVisibleObjectsOctree(
        const OctreeNode* node,
        const std::array<DirectX::XMFLOAT4, 6>& planes,
        std::vector<int>& outVisible) const;

private:
    ID3D12Device* mDevice = nullptr;
    ID3D12GraphicsCommandList* mInitCmdList = nullptr;

    UINT mRtvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;
    UINT mWidth = 0;
    UINT mHeight = 0;

    GBuffer mGBuffer;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mGBufferRtvHeap;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGeometryRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mLightingRootSig;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mShadowRootSig;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mTessPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mLightingPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mShadowPSO;

    SceneMesh mSponzaScene;
    SceneMesh mTessScene;
    SceneMesh mOptimizationScene;
    SceneMesh mShadowTestScene;

    RenderMode mMode = RenderMode::Sponza;

    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mTextures;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mTextureUploads;

    UINT mModelTextureCount = 0;
    UINT mGBufferSrvStartIndex = 0;
    UINT mShadowSrvIndex = 0;
    UINT mShadowMaskSrvIndex = 0;
    UINT mIrradianceSrvIndex = 0;
    UINT mPrefilterSrvIndex = 0;
    UINT mBrdfLutSrvIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMaskTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMaskUpload;

    Microsoft::WRL::ComPtr<ID3D12Resource> mIrradianceMap;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIrradianceUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrefilterMap;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrefilterUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mBrdfLut;
    Microsoft::WRL::ComPtr<ID3D12Resource> mBrdfLutUpload;

    Microsoft::WRL::ComPtr<ID3D12Resource> mGeometryCB;
    Microsoft::WRL::ComPtr<ID3D12Resource> mShadowGeometryCB;
    Microsoft::WRL::ComPtr<ID3D12Resource> mOptimizationGeometryCB;
    UINT mGeometryCBByteSize = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> mLightingCB;

    Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;
    UINT mShadowMapSize = 2048;
    D3D12_VIEWPORT mShadowViewport{};
    D3D12_RECT mShadowScissor{};
    DirectX::XMFLOAT4X4 mShadowViewProj[ShadowCascadeCount]{};
    float mCascadeSplits[ShadowCascadeCount]{};

    GeometryConstants mGeometryData{};
    LightConstants mLightingData{};

    DirectX::XMFLOAT4X4 mProj{};
    DirectX::XMFLOAT3 mCameraPos = { 0.0f, 1.5f, -2.0f };
    float mYaw = 0.0f;
    float mPitch = 0.0f;

    float mObjectYaw = 0.0f;
    float mObjectPitch = 0.0f;

    std::vector<SceneObject> mOptObjects;
    std::unique_ptr<OctreeNode> mOctreeRoot;
    DirectX::XMFLOAT3 mOptSceneCenter = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 mOptSceneExtents = { 60.0f, 10.0f, 60.0f };

    bool mEnableFrustumCulling = true;
    bool mEnableOctree = true;
    bool mUseBeckmann = false;

    UINT mLastVisibleCount = 0;
    UINT mLastTotalCount = 0;
    UINT mLastAnimatedCount = 0;
    float mStatsPrintTimer = 0.0f;
};