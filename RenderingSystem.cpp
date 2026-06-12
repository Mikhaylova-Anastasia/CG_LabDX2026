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
struct DDSPixelFormat_RS
{
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDSHeader_RS
{
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat_RS ddspf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

struct DDSHeaderDX10_RS
{
    DXGI_FORMAT dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};
#pragma pack(pop)

static uint32_t MakeFourCC_RS(char a, char b, char c, char d)
{
    return ((uint32_t)(uint8_t)a) |
        ((uint32_t)(uint8_t)b << 8) |
        ((uint32_t)(uint8_t)c << 16) |
        ((uint32_t)(uint8_t)d << 24);
}

static bool IsBlockCompressed_RS(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

static UINT BitsPerPixel_RS(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 128;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        return 64;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
        return 32;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
        return 16;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
        return 8;
    default:
        return 0;
    }
}

static void GetSurfaceInfo_RS(size_t width, size_t height, DXGI_FORMAT fmt,
    size_t* outNumBytes, size_t* outRowBytes, size_t* outNumRows)
{
    size_t numBytes = 0;
    size_t rowBytes = 0;
    size_t numRows = 0;

    if (IsBlockCompressed_RS(fmt))
    {
        size_t bytesPerBlock = (fmt == DXGI_FORMAT_BC1_TYPELESS || fmt == DXGI_FORMAT_BC1_UNORM ||
            fmt == DXGI_FORMAT_BC1_UNORM_SRGB || fmt == DXGI_FORMAT_BC4_TYPELESS ||
            fmt == DXGI_FORMAT_BC4_UNORM || fmt == DXGI_FORMAT_BC4_SNORM) ? 8 : 16;

        size_t numBlocksWide = (std::max<size_t>)(1, (width + 3) / 4);
        size_t numBlocksHigh = (std::max<size_t>)(1, (height + 3) / 4);
        rowBytes = numBlocksWide * bytesPerBlock;
        numRows = numBlocksHigh;
        numBytes = rowBytes * numBlocksHigh;
    }
    else
    {
        UINT bpp = BitsPerPixel_RS(fmt);
        if (bpp == 0)
            throw std::runtime_error("Unsupported DDS DXGI format");

        rowBytes = (width * bpp + 7) / 8;
        numRows = height;
        numBytes = rowBytes * height;
    }

    if (outNumBytes) *outNumBytes = numBytes;
    if (outRowBytes) *outRowBytes = rowBytes;
    if (outNumRows) *outNumRows = numRows;
}

static void AppendBox_RS(
    ObjMeshData& mesh,
    const DirectX::XMFLOAT3& center,
    const DirectX::XMFLOAT3& size,
    const DirectX::XMFLOAT2& uvScale = DirectX::XMFLOAT2(1.0f, 1.0f))
{
    using namespace DirectX;

    const float hx = size.x * 0.5f;
    const float hy = size.y * 0.5f;
    const float hz = size.z * 0.5f;

    const XMFLOAT3 p000(center.x - hx, center.y - hy, center.z - hz);
    const XMFLOAT3 p001(center.x - hx, center.y - hy, center.z + hz);
    const XMFLOAT3 p010(center.x - hx, center.y + hy, center.z - hz);
    const XMFLOAT3 p011(center.x - hx, center.y + hy, center.z + hz);
    const XMFLOAT3 p100(center.x + hx, center.y - hy, center.z - hz);
    const XMFLOAT3 p101(center.x + hx, center.y - hy, center.z + hz);
    const XMFLOAT3 p110(center.x + hx, center.y + hy, center.z - hz);
    const XMFLOAT3 p111(center.x + hx, center.y + hy, center.z + hz);

    auto addFace = [&](
        const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT3& d,
        const XMFLOAT3& normal, const XMFLOAT3& tangent, const XMFLOAT3& bitangent)
        {
            uint32_t start = (uint32_t)mesh.Vertices.size();
            mesh.Vertices.push_back({ a, normal, tangent, bitangent, XMFLOAT2(0.0f,       uvScale.y) });
            mesh.Vertices.push_back({ b, normal, tangent, bitangent, XMFLOAT2(0.0f,       0.0f) });
            mesh.Vertices.push_back({ c, normal, tangent, bitangent, XMFLOAT2(uvScale.x,  0.0f) });
            mesh.Vertices.push_back({ d, normal, tangent, bitangent, XMFLOAT2(uvScale.x,  uvScale.y) });

            mesh.Indices.push_back(start + 0);
            mesh.Indices.push_back(start + 1);
            mesh.Indices.push_back(start + 2);
            mesh.Indices.push_back(start + 0);
            mesh.Indices.push_back(start + 2);
            mesh.Indices.push_back(start + 3);
        };

    addFace(p001, p011, p111, p101, XMFLOAT3(0, 0, 1), XMFLOAT3(1, 0, 0), XMFLOAT3(0, 1, 0));
    addFace(p100, p110, p010, p000, XMFLOAT3(0, 0, -1), XMFLOAT3(-1, 0, 0), XMFLOAT3(0, 1, 0));
    addFace(p000, p010, p011, p001, XMFLOAT3(-1, 0, 0), XMFLOAT3(0, 0, 1), XMFLOAT3(0, 1, 0));
    addFace(p101, p111, p110, p100, XMFLOAT3(1, 0, 0), XMFLOAT3(0, 0, -1), XMFLOAT3(0, 1, 0));
    addFace(p010, p110, p111, p011, XMFLOAT3(0, 1, 0), XMFLOAT3(1, 0, 0), XMFLOAT3(0, 0, 1));
    addFace(p000, p001, p101, p100, XMFLOAT3(0, -1, 0), XMFLOAT3(1, 0, 0), XMFLOAT3(0, 0, -1));
}

static void BuildShadowTestMesh_RS(ObjMeshData& mesh)
{
    mesh.Vertices.clear();
    mesh.Indices.clear();
    mesh.Submeshes.clear();
    mesh.Materials.clear();
    mesh.MtlLibFile.clear();

    AppendBox_RS(mesh, XMFLOAT3(0.0f, -0.15f, 8.0f), XMFLOAT3(36.0f, 0.3f, 44.0f), XMFLOAT2(12.0f, 12.0f));

    AppendBox_RS(mesh, XMFLOAT3(-4.0f, 1.0f, 4.0f), XMFLOAT3(2.0f, 2.0f, 2.0f));
    AppendBox_RS(mesh, XMFLOAT3(1.0f, 1.5f, 7.0f), XMFLOAT3(1.5f, 3.0f, 1.5f));
    AppendBox_RS(mesh, XMFLOAT3(5.0f, 0.75f, 12.0f), XMFLOAT3(4.0f, 1.5f, 1.5f));
    AppendBox_RS(mesh, XMFLOAT3(-6.0f, 3.0f, 16.0f), XMFLOAT3(1.4f, 6.0f, 1.4f));
    AppendBox_RS(mesh, XMFLOAT3(0.0f, 0.25f, 19.0f), XMFLOAT3(7.0f, 0.5f, 2.0f));

    ObjSubmesh sm;
    sm.MaterialName = "shadow_test_white";
    sm.StartIndex = 0;
    sm.IndexCount = (uint32_t)mesh.Indices.size();
    mesh.Submeshes.push_back(sm);

    ObjMaterialInfo mat;
    mesh.Materials[sm.MaterialName] = mat;
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
    LoadIblResources();

    mGBuffer.Initialize(
        mDevice,
        mWidth,
        mHeight,
        mRtvDescriptorSize,
        mCbvSrvUavDescriptorSize);

    BuildDescriptorHeaps();
    BuildShadowResources();
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


   
    mShadowTestScene.ObjPath = exeDir + L"Models\\Cerberus.obj";
    mShadowTestScene.AssetDir = GetDirPart_RS(mShadowTestScene.ObjPath);
    mShadowTestScene.UseTessellation = false;
    mShadowTestScene.TessMin = 1.0f;
    mShadowTestScene.TessMax = 1.0f;
    mShadowTestScene.TessMaxDistance = 8.0f;
    mShadowTestScene.DisplacementScale = 0.0f;
    mShadowTestScene.NormalMapFlipY = 0.0f;

    XMStoreFloat4x4(&mShadowTestScene.World,
        
        XMMatrixScaling(6.5f, 6.5f, 6.5f) *
        XMMatrixRotationY(0.0f) *
        XMMatrixTranslation(0.0f, 1.2f, 7.0f));

    if (!ObjLoader::LoadObjPosNormalTex(mShadowTestScene.ObjPath, mShadowTestScene.CpuMesh, false))
    {
        MessageBoxW(nullptr, mShadowTestScene.ObjPath.c_str(), L"CERBERUS OBJ LOAD FAILED: check path, OBJ format, triangulation, normals, UVs", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Cerberus load failed");
    }

    mShadowTestScene.DrawSubmeshes = mShadowTestScene.CpuMesh.Submeshes;

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
    buildGpuBuffers(mShadowTestScene);

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

                if (it == scene.CpuMesh.Materials.end() && scene.CpuMesh.Materials.size() == 1)
                {
                  
                    it = scene.CpuMesh.Materials.begin();
                }

                if (it != scene.CpuMesh.Materials.end())
                    mat = it->second;

                std::wstring diff = tryResolveTexture(scene, mat.DiffuseMap);
                std::wstring norm = tryResolveTexture(scene, mat.NormalMap);
                std::wstring disp = tryResolveTexture(scene, mat.DisplacementMap);
                std::wstring rough = tryResolveTexture(scene, mat.RoughnessMap);
                std::wstring metal = tryResolveTexture(scene, mat.MetallicMap);

                
                if (scene.ObjPath.find(L"Cerberus.obj") != std::wstring::npos ||
                    scene.ObjPath.find(L"cerberus.obj") != std::wstring::npos)
                {
                    std::wstring modelsDir = GetExeDir_RS() + L"Models\\";
                    diff = modelsDir + L"Cerberus_A.jpg";
                    norm = modelsDir + L"Cerberus_N.jpg";
                    rough = modelsDir + L"Cerberus_R.jpg";
                    metal = modelsDir + L"Cerberus_M.jpg";
                    disp = L"";

                    std::wstring missing;
                    if (!FileExists_RS(diff))  missing += L"Missing: " + diff + L"\n";
                    if (!FileExists_RS(norm))  missing += L"Missing: " + norm + L"\n";
                    if (!FileExists_RS(rough)) missing += L"Missing: " + rough + L"\n";
                    if (!FileExists_RS(metal)) missing += L"Missing: " + metal + L"\n";

                    if (!missing.empty())
                    {
                        MessageBoxW(nullptr, missing.c_str(),
                            L"CERBERUS TEXTURE FILES NOT FOUND",
                            MB_OK | MB_ICONERROR);
                    }
                }

                UINT base = (UINT)mTextures.size();

                addTex(diff, 0xFFFFFFFFu);   // t0: white albedo
                addTex(norm, 0xFF8080FFu);   // t1: flat normal
                addTex(disp, 0xFF000000u);   // t2: black height
                addTex(rough, 0xFF808080u);  // t3: roughness = 0.5
                addTex(metal, 0xFF000000u);  // t4: metallic = 0.0

                scene.SubmeshBaseSrv.push_back(base);
            }
        };


    buildSceneMaterialSrvs(mSponzaScene);
    buildSceneMaterialSrvs(mTessScene);
    buildSceneMaterialSrvs(mShadowTestScene);

    {
        mOptimizationScene.SubmeshBaseSrv.clear();
        mOptimizationScene.SubmeshBaseSrv.reserve(mOptimizationScene.DrawSubmeshes.size());

        std::wstring diff = mOptimizationScene.AssetDir + L"Diffuse.jpg";
        std::wstring norm = mOptimizationScene.AssetDir + L"Normal.jpg";

        for (size_t i = 0; i < mOptimizationScene.DrawSubmeshes.size(); ++i)
        {
            UINT base = (UINT)mTextures.size();

            addTex(diff, 0xFFFFFFFFu);   // t0: diffuse / albedo
            addTex(norm, 0xFF8080FFu);   // t1: normal
            addTex(L"", 0xFF000000u);    // t2: displacement / height
            addTex(L"", 0xFF808080u);    // t3: roughness fallback = 0.5
            addTex(L"", 0xFF000000u);    // t4: metallic fallback = 0.0

            mOptimizationScene.SubmeshBaseSrv.push_back(base);
        }
    }

    mModelTextureCount = (UINT)mTextures.size();
    mGBufferSrvStartIndex = mModelTextureCount;


    std::wstring maskPath = GetExeDir_RS() + L"Models\\check.png";
    LoadTexture_WIC(maskPath, mShadowMaskTexture, mShadowMaskUpload);
}
void RenderingSystem::BuildDescriptorHeaps()
{
    const UINT extra = 10;

    mShadowSrvIndex = mGBufferSrvStartIndex + 3;
    mShadowMaskSrvIndex = mShadowSrvIndex + 1;
    mIrradianceSrvIndex = mShadowMaskSrvIndex + 1;
    mPrefilterSrvIndex = mIrradianceSrvIndex + 1;
    mBrdfLutSrvIndex = mPrefilterSrvIndex + 1;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = mModelTextureCount + 3 + 1 + 1 + 3 + extra;
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

    if (mIrradianceMap)
        CreateTextureCubeSrv(mIrradianceSrvIndex, mIrradianceMap.Get());
    if (mPrefilterMap)
        CreateTextureCubeSrv(mPrefilterSrvIndex, mPrefilterMap.Get());
    if (mBrdfLut)
        CreateTextureSrv(mBrdfLutSrvIndex, mBrdfLut.Get());
}

void RenderingSystem::BuildShadowResources()
{
    mShadowViewport = { 0.0f, 0.0f, (float)mShadowMapSize, (float)mShadowMapSize, 0.0f, 1.0f };
    mShadowScissor = { 0, 0, (LONG)mShadowMapSize, (LONG)mShadowMapSize };

    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R24G8_TYPELESS,
        mShadowMapSize,
        mShadowMapSize,
        ShadowCascadeCount,
        1,
        1,
        0,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&mShadowMap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = ShadowCascadeCount;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));

    UINT dsvSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (UINT i = 0; i < ShadowCascadeCount; ++i)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;

        CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
        dsv.Offset((INT)i, dsvSize);
        mDevice->CreateDepthStencilView(mShadowMap.Get(), &dsvDesc, dsv);
    }

    CreateShadowTextureArraySrv(mShadowSrvIndex);

    if (mShadowMaskTexture)
        CreateTextureSrv(mShadowMaskSrvIndex, mShadowMaskTexture.Get());
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

    UINT64 shadowCbCount = (UINT64)ShadowCascadeCount * (UINT64)(mOptObjects.empty() ? 1 : mOptObjects.size());
    D3D12_RESOURCE_DESC shadowGeomCbDesc = CD3DX12_RESOURCE_DESC::Buffer((UINT64)mGeometryCBByteSize * shadowCbCount);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &shadowGeomCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mShadowGeometryCB)));

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
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0);
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
        CD3DX12_ROOT_PARAMETER rootParams[1];
        rootParams[0].InitAsConstantBufferView(0);

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
            _countof(rootParams), rootParams,
            0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serializedRootSig;
        ComPtr<ID3DBlob> errorBlob;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSig, &errorBlob));
        ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mShadowRootSig)));
    }

    {
        CD3DX12_ROOT_PARAMETER rootParams[2];
        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);
        rootParams[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParams[1].InitAsConstantBufferView(0);

        CD3DX12_STATIC_SAMPLER_DESC staticSamps[2] =
        {
            CD3DX12_STATIC_SAMPLER_DESC(
                0,
                D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
            CD3DX12_STATIC_SAMPLER_DESC(
                1,
                D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                0.0f,
                16,
                D3D12_COMPARISON_FUNC_LESS_EQUAL,
                D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE)
        };

        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
            _countof(rootParams), rootParams,
            _countof(staticSamps), staticSamps,
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
    ComPtr<ID3DBlob> gVs, gPs, tessVs, gHs, gDs, lVs, lPs, shadowVs, errors;
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
    compile(L"Shaders/PostProcessVS.hlsl", "VSMain", "vs_5_0", lVs);
    compile(L"Shaders/PostProcessPS.hlsl", "PSMain", "ps_5_0", lPs);
    compile(L"Shaders/ShadowMapVS.hlsl", "VSMain", "vs_5_0", shadowVs);

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
        psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
        psoDesc.pRootSignature = mShadowRootSig.Get();
        psoDesc.VS = { shadowVs->GetBufferPointer(), shadowVs->GetBufferSize() };
        psoDesc.PS.pShaderBytecode = nullptr;
        psoDesc.PS.BytecodeLength = 0;

        CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
        // For Sponza this greatly reduces self-shadowing/acne on dense geometry.
        rast.CullMode = D3D12_CULL_MODE_FRONT;
        rast.DepthBias = 2000;
        rast.SlopeScaledDepthBias = 2.0f;
        rast.DepthBiasClamp = 0.0f;
        psoDesc.RasterizerState = rast;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 0;

        // Depth-only shadow pass: no color render targets.
        // Explicitly clear RTV formats to keep CreateGraphicsPipelineState happy.
        for (UINT i = 0; i < 8; ++i)
        {
            psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
        }

        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mShadowPSO)));
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


void RenderingSystem::LoadTexture_DDS(const std::wstring& filePath,
    ComPtr<ID3D12Resource>& tex,
    ComPtr<ID3D12Resource>& upload)
{
    std::ifstream fin(filePath, std::ios::binary | std::ios::ate);
    if (!fin)
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Cannot open DDS", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Cannot open DDS");
    }

    std::streamsize fileSize = fin.tellg();
    fin.seekg(0, std::ios::beg);

    std::vector<uint8_t> fileData((size_t)fileSize);
    if (!fin.read(reinterpret_cast<char*>(fileData.data()), fileSize))
        throw std::runtime_error("Failed to read DDS");

    if (fileData.size() < sizeof(uint32_t) + sizeof(DDSHeader_RS))
        throw std::runtime_error("Invalid DDS size");

    const uint8_t* ptr = fileData.data();
    uint32_t magic = *reinterpret_cast<const uint32_t*>(ptr);
    ptr += sizeof(uint32_t);

    if (magic != MakeFourCC_RS('D', 'D', 'S', ' '))
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Invalid DDS magic", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Invalid DDS magic");
    }

    const DDSHeader_RS* header = reinterpret_cast<const DDSHeader_RS*>(ptr);
    ptr += sizeof(DDSHeader_RS);

    if (header->size != 124 || header->ddspf.size != 32)
        throw std::runtime_error("Invalid DDS header");

    if (header->ddspf.fourCC != MakeFourCC_RS('D', 'X', '1', '0'))
    {
        MessageBoxW(nullptr, filePath.c_str(), L"DDS without DX10 header is not supported by this simple loader", MB_OK | MB_ICONERROR);
        throw std::runtime_error("DDS without DX10 header is not supported");
    }

    if ((size_t)(ptr - fileData.data()) + sizeof(DDSHeaderDX10_RS) > fileData.size())
        throw std::runtime_error("Missing DDS DX10 header");

    const DDSHeaderDX10_RS* dx10 = reinterpret_cast<const DDSHeaderDX10_RS*>(ptr);
    ptr += sizeof(DDSHeaderDX10_RS);

    const DXGI_FORMAT format = dx10->dxgiFormat;
    const UINT width = header->width;
    const UINT height = header->height;
    const UINT mipCount = (std::max)(1u, header->mipMapCount);

    UINT arraySize = (std::max)(1u, dx10->arraySize);
    const bool isCube = (dx10->miscFlag & 0x4u) != 0;
    if (isCube && arraySize == 1)
        arraySize = 6;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = (UINT16)arraySize;
    texDesc.MipLevels = (UINT16)mipCount;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&tex)));

    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    subresources.reserve((size_t)arraySize * mipCount);

    const uint8_t* dataBegin = ptr;
    const uint8_t* dataEnd = fileData.data() + fileData.size();

    for (UINT item = 0; item < arraySize; ++item)
    {
        UINT w = width;
        UINT h = height;

        for (UINT mip = 0; mip < mipCount; ++mip)
        {
            size_t numBytes = 0;
            size_t rowBytes = 0;
            size_t numRows = 0;
            GetSurfaceInfo_RS(w, h, format, &numBytes, &rowBytes, &numRows);

            if (dataBegin + numBytes > dataEnd)
            {
                MessageBoxW(nullptr, filePath.c_str(), L"DDS data ended unexpectedly", MB_OK | MB_ICONERROR);
                throw std::runtime_error("DDS data ended unexpectedly");
            }

            D3D12_SUBRESOURCE_DATA sub = {};
            sub.pData = dataBegin;
            sub.RowPitch = (LONG_PTR)rowBytes;
            sub.SlicePitch = (LONG_PTR)numBytes;
            subresources.push_back(sub);

            dataBegin += numBytes;
            w = (std::max)(1u, w >> 1);
            h = (std::max)(1u, h >> 1);
        }
    }

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
        tex.Get(), 0, (UINT)subresources.size());

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload)));

    UpdateSubresources(
        mInitCmdList,
        tex.Get(),
        upload.Get(),
        0,
        0,
        (UINT)subresources.size(),
        subresources.data());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mInitCmdList->ResourceBarrier(1, &barrier);
}

void RenderingSystem::LoadIblResources()
{
    const std::wstring iblDir = GetExeDir_RS() + L"Models\\IBL\\";

    const std::wstring irradiancePath = iblDir + L"IrradianceMap_BC6U.dds";
    const std::wstring prefilterPath = iblDir + L"PreFilteredEnvMap_BC6U.dds";
    const std::wstring brdfPath = iblDir + L"IntegrationMap.dds";

    std::wstring missing;
    if (!FileExists_RS(irradiancePath)) missing += L"Missing: " + irradiancePath + L"\n";
    if (!FileExists_RS(prefilterPath))  missing += L"Missing: " + prefilterPath + L"\n";
    if (!FileExists_RS(brdfPath))       missing += L"Missing: " + brdfPath + L"\n";

    if (!missing.empty())
    {
        MessageBoxW(nullptr, missing.c_str(), L"IBL DDS FILES NOT FOUND", MB_OK | MB_ICONERROR);
        throw std::runtime_error("IBL DDS files not found");
    }

    LoadTexture_DDS(irradiancePath, mIrradianceMap, mIrradianceUpload);
    LoadTexture_DDS(prefilterPath, mPrefilterMap, mPrefilterUpload);
    LoadTexture_DDS(brdfPath, mBrdfLut, mBrdfLutUpload);
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



void RenderingSystem::CreateTextureCubeSrv(UINT srvIndex, ID3D12Resource* tex)
{
    D3D12_RESOURCE_DESC desc = tex->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = desc.MipLevels;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    hCpu.Offset((INT)srvIndex, mCbvSrvUavDescriptorSize);
    mDevice->CreateShaderResourceView(tex, &srvDesc, hCpu);
}

void RenderingSystem::CreateShadowTextureArraySrv(UINT srvIndex)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = ShadowCascadeCount;
    srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpu(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    hCpu.Offset((INT)srvIndex, mCbvSrvUavDescriptorSize);
    mDevice->CreateShaderResourceView(mShadowMap.Get(), &srvDesc, hCpu);
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

XMFLOAT4X4 RenderingSystem::GetViewProjFloat4x4() const
{
    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, GetViewProjMatrix());
    return result;
}

XMFLOAT3 RenderingSystem::GetCameraPosition() const
{
    return mCameraPos;
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

void RenderingSystem::UpdateShadowCascades()
{
    const float cameraNear = 0.1f;

   
    const bool isSponza = (mMode == RenderMode::Sponza);
    const float shadowDistance = isSponza ? 160.0f : 120.0f;
    const float lambda = isSponza ? 0.50f : 0.55f;

    const float aspect = (mHeight > 0) ? ((float)mWidth / (float)mHeight) : 1.0f;
    const float fovY = 0.25f * XM_PI;

    float cascadeEnds[ShadowCascadeCount];

    for (UINT i = 0; i < ShadowCascadeCount; ++i)
    {
        float p = (float)(i + 1) / (float)ShadowCascadeCount;

        float logSplit = cameraNear * powf(shadowDistance / cameraNear, p);
        float uniformSplit = cameraNear + (shadowDistance - cameraNear) * p;

        cascadeEnds[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        mCascadeSplits[i] = cascadeEnds[i];
    }

    XMMATRIX view = GetViewMatrix();
    XMMATRIX invView = XMMatrixInverse(nullptr, view);

    XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&mLightingData.DirLight.Direction));

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabsf(XMVectorGetX(XMVector3Dot(lightDir, up))) > 0.95f)
        up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

    float previousSplit = cameraNear;

    for (UINT cascade = 0; cascade < ShadowCascadeCount; ++cascade)
    {
        float nearZ = previousSplit;
        float farZ = cascadeEnds[cascade];
        previousSplit = farZ;

        float tanHalfFovY = tanf(fovY * 0.5f);
        float tanHalfFovX = tanHalfFovY * aspect;

        float nearY = tanHalfFovY * nearZ;
        float nearX = tanHalfFovX * nearZ;
        float farY = tanHalfFovY * farZ;
        float farX = tanHalfFovX * farZ;

        XMVECTOR cornersView[8] =
        {
            XMVectorSet(-nearX,  nearY, nearZ, 1.0f),
            XMVectorSet(nearX,  nearY, nearZ, 1.0f),
            XMVectorSet(nearX, -nearY, nearZ, 1.0f),
            XMVectorSet(-nearX, -nearY, nearZ, 1.0f),

            XMVectorSet(-farX,  farY, farZ, 1.0f),
            XMVectorSet(farX,  farY, farZ, 1.0f),
            XMVectorSet(farX, -farY, farZ, 1.0f),
            XMVectorSet(-farX, -farY, farZ, 1.0f)
        };

        XMVECTOR cornersWorld[8];
        XMVECTOR center = XMVectorZero();

        for (int i = 0; i < 8; ++i)
        {
            cornersWorld[i] = XMVector3TransformCoord(cornersView[i], invView);
            center += cornersWorld[i];
        }

        center /= 8.0f;

        float radius = 0.0f;

        for (int i = 0; i < 8; ++i)
        {
            XMVECTOR v = cornersWorld[i] - center;
            radius = std::max(radius, XMVectorGetX(XMVector3Length(v)));
        }

       
        radius = ceilf(radius);
        radius += isSponza ? 4.0f : 1.5f;

        XMVECTOR lightPos = center - lightDir * (radius * (isSponza ? 1.2f : 2.0f));
        XMMATRIX lightView = XMMatrixLookAtLH(lightPos, center, up);

        XMVECTOR centerLSv = XMVector3TransformCoord(center, lightView);
        float centerLSX = XMVectorGetX(centerLSv);
        float centerLSY = XMVectorGetY(centerLSv);

        float minX = centerLSX - radius;
        float maxX = centerLSX + radius;
        float minY = centerLSY - radius;
        float maxY = centerLSY + radius;

        float minZ = FLT_MAX;
        float maxZ = -FLT_MAX;

        for (int i = 0; i < 8; ++i)
        {
            XMVECTOR cornerLS = XMVector3TransformCoord(cornersWorld[i], lightView);
            float z = XMVectorGetZ(cornerLS);
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }

        float extraDepth = isSponza ? radius * 2.5f : 250.0f;
        minZ -= extraDepth;
        maxZ += extraDepth;

        float texelSize = (2.0f * radius) / (float)mShadowMapSize;

        minX = floorf(minX / texelSize) * texelSize;
        maxX = floorf(maxX / texelSize) * texelSize;
        minY = floorf(minY / texelSize) * texelSize;
        maxY = floorf(maxY / texelSize) * texelSize;

        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
            minX,
            maxX,
            minY,
            maxY,
            minZ,
            maxZ
        );

        XMMATRIX lightViewProj = lightView * lightProj;

        XMStoreFloat4x4(
            &mShadowViewProj[cascade],
            XMMatrixTranspose(lightViewProj)
        );

        mLightingData.ShadowViewProj[cascade] = mShadowViewProj[cascade];
    }

    mLightingData.CascadeSplits = XMFLOAT4(
        mCascadeSplits[0],
        mCascadeSplits[1],
        mCascadeSplits[2],
        mCascadeSplits[3]
    );

    mLightingData.ShadowMapSize = XMFLOAT2(
        (float)mShadowMapSize,
        (float)mShadowMapSize
    );
}

void RenderingSystem::UpdateShadowGeometryCB(UINT cascadeIndex, UINT objectIndex, CXMMATRIX world, CXMMATRIX lightViewProj)
{
    GeometryConstants data = {};

    XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
    XMStoreFloat4x4(&data.ViewProj, XMMatrixTranspose(lightViewProj));

    data.Tiling = XMFLOAT2(1.0f, 1.0f);
    data.UVOffset = XMFLOAT2(0.0f, 0.0f);
    data.EyePosW = mCameraPos;

    data.TessMin = 1.0f;
    data.TessMax = 1.0f;
    data.TessMaxDistance = 8.0f;
    data.DisplacementScale = 0.0f;
    data.NormalMapFlipY = 0.0f;

    UINT objectCount = (UINT)(mOptObjects.empty() ? 1 : mOptObjects.size());
    UINT cbIndex = cascadeIndex * objectCount + objectIndex;

    BYTE* mapped = nullptr;
    ThrowIfFailed(mShadowGeometryCB->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
    memcpy(mapped + (size_t)cbIndex * mGeometryCBByteSize, &data, sizeof(GeometryConstants));
    mShadowGeometryCB->Unmap(0, nullptr);
}

void RenderingSystem::UpdateLightCB(float totalTime)
{
    mLightingData.EyePosW = mCameraPos;
    mLightingData.AmbientColor = { 0.22f, 0.22f, 0.24f };


    XMVECTOR sunDir = XMVector3Normalize(XMVectorSet(-0.05f, -1.0f, 0.03f, 0.0f));
    XMStoreFloat3(&mLightingData.DirLight.Direction, sunDir);

    mLightingData.DirLight.Color = { 1.0f, 1.0f, 1.0f };
    mLightingData.DirLight.Intensity = 1.35f;

    mLightingData.PointLights[0].Range = 0.0f;
    mLightingData.PointLights[0].Intensity = 0.0f;
    mLightingData.PointLights[1].Range = 0.0f;
    mLightingData.PointLights[1].Intensity = 0.0f;
    mLightingData.SpotLight.Range = 0.0f;
    mLightingData.SpotLight.Intensity = 0.0f;

    XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        cosf(mPitch) * sinf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * cosf(mYaw),
        0.0f
    ));

    XMStoreFloat4(&mLightingData.CameraForward, forward);

    UpdateShadowCascades();

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

    case RenderMode::ShadowTest:
        
        mCameraPos = { 0.0f, 1.4f, -9.0f };
        mYaw = 0.0f;
        mPitch = 0.0f;
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

    if (input.WasKeyPressed('4'))
    {
        mMode = RenderMode::ShadowTest;
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
    else if (mMode == RenderMode::ShadowTest)
        UpdateGeometryCB(mShadowTestScene);

    UpdateLightCB(totalTime);

    mStatsPrintTimer += deltaTime;
    if (mStatsPrintTimer >= 1.0f)
    {
        mStatsPrintTimer = 0.0f;

        std::ostringstream oss;
        oss << "[DX12 OPT] mode=";
        if (mMode == RenderMode::Sponza) oss << "Sponza";
        else if (mMode == RenderMode::Tessellation) oss << "Tessellation";
        else if (mMode == RenderMode::Optimization) oss << "Optimization";
        else oss << "ShadowTest";

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


void RenderingSystem::DrawSceneIntoShadowMap(
    ID3D12GraphicsCommandList* cmdList,
    const SceneMesh& scene,
    CXMMATRIX lightViewProj,
    UINT cascadeIndex)
{
    XMMATRIX world = XMLoadFloat4x4(&scene.World);


    if (mMode == RenderMode::Tessellation)
    {
        world =
            XMMatrixRotationX(mObjectPitch) *
            XMMatrixRotationY(mObjectYaw) *
            world;
    }

    UpdateShadowGeometryCB(cascadeIndex, 0, world, lightViewProj);

    cmdList->SetPipelineState(mShadowPSO.Get());
    cmdList->SetGraphicsRootSignature(mShadowRootSig.Get());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &scene.VBV);
    cmdList->IASetIndexBuffer(&scene.IBV);

    UINT objectCount = (UINT)(mOptObjects.empty() ? 1 : mOptObjects.size());
    UINT cbIndex = cascadeIndex * objectCount;

    cmdList->SetGraphicsRootConstantBufferView(
        0,
        mShadowGeometryCB->GetGPUVirtualAddress() + (UINT64)cbIndex * mGeometryCBByteSize);

    for (const ObjSubmesh& sm : scene.DrawSubmeshes)
    {
        cmdList->DrawIndexedInstanced(
            sm.IndexCount,
            1,
            sm.StartIndex,
            0,
            0);
    }
}

void RenderingSystem::DrawOptimizationIntoShadowMap(
    ID3D12GraphicsCommandList* cmdList,
    CXMMATRIX lightViewProj,
    UINT cascadeIndex)
{
    cmdList->SetPipelineState(mShadowPSO.Get());
    cmdList->SetGraphicsRootSignature(mShadowRootSig.Get());

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mOptimizationScene.VBV);
    cmdList->IASetIndexBuffer(&mOptimizationScene.IBV);

    UINT objectCount = (UINT)(mOptObjects.empty() ? 1 : mOptObjects.size());

    for (UINT objectIndex = 0; objectIndex < (UINT)mOptObjects.size(); ++objectIndex)
    {
        const SceneObject& obj = mOptObjects[objectIndex];
        XMMATRIX world = XMLoadFloat4x4(&obj.World);

        UpdateShadowGeometryCB(cascadeIndex, objectIndex, world, lightViewProj);

        UINT cbIndex = cascadeIndex * objectCount + objectIndex;

        cmdList->SetGraphicsRootConstantBufferView(
            0,
            mShadowGeometryCB->GetGPUVirtualAddress() + (UINT64)cbIndex * mGeometryCBByteSize);

        for (const ObjSubmesh& sm : mOptimizationScene.DrawSubmeshes)
        {
            cmdList->DrawIndexedInstanced(
                sm.IndexCount,
                1,
                sm.StartIndex,
                0,
                0);
        }
    }
}

void RenderingSystem::DrawShadowPass(ID3D12GraphicsCommandList* cmdList)
{
    auto toDepthWrite = CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMap.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmdList->ResourceBarrier(1, &toDepthWrite);

    cmdList->RSSetViewports(1, &mShadowViewport);
    cmdList->RSSetScissorRects(1, &mShadowScissor);

    cmdList->SetPipelineState(mShadowPSO.Get());
    cmdList->SetGraphicsRootSignature(mShadowRootSig.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT dsvSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    for (UINT cascade = 0; cascade < ShadowCascadeCount; ++cascade)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
        dsv.Offset((INT)cascade, dsvSize);

        cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

        XMMATRIX lightViewProj = XMMatrixTranspose(XMLoadFloat4x4(&mShadowViewProj[cascade]));

        switch (mMode)
        {
        case RenderMode::Sponza:
            DrawSceneIntoShadowMap(cmdList, mSponzaScene, lightViewProj, cascade);
            break;

        case RenderMode::Tessellation:
            DrawSceneIntoShadowMap(cmdList, mTessScene, lightViewProj, cascade);
            break;

        case RenderMode::Optimization:
            DrawOptimizationIntoShadowMap(cmdList, lightViewProj, cascade);
            break;

        case RenderMode::ShadowTest:
            DrawSceneIntoShadowMap(cmdList, mShadowTestScene, lightViewProj, cascade);
            break;
        }
    }

    auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMap.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toShader);
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
    cmdList->DrawInstanced(6, 1, 0, 0);
}

void RenderingSystem::Draw(
    ID3D12GraphicsCommandList* cmdList,
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv)
{
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);


    DrawShadowPass(cmdList);


    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)mWidth, (float)mHeight, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, (LONG)mWidth, (LONG)mHeight };
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);


    switch (mMode)
    {
    case RenderMode::Sponza:
        UpdateGeometryCB(mSponzaScene);
        DrawSceneGeometryPass(cmdList, mSponzaScene, depthDsv);
        break;

    case RenderMode::Tessellation:
        UpdateGeometryCB(mTessScene);
        DrawSceneGeometryPass(cmdList, mTessScene, depthDsv);
        break;

    case RenderMode::Optimization:
        DrawOptimizationGeometryPass(cmdList, depthDsv);
        break;

    case RenderMode::ShadowTest:
        UpdateGeometryCB(mShadowTestScene);
        DrawSceneGeometryPass(cmdList, mShadowTestScene, depthDsv);
        break;
    }


    DrawLightingPass(cmdList, backBufferRtv);
}