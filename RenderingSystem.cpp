#include "RenderingSystem.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <random>
#include <cfloat>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static UINT AlignCB_RS(UINT size)
{
    return (size + 255) & ~255;
}

static std::wstring GetExeDir_RS()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    return std::wstring(exePath);
}

static std::wstring GetDirPart_RS(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos)
        return L"";
    return path.substr(0, p + 1);
}

static bool EndsWithNoCase_RS(const std::wstring& s, const std::wstring& suffix)
{
    if (s.size() < suffix.size()) return false;
    std::wstring a = s.substr(s.size() - suffix.size());
    std::wstring b = suffix;
    std::transform(a.begin(), a.end(), a.begin(), towlower);
    std::transform(b.begin(), b.end(), b.begin(), towlower);
    return a == b;
}

static bool FileExists_RS(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

#pragma pack(push, 1)
struct TGAHeader_RS
{
    uint8_t  idLength;
    uint8_t  colorMapType;
    uint8_t  imageType;
    uint16_t colorMapFirstEntryIndex;
    uint16_t colorMapLength;
    uint8_t  colorMapEntrySize;
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t  pixelDepth;
    uint8_t  imageDescriptor;
};
#pragma pack(pop)

static void LoadTexture_TGA_Internal_RS(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filePath,
    ComPtr<ID3D12Resource>& tex,
    ComPtr<ID3D12Resource>& upload)
{
    std::ifstream fin(filePath, std::ios::binary);
    if (!fin)
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Cannot open TGA", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Cannot open TGA");
    }

    TGAHeader_RS hdr = {};
    fin.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    if (!fin || hdr.imageType != 2 || (hdr.pixelDepth != 24 && hdr.pixelDepth != 32))
        throw std::runtime_error("Unsupported TGA");

    if (hdr.idLength > 0)
        fin.seekg(hdr.idLength, std::ios::cur);

    const UINT width = hdr.width;
    const UINT height = hdr.height;
    const UINT srcBpp = hdr.pixelDepth / 8;
    const UINT srcRowPitch = width * srcBpp;
    const UINT srcImageSize = srcRowPitch * height;

    std::vector<uint8_t> src(srcImageSize);
    fin.read(reinterpret_cast<char*>(src.data()), srcImageSize);
    if (!fin)
        throw std::runtime_error("Failed to read TGA");

    std::vector<uint8_t> rgba(width * height * 4);
    const bool topOrigin = (hdr.imageDescriptor & 0x20) != 0;

    for (UINT y = 0; y < height; ++y)
    {
        UINT srcY = topOrigin ? y : (height - 1 - y);
        const uint8_t* srcRow = src.data() + srcY * srcRowPitch;
        uint8_t* dstRow = rgba.data() + y * width * 4;

        for (UINT x = 0; x < width; ++x)
        {
            const uint8_t* p = srcRow + x * srcBpp;
            uint8_t* d = dstRow + x * 4;
            d[0] = p[2];
            d[1] = p[1];
            d[2] = p[0];
            d[3] = (srcBpp == 4) ? p[3] : 255;
        }
    }

    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&tex)));

    const UINT rowPitch = width * 4;
    const UINT imageSize = rowPitch * height;
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex.Get(), 0, 1);

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload)));

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = rgba.data();
    subData.RowPitch = rowPitch;
    subData.SlicePitch = imageSize;

    UpdateSubresources(cmdList, tex.Get(), upload.Get(), 0, 0, 1, &subData);

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
}

RenderingSystem::RenderingSystem(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    UINT rtvDescriptorSize,
    UINT cbvSrvUavDescriptorSize,
    UINT width,
    UINT height)
    : mDevice(device)
    , mInitCmdList(cmdList)
    , mRtvDescriptorSize(rtvDescriptorSize)
    , mCbvSrvUavDescriptorSize(cbvSrvUavDescriptorSize)
    , mWidth(width)
    , mHeight(height)
{
    float aspect = (float)width / (float)height;
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 0.1f, 5000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void RenderingSystem::BuildResources()
{
    BuildSceneGeometry();
    BuildSceneTextures();

    mGBuffer.Initialize(
        mDevice,
        mWidth,
        mHeight,
        mRtvDescriptorSize,
        mCbvSrvUavDescriptorSize);

    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildRootSignatures();
    BuildPSOs();
}

void RenderingSystem::OnResize(UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;
    if (width == 0 || height == 0)
        return;

    float aspect = (float)width / (float)height;
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 0.1f, 5000.0f);
    XMStoreFloat4x4(&mProj, P);

    mGBuffer.OnResize(mDevice, width, height);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = mGBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();

    CD3DX12_CPU_DESCRIPTOR_HANDLE gbufSrvCpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    gbufSrvCpu.Offset((INT)mGBufferSrvStartIndex, mCbvSrvUavDescriptorSize);

    CD3DX12_GPU_DESCRIPTOR_HANDLE gbufSrvGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    gbufSrvGpu.Offset((INT)mGBufferSrvStartIndex, mCbvSrvUavDescriptorSize);

    mGBuffer.CreateDescriptors(mDevice, rtvStart, gbufSrvCpu, gbufSrvGpu);
}

void RenderingSystem::BuildSceneGeometry()
{
    std::wstring exeDir = GetExeDir_RS();

    // Sponza
    mSponzaScene.ObjPath = exeDir + L"Models\\sponza.obj";
    mSponzaScene.AssetDir = GetDirPart_RS(mSponzaScene.ObjPath);
    mSponzaScene.UseTessellation = false;

    XMStoreFloat4x4(&mSponzaScene.World,
        XMMatrixScaling(0.1f, 0.1f, 0.1f) *
        XMMatrixTranslation(0.0f, -1.0f, 0.0f));

    if (!ObjLoader::LoadObjPosNormalTex(mSponzaScene.ObjPath, mSponzaScene.CpuMesh, false))
    {
        MessageBoxW(nullptr, mSponzaScene.ObjPath.c_str(), L"SPONZA OBJ NOT FOUND", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Sponza load failed");
    }
    mSponzaScene.DrawSubmeshes = mSponzaScene.CpuMesh.Submeshes;

    // Tessellation scene
    mTessScene.ObjPath = exeDir + L"Models\\cylinder.obj";
    mTessScene.AssetDir = GetDirPart_RS(mTessScene.ObjPath);
    mTessScene.UseTessellation = true;
    mTessScene.TessMin = 1.0f;
    mTessScene.TessMax = 6.0f;
    mTessScene.TessMaxDistance = 10.0f;
    mTessScene.DisplacementScale = 0.3f;
    mTessScene.NormalMapFlipY = 0.0f;

    XMStoreFloat4x4(&mTessScene.World,
        XMMatrixScaling(1.5f, 1.5f, 1.5f) *
        XMMatrixTranslation(0.0f, 0.0f, 3.0f));

    if (!ObjLoader::LoadObjPosNormalTex(mTessScene.ObjPath, mTessScene.CpuMesh, false))
    {
        MessageBoxW(nullptr, mTessScene.ObjPath.c_str(), L"TESS OBJ NOT FOUND", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Tess mesh load failed");
    }
    mTessScene.DrawSubmeshes = mTessScene.CpuMesh.Submeshes;

    // Optimization scene with real OBJ
    mOptimizationScene.ObjPath = exeDir + L"Models\\Box of bottles.obj";
    mOptimizationScene.AssetDir = GetDirPart_RS(mOptimizationScene.ObjPath);
    mOptimizationScene.UseTessellation = false;

    XMStoreFloat4x4(&mOptimizationScene.World,
        XMMatrixScaling(0.35f, 0.35f, 0.35f));

    if (!ObjLoader::LoadObjPosNormalTex(mOptimizationScene.ObjPath, mOptimizationScene.CpuMesh, false))
    {
        MessageBoxW(nullptr, mOptimizationScene.ObjPath.c_str(), L"OPTIMIZATION OBJ NOT FOUND", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Optimization mesh load failed");
    }

    mOptimizationScene.DrawSubmeshes = mOptimizationScene.CpuMesh.Submeshes;

    auto buildGpuBuffers = [&](SceneMesh& scene)
        {
            const UINT vbSize = (UINT)(scene.CpuMesh.Vertices.size() * sizeof(VertexPosNormalTangentTex));
            const UINT ibSize = (UINT)(scene.CpuMesh.Indices.size() * sizeof(uint32_t));

            CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
            CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);
            D3D12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(ibSize);

            ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&scene.VertexBuffer)));
            ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&scene.VBUpload)));

            void* mapped = nullptr;
            ThrowIfFailed(scene.VBUpload->Map(0, nullptr, &mapped));
            memcpy(mapped, scene.CpuMesh.Vertices.data(), vbSize);
            scene.VBUpload->Unmap(0, nullptr);

            mInitCmdList->CopyBufferRegion(scene.VertexBuffer.Get(), 0, scene.VBUpload.Get(), 0, vbSize);
            auto vbBarrier = CD3DX12_RESOURCE_BARRIER::Transition(scene.VertexBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            mInitCmdList->ResourceBarrier(1, &vbBarrier);

            ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&scene.IndexBuffer)));
            ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&scene.IBUpload)));

            ThrowIfFailed(scene.IBUpload->Map(0, nullptr, &mapped));
            memcpy(mapped, scene.CpuMesh.Indices.data(), ibSize);
            scene.IBUpload->Unmap(0, nullptr);

            mInitCmdList->CopyBufferRegion(scene.IndexBuffer.Get(), 0, scene.IBUpload.Get(), 0, ibSize);
            auto ibBarrier = CD3DX12_RESOURCE_BARRIER::Transition(scene.IndexBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
            mInitCmdList->ResourceBarrier(1, &ibBarrier);

            scene.VBV.BufferLocation = scene.VertexBuffer->GetGPUVirtualAddress();
            scene.VBV.StrideInBytes = sizeof(VertexPosNormalTangentTex);
            scene.VBV.SizeInBytes = vbSize;

            scene.IBV.BufferLocation = scene.IndexBuffer->GetGPUVirtualAddress();
            scene.IBV.Format = DXGI_FORMAT_R32_UINT;
            scene.IBV.SizeInBytes = ibSize;
        };

    buildGpuBuffers(mSponzaScene);
    buildGpuBuffers(mTessScene);
    buildGpuBuffers(mOptimizationScene);

    BuildOptimizationSceneObjects();
    BuildOptimizationOctree();
}

void RenderingSystem::BuildOptimizationSceneObjects()
{
    mOptObjects.clear();

    const int gridX = 16;
    const int gridZ = 16;
    const float spacing = 6.0f;

    const float startX = -0.5f * (gridX - 1) * spacing;
    const float startZ = -0.5f * (gridZ - 1) * spacing;

    std::mt19937 rng(1337u);
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.5f);
    std::uniform_real_distribution<float> yawDist(0.0f, XM_2PI);
    std::uniform_real_distribution<float> jitterDist(-0.7f, 0.7f);
    std::uniform_real_distribution<float> bobAmpDist(0.12f, 0.35f);
    std::uniform_real_distribution<float> bobSpeedDist(0.8f, 1.6f);
    std::uniform_real_distribution<float> rotSpeedDist(0.25f, 0.9f);
    std::uniform_real_distribution<float> phaseDist(0.0f, XM_2PI);

    float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
    float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

    for (int z = 0; z < gridZ; ++z)
    {
        for (int x = 0; x < gridX; ++x)
        {
            const float sx = scaleDist(rng);
            const float sy = scaleDist(rng) * 1.2f;
            const float sz = scaleDist(rng);

            const float px = startX + x * spacing + jitterDist(rng);
            const float pz = startZ + z * spacing + jitterDist(rng);
            const float py = 0.0f;

            SceneObject obj;
            obj.BasePosition = XMFLOAT3(px, py, pz);
            obj.Scale = XMFLOAT3(sx, sy, sz);
            obj.BaseYaw = yawDist(rng);
            obj.AnimationTime = 0.0f;
            obj.AnimationAccumulatedDt = 0.0f;
            obj.BobAmplitude = bobAmpDist(rng);
            obj.BobSpeed = bobSpeedDist(rng);
            obj.RotationSpeed = rotSpeedDist(rng);
            obj.AnimationPhase = phaseDist(rng);
            obj.UpdateRate = 1;
            obj.FrameCounter = (x + z) & 3;

            RebuildOptimizationObjectWorld(obj);
            mOptObjects.push_back(obj);

            const BoundingSphere& bounds = mOptObjects.back().Bounds;

            minX = (std::min)(minX, bounds.Center.x - bounds.Radius);
            minY = (std::min)(minY, bounds.Center.y - bounds.Radius);
            minZ = (std::min)(minZ, bounds.Center.z - bounds.Radius);

            maxX = (std::max)(maxX, bounds.Center.x + bounds.Radius);
            maxY = (std::max)(maxY, bounds.Center.y + bounds.Radius);
            maxZ = (std::max)(maxZ, bounds.Center.z + bounds.Radius);
        }
    }

    mOptSceneCenter = XMFLOAT3(
        0.5f * (minX + maxX),
        0.5f * (minY + maxY),
        0.5f * (minZ + maxZ));

    mOptSceneExtents = XMFLOAT3(
        0.5f * (maxX - minX) + 1.0f,
        0.5f * (maxY - minY) + 1.0f,
        0.5f * (maxZ - minZ) + 1.0f);

    mLastTotalCount = (UINT)mOptObjects.size();
}

int RenderingSystem::GetAnimationUpdateRate(float distanceSq) const
{
    const float nearDist = 15.0f;
    const float midDist = 35.0f;
    const float farDist = 70.0f;

    if (distanceSq < nearDist * nearDist)
        return 1;

    if (distanceSq < midDist * midDist)
        return 8;

    if (distanceSq < farDist * farDist)
        return 12;

    return 25;
}

void RenderingSystem::RebuildOptimizationObjectWorld(SceneObject& obj)
{
    const float bobOffset = obj.BobAmplitude * sinf(obj.AnimationTime * obj.BobSpeed + obj.AnimationPhase);
    const float yaw = obj.BaseYaw + obj.AnimationTime * obj.RotationSpeed;

    XMMATRIX world =
        XMMatrixScaling(obj.Scale.x, obj.Scale.y, obj.Scale.z) *
        XMMatrixRotationY(yaw) *
        XMMatrixTranslation(obj.BasePosition.x, obj.BasePosition.y + bobOffset, obj.BasePosition.z);

    XMStoreFloat4x4(&obj.World, world);

    const float baseModelRadius = 1.2f;
    obj.Bounds.Center = XMFLOAT3(
        obj.BasePosition.x,
        obj.BasePosition.y + bobOffset + obj.Scale.y,
        obj.BasePosition.z);
    obj.Bounds.Radius = baseModelRadius * (std::max)((std::max)(obj.Scale.x, obj.Scale.y), obj.Scale.z);
}

void RenderingSystem::UpdateOptimizationSceneAnimation(float deltaTime)
{
    mLastAnimatedCount = 0;

    for (SceneObject& obj : mOptObjects)
    {
        const float dx = obj.BasePosition.x - mCameraPos.x;
        const float dy = obj.BasePosition.y - mCameraPos.y;
        const float dz = obj.BasePosition.z - mCameraPos.z;
        const float distanceSq = dx * dx + dy * dy + dz * dz;

        obj.UpdateRate = GetAnimationUpdateRate(distanceSq);
        obj.AnimationAccumulatedDt += deltaTime;
        obj.FrameCounter++;

        if (obj.FrameCounter < obj.UpdateRate)
            continue;

        obj.FrameCounter = 0;
        obj.AnimationTime += obj.AnimationAccumulatedDt;
        obj.AnimationAccumulatedDt = 0.0f;

        RebuildOptimizationObjectWorld(obj);
        ++mLastAnimatedCount;
    }
}

void RenderingSystem::BuildOptimizationOctree()
{
    std::vector<int> indices;
    indices.reserve(mOptObjects.size());

    for (int i = 0; i < (int)mOptObjects.size(); ++i)
        indices.push_back(i);

    mOctreeRoot = BuildOctreeNode(mOptSceneCenter, mOptSceneExtents, indices, 0);
}

std::unique_ptr<OctreeNode> RenderingSystem::BuildOctreeNode(
    const XMFLOAT3& center,
    const XMFLOAT3& extents,
    const std::vector<int>& objectIndices,
    int depth)
{
    auto node = std::make_unique<OctreeNode>();
    node->Center = center;
    node->Extents = extents;
    node->ObjectIndices = objectIndices;
    node->IsLeaf = true;

    const int maxDepth = 6;
    const int maxObjectsPerLeaf = 24;

    if (depth >= maxDepth || (int)objectIndices.size() <= maxObjectsPerLeaf)
        return node;

    XMFLOAT3 childExtents(extents.x * 0.5f, extents.y * 0.5f, extents.z * 0.5f);

    std::vector<int> childLists[8];

    for (int objIndex : objectIndices)
    {
        const BoundingSphere& s = mOptObjects[objIndex].Bounds;

        for (int c = 0; c < 8; ++c)
        {
            XMFLOAT3 childCenter = center;
            childCenter.x += (c & 1) ? childExtents.x : -childExtents.x;
            childCenter.y += (c & 2) ? childExtents.y : -childExtents.y;
            childCenter.z += (c & 4) ? childExtents.z : -childExtents.z;

            bool overlapX = std::fabs(s.Center.x - childCenter.x) <= (childExtents.x + s.Radius);
            bool overlapY = std::fabs(s.Center.y - childCenter.y) <= (childExtents.y + s.Radius);
            bool overlapZ = std::fabs(s.Center.z - childCenter.z) <= (childExtents.z + s.Radius);

            if (overlapX && overlapY && overlapZ)
                childLists[c].push_back(objIndex);
        }
    }

    bool anySplit = false;
    for (int c = 0; c < 8; ++c)
    {
        if (childLists[c].empty())
            continue;

        if ((int)childLists[c].size() == (int)objectIndices.size())
            continue;

        anySplit = true;

        XMFLOAT3 childCenter = center;
        childCenter.x += (c & 1) ? childExtents.x : -childExtents.x;
        childCenter.y += (c & 2) ? childExtents.y : -childExtents.y;
        childCenter.z += (c & 4) ? childExtents.z : -childExtents.z;

        node->Children[c] = BuildOctreeNode(childCenter, childExtents, childLists[c], depth + 1);
    }

    if (anySplit)
        node->IsLeaf = false;

    return node;
}

void RenderingSystem::BuildSceneTextures()
{
    mTextures.clear();
    mTextureUploads.clear();

    auto resolveTexture = [](const std::wstring& assetDir, const std::string& file) -> std::wstring
        {
            if (file.empty())
                return L"";

            std::wstring rel(file.begin(), file.end());
            for (wchar_t& c : rel)
            {
                if (c == L'/')
                    c = L'\\';
            }

            if (rel.size() > 1 && rel[1] == L':')
                return rel;

            return assetDir + rel;
        };

    auto addTex = [&](const std::wstring& path, UINT solidRGBAIfMissing) -> UINT
        {
            UINT idx = (UINT)mTextures.size();
            mTextures.push_back(nullptr);
            mTextureUploads.push_back(nullptr);

            if (!path.empty() && FileExists_RS(path))
                LoadTexture_WIC(path, mTextures.back(), mTextureUploads.back());
            else
                CreateSolidTextureRGBA(solidRGBAIfMissing, mTextures.back(), mTextureUploads.back());

            return idx;
        };

    auto tryResolveTexture = [&](const SceneMesh& scene, const std::string& file) -> std::wstring
        {
            if (file.empty())
                return L"";

            std::wstring rel(file.begin(), file.end());
            for (wchar_t& c : rel)
            {
                if (c == L'/')
                    c = L'\\';
            }

            std::wstring full = resolveTexture(scene.AssetDir, file);
            if (FileExists_RS(full))
                return full;

            size_t slashPos = rel.find_last_of(L"\\/");
            std::wstring fileOnly = (slashPos == std::wstring::npos) ? rel : rel.substr(slashPos + 1);

            std::wstring exeDir = GetExeDir_RS();
            std::wstring modelsDir = exeDir + L"Models\\";
            std::wstring try1 = scene.AssetDir + fileOnly;
            std::wstring try2 = modelsDir + fileOnly;
            std::wstring try3 = modelsDir + rel;
            std::wstring try4 = modelsDir + L"textures\\" + fileOnly;

            if (FileExists_RS(try1)) return try1;
            if (FileExists_RS(try2)) return try2;
            if (FileExists_RS(try3)) return try3;
            if (FileExists_RS(try4)) return try4;

            return L"";
        };

    auto buildSceneMaterialSrvs = [&](SceneMesh& scene)
        {
            scene.SubmeshBaseSrv.clear();
            scene.SubmeshBaseSrv.reserve(scene.DrawSubmeshes.size());

            for (const ObjSubmesh& sm : scene.DrawSubmeshes)
            {
                ObjMaterialInfo mat{};

                auto it = scene.CpuMesh.Materials.find(sm.MaterialName);
                if (it == scene.CpuMesh.Materials.end())
                {
                    std::string key = sm.MaterialName;
                    std::transform(key.begin(), key.end(), key.begin(),
                        [](unsigned char c) { return (char)tolower(c); });
                    it = scene.CpuMesh.Materials.find(key);
                }

                if (it != scene.CpuMesh.Materials.end())
                    mat = it->second;

                std::wstring diff = tryResolveTexture(scene, mat.DiffuseMap);
                std::wstring norm = tryResolveTexture(scene, mat.NormalMap);
                std::wstring disp = tryResolveTexture(scene, mat.DisplacementMap);

                UINT base = (UINT)mTextures.size();

                addTex(diff, 0xFFFFFFFFu);   // white
                addTex(norm, 0xFF8080FFu);   // flat normal
                addTex(disp, 0xFF000000u);   // black height

                scene.SubmeshBaseSrv.push_back(base);
            }
        };

 
    buildSceneMaterialSrvs(mSponzaScene);
    buildSceneMaterialSrvs(mTessScene);

    {
        mOptimizationScene.SubmeshBaseSrv.clear();
        mOptimizationScene.SubmeshBaseSrv.reserve(mOptimizationScene.DrawSubmeshes.size());

        std::wstring diff = mOptimizationScene.AssetDir + L"Diffuse.jpg";
        std::wstring norm = mOptimizationScene.AssetDir + L"Normal.jpg";

        for (size_t i = 0; i < mOptimizationScene.DrawSubmeshes.size(); ++i)
        {
            UINT base = (UINT)mTextures.size();

            addTex(diff, 0xFFFFFFFFu);   // diffuse
            addTex(norm, 0xFF8080FFu);   // normal
            addTex(L"", 0xFF000000u);   

            mOptimizationScene.SubmeshBaseSrv.push_back(base);
        }
    }

    mModelTextureCount = (UINT)mTextures.size();
    mGBufferSrvStartIndex = mModelTextureCount;
}
void RenderingSystem::BuildDescriptorHeaps()
{
    const UINT extra = 10;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = mModelTextureCount + 3 + extra;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 3;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mGBufferRtvHeap)));

    for (UINT i = 0; i < (UINT)mTextures.size(); ++i)
        CreateTextureSrv(i, mTextures[i].Get());

    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = mGBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();

    CD3DX12_CPU_DESCRIPTOR_HANDLE gbufSrvCpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    gbufSrvCpu.Offset((INT)mGBufferSrvStartIndex, mCbvSrvUavDescriptorSize);

    CD3DX12_GPU_DESCRIPTOR_HANDLE gbufSrvGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    gbufSrvGpu.Offset((INT)mGBufferSrvStartIndex, mCbvSrvUavDescriptorSize);

    mGBuffer.CreateDescriptors(mDevice, rtvStart, gbufSrvCpu, gbufSrvGpu);
}

void RenderingSystem::BuildConstantBuffers()
{
    mGeometryCBByteSize = AlignCB_RS(sizeof(GeometryConstants));

    UINT lightSize = AlignCB_RS(sizeof(LightConstants));

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC geomCbDesc = CD3DX12_RESOURCE_DESC::Buffer(mGeometryCBByteSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &geomCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mGeometryCB)));

    UINT64 optCbSize = (UINT64)mGeometryCBByteSize * (UINT64)(mOptObjects.empty() ? 1 : mOptObjects.size());
    D3D12_RESOURCE_DESC optGeomCbDesc = CD3DX12_RESOURCE_DESC::Buffer(optCbSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &optGeomCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mOptimizationGeometryCB)));

    D3D12_RESOURCE_DESC lightCbDesc = CD3DX12_RESOURCE_DESC::Buffer(lightSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &lightCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mLightingCB)));
}

void RenderingSystem::BuildRootSignatures()
{
    {
        CD3DX12_ROOT_PARAMETER rootParams[2];
        rootParams[0].InitAsConstantBufferView(0);

        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
        rootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_STATIC_SAMPLER_DESC staticSamp(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP);

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
            _countof(rootParams), rootParams,
            1, &staticSamp,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serializedRootSig;
        ComPtr<ID3DBlob> errorBlob;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSig, &errorBlob));
        ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mGeometryRootSig)));
    }

    {
        CD3DX12_ROOT_PARAMETER rootParams[2];
        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
        rootParams[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[1].InitAsConstantBufferView(0);

        CD3DX12_STATIC_SAMPLER_DESC staticSamp(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
            _countof(rootParams), rootParams,
            1, &staticSamp,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serializedRootSig;
        ComPtr<ID3DBlob> errorBlob;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSig, &errorBlob));
        ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mLightingRootSig)));
    }
}

void RenderingSystem::BuildPSOs()
{
    ComPtr<ID3DBlob> gVs, gPs, tessVs, gHs, gDs, lVs, lPs, errors;
    HRESULT hr = S_OK;

    auto compile = [&](const wchar_t* path, const char* entry, const char* target, ComPtr<ID3DBlob>& out)
        {
            errors.Reset();
            hr = D3DCompileFromFile(path, nullptr, nullptr, entry, target,
                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &out, &errors);
            if (FAILED(hr))
            {
                if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "Shader compile error", MB_OK);
                ThrowIfFailed(hr);
            }
        };

    compile(L"Shaders/GBufferVS.hlsl", "VSMain", "vs_5_0", gVs);
    compile(L"Shaders/GBufferPS.hlsl", "PSMain", "ps_5_0", gPs);
    compile(L"Shaders/GBufferTessVS.hlsl", "VSMain", "vs_5_0", tessVs);
    compile(L"Shaders/GBufferHS.hlsl", "HSMain", "hs_5_0", gHs);
    compile(L"Shaders/GBufferDS.hlsl", "DSMain", "ds_5_0", gDs);
    compile(L"Shaders/DeferredLightVS.hlsl", "VSMain", "vs_5_0", lVs);
    compile(L"Shaders/DeferredLightPS.hlsl", "PSMain", "ps_5_0", lPs);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.pRootSignature = mGeometryRootSig.Get();
        psoDesc.VS = { gVs->GetBufferPointer(), gVs->GetBufferSize() };
        psoDesc.PS = { gPs->GetBufferPointer(), gPs->GetBufferSize() };

        CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
        rast.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState = rast;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 3;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mGeometryPSO)));
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.pRootSignature = mGeometryRootSig.Get();
        psoDesc.VS = { tessVs->GetBufferPointer(), tessVs->GetBufferSize() };
        psoDesc.HS = { gHs->GetBufferPointer(), gHs->GetBufferSize() };
        psoDesc.DS = { gDs->GetBufferPointer(), gDs->GetBufferSize() };
        psoDesc.PS = { gPs->GetBufferPointer(), gPs->GetBufferSize() };

        CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
        rast.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState = rast;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        psoDesc.NumRenderTargets = 3;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.RTVFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mTessPSO)));
    }

    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { nullptr, 0 };
        psoDesc.pRootSignature = mLightingRootSig.Get();
        psoDesc.VS = { lVs->GetBufferPointer(), lVs->GetBufferSize() };
        psoDesc.PS = { lPs->GetBufferPointer(), lPs->GetBufferSize() };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

        D3D12_DEPTH_STENCIL_DESC ds = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        ds.DepthEnable = FALSE;
        ds.StencilEnable = FALSE;
        psoDesc.DepthStencilState = ds;

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mLightingPSO)));
    }
}

void RenderingSystem::LoadTexture_WIC(const std::wstring& filePath,
    ComPtr<ID3D12Resource>& tex,
    ComPtr<ID3D12Resource>& upload)
{
    if (EndsWithNoCase_RS(filePath, L".tga"))
    {
        LoadTexture_TGA_Internal_RS(mDevice, mInitCmdList, filePath, tex, upload);
        return;
    }

    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)));

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr))
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Cannot load texture (WIC)", MB_OK | MB_ICONERROR);
        ThrowIfFailed(hr);
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame));

    UINT width = 0, height = 0;
    ThrowIfFailed(frame->GetSize(&width, &height));

    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter));
    ThrowIfFailed(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

    const UINT rowPitch = width * 4;
    const UINT imageSize = rowPitch * height;
    std::vector<uint8_t> pixels(imageSize);
    ThrowIfFailed(converter->CopyPixels(nullptr, rowPitch, imageSize, pixels.data()));

    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex.Get(), 0, 1);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = pixels.data();
    subData.RowPitch = rowPitch;
    subData.SlicePitch = imageSize;

    UpdateSubresources(mInitCmdList, tex.Get(), upload.Get(), 0, 0, 1, &subData);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mInitCmdList->ResourceBarrier(1, &barrier);
}

void RenderingSystem::CreateSolidTextureRGBA(UINT rgba,
    ComPtr<ID3D12Resource>& tex,
    ComPtr<ID3D12Resource>& upload)
{
    const UINT width = 1;
    const UINT height = 1;
    uint8_t pixel[4] =
    {
        (uint8_t)((rgba >> 16) & 0xFF),
        (uint8_t)((rgba >> 8) & 0xFF),
        (uint8_t)(rgba & 0xFF),
        (uint8_t)((rgba >> 24) & 0xFF)
    };

    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex.Get(), 0, 1);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)));

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = pixel;
    subData.RowPitch = 4;
    subData.SlicePitch = 4;

    UpdateSubresources(mInitCmdList, tex.Get(), upload.Get(), 0, 0, 1, &subData);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mInitCmdList->ResourceBarrier(1, &barrier);
}

void RenderingSystem::CreateTextureSrv(UINT srvIndex, ID3D12Resource* tex)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = tex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    hCpu.Offset((INT)srvIndex, mCbvSrvUavDescriptorSize);
    mDevice->CreateShaderResourceView(tex, &srvDesc, hCpu);
}

XMMATRIX RenderingSystem::GetViewMatrix() const
{
    XMVECTOR pos = XMLoadFloat3(&mCameraPos);
    XMVECTOR forward = XMVectorSet(
        cosf(mPitch) * sinf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * cosf(mYaw),
        0.0f);

    return XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0, 1, 0, 0));
}

XMMATRIX RenderingSystem::GetProjMatrix() const
{
    return XMLoadFloat4x4(&mProj);
}

XMMATRIX RenderingSystem::GetViewProjMatrix() const
{
    return GetViewMatrix() * GetProjMatrix();
}

void RenderingSystem::UpdateCamera(const InputDevice& input, float dt)
{
    const float moveSpeed = 10.0f;
    const float mouseSens = 0.0025f;

    if (input.IsMouseDown(1))
    {
        POINT md = input.MouseDelta();
        mYaw += md.x * mouseSens;
        mPitch += md.y * mouseSens;

        const float limit = XM_PIDIV2 - 0.1f;
        if (mPitch > limit)  mPitch = limit;
        if (mPitch < -limit) mPitch = -limit;
    }

    XMVECTOR forward = XMVectorSet(
        cosf(mPitch) * sinf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * cosf(mYaw),
        0.0f);

    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
    XMVECTOR pos = XMLoadFloat3(&mCameraPos);

    if (input.IsKeyDown('W')) pos += forward * moveSpeed * dt;
    if (input.IsKeyDown('S')) pos -= forward * moveSpeed * dt;
    if (input.IsKeyDown('A')) pos -= right * moveSpeed * dt;
    if (input.IsKeyDown('D')) pos += right * moveSpeed * dt;
    if (input.IsKeyDown('Q')) pos += up * moveSpeed * dt;
    if (input.IsKeyDown('E')) pos -= up * moveSpeed * dt;

    XMStoreFloat3(&mCameraPos, pos);
}

void RenderingSystem::UpdateObjectRotation(const InputDevice& input)
{
    if (mMode != RenderMode::Tessellation)
        return;

    if (input.IsMouseDown(0))
    {
        const float rotSpeed = 0.01f;
        POINT md = input.MouseDelta();
        mObjectYaw += md.x * rotSpeed;
        mObjectPitch += md.y * rotSpeed;

        const float limit = XM_PIDIV2 - 0.01f;
        if (mObjectPitch > limit)  mObjectPitch = limit;
        if (mObjectPitch < -limit) mObjectPitch = -limit;
    }
}

void RenderingSystem::UpdateGeometryCB(const SceneMesh& scene)
{
    XMMATRIX baseWorld = XMLoadFloat4x4(&scene.World);
    XMMATRIX world =
        XMMatrixRotationX(mObjectPitch) *
        XMMatrixRotationY(mObjectYaw) *
        baseWorld;

    UpdateGeometryCBWithWorld(scene, world);
}

void RenderingSystem::UpdateGeometryCBWithWorld(const SceneMesh& scene, CXMMATRIX world)
{
    XMMATRIX viewProj = GetViewProjMatrix();

    XMStoreFloat4x4(&mGeometryData.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&mGeometryData.ViewProj, XMMatrixTranspose(viewProj));
    mGeometryData.Tiling = XMFLOAT2(1.0f, 1.0f);
    mGeometryData.UVOffset = XMFLOAT2(0.0f, 0.0f);
    mGeometryData.EyePosW = mCameraPos;
    mGeometryData.TessMin = scene.TessMin;
    mGeometryData.TessMax = scene.TessMax;
    mGeometryData.TessMaxDistance = scene.TessMaxDistance;
    mGeometryData.DisplacementScale = scene.DisplacementScale;
    mGeometryData.NormalMapFlipY = scene.NormalMapFlipY;

    void* mapped = nullptr;
    ThrowIfFailed(mGeometryCB->Map(0, nullptr, &mapped));
    memcpy(mapped, &mGeometryData, sizeof(GeometryConstants));
    mGeometryCB->Unmap(0, nullptr);
}

void RenderingSystem::UpdateOptimizationGeometryCB(UINT objectIndex, CXMMATRIX world)
{
    XMMATRIX viewProj = GetViewProjMatrix();

    GeometryConstants data = {};
    XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&data.ViewProj, XMMatrixTranspose(viewProj));
    data.Tiling = XMFLOAT2(1.0f, 1.0f);
    data.UVOffset = XMFLOAT2(0.0f, 0.0f);
    data.EyePosW = mCameraPos;
    data.TessMin = mOptimizationScene.TessMin;
    data.TessMax = mOptimizationScene.TessMax;
    data.TessMaxDistance = mOptimizationScene.TessMaxDistance;
    data.DisplacementScale = mOptimizationScene.DisplacementScale;
    data.NormalMapFlipY = mOptimizationScene.NormalMapFlipY;

    BYTE* mapped = nullptr;
    ThrowIfFailed(mOptimizationGeometryCB->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
    memcpy(mapped + (size_t)objectIndex * mGeometryCBByteSize, &data, sizeof(GeometryConstants));
    mOptimizationGeometryCB->Unmap(0, nullptr);
}

void RenderingSystem::UpdateLightCB(float totalTime)
{
    mLightingData.EyePosW = mCameraPos;

    mLightingData.AmbientColor = { 0.18f, 0.18f, 0.20f };

    mLightingData.DirLight.Direction = { 0.5f, -1.0f, -0.3f };
    mLightingData.DirLight.Color = { 1.0f, 1.0f, 1.0f };
    mLightingData.DirLight.Intensity = 1.0f;

    mLightingData.PointLights[0].Position = { 8.0f * cosf(totalTime), 4.0f, 8.0f * sinf(totalTime) };
    mLightingData.PointLights[0].Range = 20.0f;
    mLightingData.PointLights[0].Color = { 1.0f, 0.2f, 0.2f };
    mLightingData.PointLights[0].Intensity = 2.0f;

    mLightingData.PointLights[1].Position = { 8.0f * cosf(-0.6f * totalTime), 6.0f, 8.0f * sinf(-0.6f * totalTime) };
    mLightingData.PointLights[1].Range = 20.0f;
    mLightingData.PointLights[1].Color = { 0.2f, 0.4f, 1.0f };
    mLightingData.PointLights[1].Intensity = 2.0f;

    mLightingData.SpotLight.Position = { 0.0f, 10.0f, -5.0f };
    mLightingData.SpotLight.Direction = { 0.0f, -1.0f, 0.3f };
    mLightingData.SpotLight.Range = 40.0f;
    mLightingData.SpotLight.SpotPower = 24.0f;
    mLightingData.SpotLight.Color = { 0.9f, 1.0f, 0.7f };
    mLightingData.SpotLight.Intensity = 2.0f;

    BYTE* mapped = nullptr;
    D3D12_RANGE readRange = {};
    ThrowIfFailed(mLightingCB->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
    memcpy(mapped, &mLightingData, sizeof(LightConstants));
    mLightingCB->Unmap(0, nullptr);
}

void RenderingSystem::ResetCameraForMode(RenderMode mode)
{
    mObjectYaw = 0.0f;
    mObjectPitch = 0.0f;

    switch (mode)
    {
    case RenderMode::Sponza:
        mCameraPos = { 0.0f, 1.5f, -2.0f };
        mYaw = 0.0f;
        mPitch = 0.0f;
        break;

    case RenderMode::Tessellation:
        mCameraPos = { 0.0f, 0.0f, 0.0f };
        mYaw = 0.0f;
        mPitch = 0.0f;
        break;

    case RenderMode::Optimization:
        mCameraPos = { 0.0f, 8.0f, -25.0f };
        mYaw = 0.0f;
        mPitch = 0.05f;
        break;
    }
}

std::array<XMFLOAT4, 6> RenderingSystem::ExtractFrustumPlanes(CXMMATRIX viewProj) const
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, viewProj);

    std::array<XMFLOAT4, 6> p;

    p[0] = XMFLOAT4(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
    p[1] = XMFLOAT4(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
    p[2] = XMFLOAT4(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42);
    p[3] = XMFLOAT4(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);
    p[4] = XMFLOAT4(m._13, m._23, m._33, m._43);
    p[5] = XMFLOAT4(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);

    for (auto& plane : p)
    {
        float len = sqrtf(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
        if (len > 0.0f)
        {
            plane.x /= len;
            plane.y /= len;
            plane.z /= len;
            plane.w /= len;
        }
    }

    return p;
}

bool RenderingSystem::SphereInsideFrustum(const BoundingSphere& sphere, const std::array<XMFLOAT4, 6>& planes) const
{
    for (const XMFLOAT4& p : planes)
    {
        float d = p.x * sphere.Center.x + p.y * sphere.Center.y + p.z * sphere.Center.z + p.w;
        if (d < -sphere.Radius)
            return false;
    }
    return true;
}

bool RenderingSystem::AabbInsideFrustum(
    const XMFLOAT3& center,
    const XMFLOAT3& extents,
    const std::array<XMFLOAT4, 6>& planes) const
{
    for (const XMFLOAT4& p : planes)
    {
        float r = extents.x * fabsf(p.x) + extents.y * fabsf(p.y) + extents.z * fabsf(p.z);
        float s = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (s + r < 0.0f)
            return false;
    }
    return true;
}

void RenderingSystem::CollectVisibleObjectsLinear(const std::array<XMFLOAT4, 6>& planes, std::vector<int>& outVisible) const
{
    outVisible.clear();
    outVisible.reserve(mOptObjects.size());

    for (int i = 0; i < (int)mOptObjects.size(); ++i)
    {
        if (SphereInsideFrustum(mOptObjects[i].Bounds, planes))
            outVisible.push_back(i);
    }
}

void RenderingSystem::CollectVisibleObjectsOctree(
    const OctreeNode* node,
    const std::array<XMFLOAT4, 6>& planes,
    std::vector<int>& outVisible) const
{
    if (!node)
        return;

    if (!AabbInsideFrustum(node->Center, node->Extents, planes))
        return;

    if (node->IsLeaf)
    {
        for (int objIndex : node->ObjectIndices)
        {
            if (SphereInsideFrustum(mOptObjects[objIndex].Bounds, planes))
                outVisible.push_back(objIndex);
        }
        return;
    }

    for (const auto& child : node->Children)
    {
        if (child)
            CollectVisibleObjectsOctree(child.get(), planes, outVisible);
    }
}

void RenderingSystem::Update(float totalTime, float deltaTime, const InputDevice& input)
{
    if (input.WasKeyPressed('1'))
    {
        mMode = RenderMode::Sponza;
        ResetCameraForMode(mMode);
    }

    if (input.WasKeyPressed('2'))
    {
        mMode = RenderMode::Tessellation;
        ResetCameraForMode(mMode);
    }

    if (input.WasKeyPressed('3'))
    {
        mMode = RenderMode::Optimization;
        ResetCameraForMode(mMode);
    }

    if (input.WasKeyPressed('F'))
        mEnableFrustumCulling = !mEnableFrustumCulling;

    if (input.WasKeyPressed('O'))
        mEnableOctree = !mEnableOctree;

    UpdateObjectRotation(input);
    UpdateCamera(input, deltaTime);

    mLastAnimatedCount = 0;

    if (mMode == RenderMode::Sponza)
        UpdateGeometryCB(mSponzaScene);
    else if (mMode == RenderMode::Tessellation)
        UpdateGeometryCB(mTessScene);
    else if (mMode == RenderMode::Optimization)
        UpdateOptimizationSceneAnimation(deltaTime);

    UpdateLightCB(totalTime);

    mStatsPrintTimer += deltaTime;
    if (mStatsPrintTimer >= 1.0f)
    {
        mStatsPrintTimer = 0.0f;

        std::ostringstream oss;
        oss << "[DX12 OPT] mode=";
        if (mMode == RenderMode::Sponza) oss << "Sponza";
        else if (mMode == RenderMode::Tessellation) oss << "Tessellation";
        else oss << "Optimization";

        oss << " | frustum=" << (mEnableFrustumCulling ? "ON" : "OFF")
            << " | octree=" << (mEnableOctree ? "ON" : "OFF")
            << " | visible=" << mLastVisibleCount
            << " / total=" << mLastTotalCount;

        if (mMode == RenderMode::Optimization)
            oss << " | animUpdated=" << mLastAnimatedCount;

        oss << "\n";

        OutputDebugStringA(oss.str().c_str());
    }
}

void RenderingSystem::DrawSceneGeometryPass(
    ID3D12GraphicsCommandList* cmdList,
    const SceneMesh& scene,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv)
{
    mGBuffer.TransitionToRenderTarget(cmdList);
    mGBuffer.Clear(cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
    {
        mGBuffer.GetAlbedoRtv(),
        mGBuffer.GetNormalRtv(),
        mGBuffer.GetPositionRtv()
    };

    cmdList->OMSetRenderTargets(3, rtvs, TRUE, &depthDsv);
    cmdList->SetPipelineState(scene.UseTessellation ? mTessPSO.Get() : mGeometryPSO.Get());
    cmdList->SetGraphicsRootSignature(mGeometryRootSig.Get());

    cmdList->IASetPrimitiveTopology(scene.UseTessellation
        ? D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST
        : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->IASetVertexBuffers(0, 1, &scene.VBV);
    cmdList->IASetIndexBuffer(&scene.IBV);
    cmdList->SetGraphicsRootConstantBufferView(0, mGeometryCB->GetGPUVirtualAddress());

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    for (size_t i = 0; i < scene.DrawSubmeshes.size(); ++i)
    {
        UINT baseSrv = (i < scene.SubmeshBaseSrv.size()) ? scene.SubmeshBaseSrv[i] : 0;
        CD3DX12_GPU_DESCRIPTOR_HANDLE handle = hGpu;
        handle.Offset((INT)baseSrv, mCbvSrvUavDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(1, handle);

        const ObjSubmesh& sm = scene.DrawSubmeshes[i];
        cmdList->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, 0, 0);
    }

    mGBuffer.TransitionToShaderResource(cmdList);
}

void RenderingSystem::DrawOptimizationGeometryPass(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv)
{
    mGBuffer.TransitionToRenderTarget(cmdList);
    mGBuffer.Clear(cmdList);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3] =
    {
        mGBuffer.GetAlbedoRtv(),
        mGBuffer.GetNormalRtv(),
        mGBuffer.GetPositionRtv()
    };

    cmdList->OMSetRenderTargets(3, rtvs, TRUE, &depthDsv);
    cmdList->SetPipelineState(mGeometryPSO.Get());
    cmdList->SetGraphicsRootSignature(mGeometryRootSig.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mOptimizationScene.VBV);
    cmdList->IASetIndexBuffer(&mOptimizationScene.IBV);

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    std::vector<int> visibleObjects;
    mLastTotalCount = (UINT)mOptObjects.size();

    if (!mEnableFrustumCulling)
    {
        visibleObjects.reserve(mOptObjects.size());
        for (int i = 0; i < (int)mOptObjects.size(); ++i)
            visibleObjects.push_back(i);
    }
    else
    {
        auto planes = ExtractFrustumPlanes(GetViewProjMatrix());

        if (mEnableOctree && mOctreeRoot)
            CollectVisibleObjectsOctree(mOctreeRoot.get(), planes, visibleObjects);
        else
            CollectVisibleObjectsLinear(planes, visibleObjects);
    }

    std::sort(visibleObjects.begin(), visibleObjects.end());
    visibleObjects.erase(std::unique(visibleObjects.begin(), visibleObjects.end()), visibleObjects.end());

    mLastVisibleCount = (UINT)visibleObjects.size();

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHeapStart(mSrvHeap->GetGPUDescriptorHandleForHeapStart());

    for (int objIndex : visibleObjects)
    {
        XMMATRIX world = XMLoadFloat4x4(&mOptObjects[objIndex].World);
        UpdateOptimizationGeometryCB((UINT)objIndex, world);

        D3D12_GPU_VIRTUAL_ADDRESS gpuAddr =
            mOptimizationGeometryCB->GetGPUVirtualAddress() + (UINT64)objIndex * (UINT64)mGeometryCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, gpuAddr);

        for (size_t smIndex = 0; smIndex < mOptimizationScene.DrawSubmeshes.size(); ++smIndex)
        {
            UINT baseSrv = (smIndex < mOptimizationScene.SubmeshBaseSrv.size()) ? mOptimizationScene.SubmeshBaseSrv[smIndex] : 0;
            CD3DX12_GPU_DESCRIPTOR_HANDLE handle = srvHeapStart;
            handle.Offset((INT)baseSrv, mCbvSrvUavDescriptorSize);
            cmdList->SetGraphicsRootDescriptorTable(1, handle);

            const ObjSubmesh& sm = mOptimizationScene.DrawSubmeshes[smIndex];
            cmdList->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, 0, 0);
        }
    }

    mGBuffer.TransitionToShaderResource(cmdList);
}

void RenderingSystem::DrawLightingPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv)
{
    FLOAT clearColor[] = { 0.07f, 0.07f, 0.09f, 1.0f };
    cmdList->ClearRenderTargetView(backBufferRtv, clearColor, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &backBufferRtv, TRUE, nullptr);

    cmdList->SetPipelineState(mLightingPSO.Get());
    cmdList->SetGraphicsRootSignature(mLightingRootSig.Get());

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE gbufSrvGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    gbufSrvGpu.Offset((INT)mGBufferSrvStartIndex, mCbvSrvUavDescriptorSize);

    cmdList->SetGraphicsRootDescriptorTable(0, gbufSrvGpu);
    cmdList->SetGraphicsRootConstantBufferView(1, mLightingCB->GetGPUVirtualAddress());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

void RenderingSystem::Draw(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv)
{
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    switch (mMode)
    {
    case RenderMode::Sponza:
        DrawSceneGeometryPass(cmdList, mSponzaScene, depthDsv);
        break;

    case RenderMode::Tessellation:
        DrawSceneGeometryPass(cmdList, mTessScene, depthDsv);
        break;

    case RenderMode::Optimization:
        DrawOptimizationGeometryPass(cmdList, depthDsv);
        break;
    }

    DrawLightingPass(cmdList, backBufferRtv);
}