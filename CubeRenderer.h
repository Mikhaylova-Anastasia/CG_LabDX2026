// CubeRenderer.h
#pragma once
#include "Common.h"
#include "InputDevice.h"
#include "ObjLoader.h"
#include <unordered_map>

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 WorldViewProj;
    DirectX::XMFLOAT4X4 World;

    DirectX::XMFLOAT3   EyePosW;
    float      pad0;

    DirectX::XMFLOAT4   LightDir;
    DirectX::XMFLOAT4   DiffuseColor;

    DirectX::XMFLOAT4   SpecularColor;
    float      Shininess;
    float      pad1;
    DirectX::XMFLOAT2   pad2;

    DirectX::XMFLOAT2   Tiling;
    DirectX::XMFLOAT2   UVOffset;
};

class CubeRenderer
{
public:
    CubeRenderer(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        UINT cbvSrvUavDescriptorSize);

    void BuildResources();
    void BuildPSO();

    void Update(float totalTime, float deltaTime, const InputDevice& input);
    void Draw(ID3D12GraphicsCommandList* cmdList);

    ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
    ID3D12PipelineState* GetPSO() const { return mPSO.Get(); }

private:
    void BuildMeshGeometry();   
    void BuildConstantBuffer();
    void BuildRootSignature();

    void BuildSrvDescriptorHeap(UINT numDescriptors);
    void LoadTexture_WIC(const std::wstring& filePath,
        Microsoft::WRL::ComPtr<ID3D12Resource>& tex,
        Microsoft::WRL::ComPtr<ID3D12Resource>& upload);
    void CreateTextureSrv(UINT srvIndex, ID3D12Resource* tex);

    
    std::unordered_map<std::string, std::string> ParseMtlDiffuseMaps(const std::wstring& mtlPath);

    void UpdateCamera(const InputDevice& input, float dt);
    void UpdateCubeRotation(const InputDevice& input, float dt);

private:
    enum class DemoMode { Bread = 0, Animated = 1 };
    DemoMode mMode = DemoMode::Bread;

private:
    ID3D12Device* mDevice;
    ID3D12GraphicsCommandList* mCmdList;

    UINT mCbvSrvUavDescriptorSize;

    Microsoft::WRL::ComPtr<ID3D12Resource> mVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndexBuffer;

    Microsoft::WRL::ComPtr<ID3D12Resource> mVBUpload;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIBUpload;

    Microsoft::WRL::ComPtr<ID3D12Resource> mConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mConstantUploadBuffer;

    D3D12_VERTEX_BUFFER_VIEW mVBV = {};
    D3D12_INDEX_BUFFER_VIEW  mIBV = {};

    UINT mIndexCount = 0;

    ObjectConstants mConstants;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;

    DirectX::XMFLOAT4X4 mProj;

    DirectX::XMFLOAT3 mCameraPos = { 0.0f, 2.0f, -5.0f };
    float mYaw = 0.0f;
    float mPitch = 0.0f;

    float mCubeYaw = 0.0f;
    float mCubePitch = 0.0f;

    
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mTextures;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mTextureUploads;

   
    std::vector<ObjSubmesh> mDrawSubmeshes;
    std::vector<UINT>       mSubmeshSrvIndex;

    UINT mAnimatedSrvIndex = 0;

    
    bool mPrevKey1 = false;
    bool mPrevKey2 = false;

    float mTotalTime = 0.0f;
};