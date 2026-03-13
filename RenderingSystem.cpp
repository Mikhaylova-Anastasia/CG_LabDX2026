#include "RenderingSystem.h"
#include <fstream>
#include <sstream>
#include <algorithm>

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

static std::string TrimA_RS(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string ToLowerA_RS(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = (char)tolower((unsigned char)s[i]);
    return s;
}

static std::wstring WidenAscii_RS(const std::string& s)
{
    std::wstring w;
    w.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        w.push_back((wchar_t)(unsigned char)s[i]);
    return w;
}

static bool EndsWithNoCase_RS(const std::wstring& s, const std::wstring& suffix)
{
    if (s.size() < suffix.size())
        return false;

    std::wstring a = s.substr(s.size() - suffix.size());
    std::wstring b = suffix;

    std::transform(a.begin(), a.end(), a.begin(), towlower);
    std::transform(b.begin(), b.end(), b.begin(), towlower);

    return a == b;
}

static std::wstring GetDirPart_RS(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos)
        return L"";
    return path.substr(0, p + 1);
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

    if (!fin)
        throw std::runtime_error("Failed to read TGA header");

    if (hdr.imageType != 2)
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Unsupported TGA type", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Unsupported TGA type");
    }

    if (hdr.pixelDepth != 24 && hdr.pixelDepth != 32)
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Unsupported TGA pixel depth", MB_OK | MB_ICONERROR);
        throw std::runtime_error("Unsupported TGA bpp");
    }

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
        throw std::runtime_error("Failed to read TGA pixels");

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

std::unordered_map<std::string, std::string> RenderingSystem::ParseMtlDiffuseMaps(const std::wstring& mtlPath)
{
    std::unordered_map<std::string, std::string> out;

    std::string pathA(mtlPath.begin(), mtlPath.end());
    std::ifstream fin(pathA.c_str(), std::ios::in);

    if (!fin.is_open())
    {
        MessageBoxW(nullptr, mtlPath.c_str(), L"Cannot open MTL", MB_OK | MB_ICONERROR);
        return out;
    }

    std::string line;
    std::string current;

    while (std::getline(fin, line))
    {
        line = TrimA_RS(line);
        if (line.empty() || line[0] == '#')
            continue;

        std::stringstream ss(line);
        std::string tag;
        ss >> tag;
        tag = ToLowerA_RS(tag);

        if (tag == "newmtl")
        {
            std::string name;
            std::getline(ss, name);
            current = ToLowerA_RS(TrimA_RS(name));
        }
        else if (tag == "map_kd" && !current.empty())
        {
            std::string rest;
            std::getline(ss, rest);
            rest = TrimA_RS(rest);

            std::stringstream ss2(rest);
            std::string tok;
            std::string lastTok;
            while (ss2 >> tok)
                lastTok = tok;

            if (!lastTok.empty())
                out[current] = lastTok;
        }
    }

    return out;
}

void RenderingSystem::BuildResources()
{
    BuildMeshGeometry();
    BuildTextureResources();

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

void RenderingSystem::BuildMeshGeometry()
{
    ObjMeshData mesh;

    std::wstring exeDir = GetExeDir_RS();
    std::wstring objPath = exeDir + L"Models\\sponza.obj";

    if (!ObjLoader::LoadObjPosNormalTex(objPath, mesh, false))
    {
        MessageBoxW(nullptr, objPath.c_str(), L"OBJ NOT FOUND", MB_OK | MB_ICONERROR);
        throw std::runtime_error("OBJ load failed");
    }

    mDrawSubmeshes = mesh.Submeshes;
    if (mDrawSubmeshes.empty())
    {
        ObjSubmesh sm;
        sm.MaterialName = "__default__";
        sm.StartIndex = 0;
        sm.IndexCount = (uint32_t)mesh.Indices.size();
        mDrawSubmeshes.push_back(sm);
    }

    mIndexCount = (UINT)mesh.Indices.size();

    const UINT vBufferSize = (UINT)(mesh.Vertices.size() * sizeof(VertexPosNormalTex));
    const UINT iBufferSize = (UINT)(mesh.Indices.size() * sizeof(uint32_t));

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vBufferSize);
    D3D12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(iBufferSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mVertexBuffer)));

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mVBUpload)));

    void* mapped = nullptr;
    ThrowIfFailed(mVBUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, mesh.Vertices.data(), vBufferSize);
    mVBUpload->Unmap(0, nullptr);

    mInitCmdList->CopyBufferRegion(mVertexBuffer.Get(), 0, mVBUpload.Get(), 0, vBufferSize);
    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mVertexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        mInitCmdList->ResourceBarrier(1, &barrier);
    }

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&mIndexBuffer)));

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mIBUpload)));

    ThrowIfFailed(mIBUpload->Map(0, nullptr, &mapped));
    memcpy(mapped, mesh.Indices.data(), iBufferSize);
    mIBUpload->Unmap(0, nullptr);

    mInitCmdList->CopyBufferRegion(mIndexBuffer.Get(), 0, mIBUpload.Get(), 0, iBufferSize);
    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mIndexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_INDEX_BUFFER);
        mInitCmdList->ResourceBarrier(1, &barrier);
    }

    mVBV.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVBV.StrideInBytes = sizeof(VertexPosNormalTex);
    mVBV.SizeInBytes = vBufferSize;

    mIBV.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    mIBV.Format = DXGI_FORMAT_R32_UINT;
    mIBV.SizeInBytes = iBufferSize;
}

void RenderingSystem::BuildTextureResources()
{
    std::wstring exeDir = GetExeDir_RS();
    std::wstring modelsDir = exeDir + L"Models\\";
    std::wstring objPath = modelsDir + L"sponza.obj";

    ObjMeshData tmp;
    if (!ObjLoader::LoadObjPosNormalTex(objPath, tmp, false))
    {
        MessageBoxW(nullptr, objPath.c_str(), L"OBJ NOT FOUND FOR TEXTURES", MB_OK | MB_ICONERROR);
        throw std::runtime_error("OBJ load failed in BuildTextureResources");
    }

    std::wstring mtlPath = modelsDir + L"sponza.mtl";
    auto matToTex = ParseMtlDiffuseMaps(mtlPath);
    std::wstring mtlDir = GetDirPart_RS(mtlPath);

    std::vector<std::wstring> texturePaths;
    std::unordered_map<std::string, UINT> texIndexByFile;

    auto addTexture = [&](const std::string& file) -> UINT
        {
            std::string key = ToLowerA_RS(file);

            std::unordered_map<std::string, UINT>::iterator it = texIndexByFile.find(key);
            if (it != texIndexByFile.end())
                return it->second;

            std::wstring rel = WidenAscii_RS(file);
            for (size_t i = 0; i < rel.size(); ++i)
            {
                if (rel[i] == L'/')
                    rel[i] = L'\\';
            }

            std::wstring fullPath;

            if (rel.size() > 1 && rel[1] == L':')
            {
                fullPath = rel;
            }
            else
            {
                fullPath = mtlDir + rel;
            }

            if (!FileExists_RS(fullPath))
            {
                size_t slashPos = rel.find_last_of(L"\\/");
                std::wstring filenameOnly = (slashPos == std::wstring::npos) ? rel : rel.substr(slashPos + 1);

                std::wstring try1 = modelsDir + rel;
                std::wstring try2 = modelsDir + filenameOnly;
                std::wstring try3 = modelsDir + L"textures\\" + filenameOnly;
                std::wstring try4 = mtlDir + filenameOnly;

                if (FileExists_RS(try1))
                    fullPath = try1;
                else if (FileExists_RS(try2))
                    fullPath = try2;
                else if (FileExists_RS(try3))
                    fullPath = try3;
                else if (FileExists_RS(try4))
                    fullPath = try4;
                else
                    fullPath = modelsDir + L"check.png";
            }

            UINT idx = (UINT)texturePaths.size();
            texturePaths.push_back(fullPath);
            texIndexByFile[key] = idx;
            return idx;
        };

    const std::string fallbackTex = "check.png";

    mSubmeshSrvIndex.clear();
    mSubmeshSrvIndex.reserve(mDrawSubmeshes.size());

    for (size_t i = 0; i < mDrawSubmeshes.size(); ++i)
    {
        const ObjSubmesh& sm = mDrawSubmeshes[i];
        std::unordered_map<std::string, std::string>::iterator it = matToTex.find(ToLowerA_RS(sm.MaterialName));

        if (it != matToTex.end())
        {
            UINT idx = addTexture(it->second);
            mSubmeshSrvIndex.push_back(idx);
        }
        else
        {
            UINT idx = addTexture(fallbackTex);
            mSubmeshSrvIndex.push_back(idx);
        }
    }

    if (texturePaths.empty())
    {
        std::wstring fallback = modelsDir + L"check.png";
        texturePaths.push_back(fallback);
    }

    mTextures.clear();
    mTextureUploads.clear();
    mTextures.resize(texturePaths.size());
    mTextureUploads.resize(texturePaths.size());

    for (UINT i = 0; i < (UINT)texturePaths.size(); ++i)
    {
        LoadTexture_WIC(texturePaths[i], mTextures[i], mTextureUploads[i]);
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
    UINT geomSize = AlignCB_RS(sizeof(GeometryConstants));
    UINT lightSize = AlignCB_RS(sizeof(LightConstants));

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC geomCbDesc = CD3DX12_RESOURCE_DESC::Buffer(geomSize);
    D3D12_RESOURCE_DESC lightCbDesc = CD3DX12_RESOURCE_DESC::Buffer(lightSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &geomCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mGeometryCB)));

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
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        rootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

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

        ThrowIfFailed(D3D12SerializeRootSignature(
            &rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSig,
            &errorBlob));

        ThrowIfFailed(mDevice->CreateRootSignature(
            0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&mGeometryRootSig)));
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

        ThrowIfFailed(D3D12SerializeRootSignature(
            &rootSigDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSig,
            &errorBlob));

        ThrowIfFailed(mDevice->CreateRootSignature(
            0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            IID_PPV_ARGS(&mLightingRootSig)));
    }
}

void RenderingSystem::BuildPSOs()
{
    ComPtr<ID3DBlob> gVs;
    ComPtr<ID3DBlob> gPs;
    ComPtr<ID3DBlob> lVs;
    ComPtr<ID3DBlob> lPs;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = S_OK;

    hr = D3DCompileFromFile(L"Shaders/GBufferVS.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &gVs, &errors);
    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "GBufferVS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(L"Shaders/GBufferPS.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &gPs, &errors);
    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "GBufferPS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(L"Shaders/DeferredLightVS.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &lVs, &errors);
    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "DeferredLightVS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(L"Shaders/DeferredLightPS.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &lPs, &errors);
    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "DeferredLightPS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mLightingPSO)));
    }
}

void RenderingSystem::LoadTexture_WIC(const std::wstring& filePath, ComPtr<ID3D12Resource>& tex, ComPtr<ID3D12Resource>& upload)
{
    if (EndsWithNoCase_RS(filePath, L".tga"))
    {
        LoadTexture_TGA_Internal_RS(mDevice, mInitCmdList, filePath, tex, upload);
        return;
    }

    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(
        filePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);

    if (FAILED(hr))
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Cannot load texture (WIC)", MB_OK | MB_ICONERROR);
        ThrowIfFailed(hr);
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame));

    UINT width = 0;
    UINT height = 0;
    ThrowIfFailed(frame->GetSize(&width, &height));

    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter));

    ThrowIfFailed(converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr, 0.0,
        WICBitmapPaletteTypeCustom));

    const UINT bytesPerPixel = 4;
    const UINT rowPitch = width * bytesPerPixel;
    const UINT imageSize = rowPitch * height;

    std::vector<uint8_t> pixels(imageSize);
    ThrowIfFailed(converter->CopyPixels(nullptr, rowPitch, imageSize, pixels.data()));

    D3D12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);

    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&tex)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex.Get(), 0, 1);

    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&upload)));

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = pixels.data();
    subData.RowPitch = rowPitch;
    subData.SlicePitch = imageSize;

    UpdateSubresources(mInitCmdList, tex.Get(), upload.Get(), 0, 0, 1, &subData);

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

void RenderingSystem::UpdateCamera(const InputDevice& input, float dt)
{
    const float moveSpeed = 6.0f;
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

    XMStoreFloat3(&mCameraPos, pos);
}

void RenderingSystem::UpdateObjectRotation(const InputDevice& input)
{
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

void RenderingSystem::UpdateGeometryCB(float /*totalTime*/)
{
    XMMATRIX world =
        XMMatrixScaling(0.1f, 0.1f, 0.1f) *
        XMMatrixRotationX(mObjectPitch) *
        XMMatrixRotationY(mObjectYaw) *
        XMMatrixTranslation(0.0f, -1.0f, 0.0f);

    XMVECTOR pos = XMLoadFloat3(&mCameraPos);
    XMVECTOR forward = XMVectorSet(
        cosf(mPitch) * sinf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * cosf(mYaw),
        0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0, 1, 0, 0));
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX wvp = world * view * proj;

    XMStoreFloat4x4(&mGeometryData.WorldViewProj, XMMatrixTranspose(wvp));
    XMStoreFloat4x4(&mGeometryData.World, XMMatrixTranspose(world));

    mGeometryData.Tiling = XMFLOAT2(1.0f, 1.0f);
    mGeometryData.UVOffset = XMFLOAT2(0.0f, 0.0f);

    void* mapped = nullptr;
    ThrowIfFailed(mGeometryCB->Map(0, nullptr, &mapped));
    memcpy(mapped, &mGeometryData, sizeof(GeometryConstants));
    mGeometryCB->Unmap(0, nullptr);
}

void RenderingSystem::UpdateLightCB(float totalTime)
{
    mLightingData.EyePosW = mCameraPos;
    mLightingData.AmbientColor = XMFLOAT3(0.05f, 0.05f, 0.05f);

    mLightingData.DirLight.Direction = XMFLOAT3(0.3f, -1.0f, -0.2f);
    mLightingData.DirLight.Intensity = 0.35f;
    mLightingData.DirLight.Color = XMFLOAT3(1.0f, 1.0f, 1.0f);

    // Красный point light
    mLightingData.PointLights[0].Position = XMFLOAT3(
        35.0f * cosf(totalTime),
        12.0f,
        35.0f * sinf(totalTime));
    mLightingData.PointLights[0].Range = 80.0f;
    mLightingData.PointLights[0].Color = XMFLOAT3(1.0f, 0.15f, 0.15f);
    mLightingData.PointLights[0].Intensity = 8.0f;

    // Синий point light
    mLightingData.PointLights[1].Position = XMFLOAT3(
        35.0f * cosf(-0.7f * totalTime + 1.5f),
        18.0f,
        35.0f * sinf(-0.7f * totalTime + 1.5f));
    mLightingData.PointLights[1].Range = 80.0f;
    mLightingData.PointLights[1].Color = XMFLOAT3(0.2f, 0.45f, 1.0f);
    mLightingData.PointLights[1].Intensity = 8.0f;

    // Spot light сверху
    mLightingData.SpotLight.Position = XMFLOAT3(0.0f, 35.0f, -10.0f);
    mLightingData.SpotLight.Direction = XMFLOAT3(0.0f, -1.0f, 0.2f);
    mLightingData.SpotLight.Range = 120.0f;
    mLightingData.SpotLight.SpotPower = 20.0f;
    mLightingData.SpotLight.Color = XMFLOAT3(0.9f, 1.0f, 0.7f);
    mLightingData.SpotLight.Intensity = 6.0f;

    void* mapped = nullptr;
    ThrowIfFailed(mLightingCB->Map(0, nullptr, &mapped));
    memcpy(mapped, &mLightingData, sizeof(LightConstants));
    mLightingCB->Unmap(0, nullptr);
}

void RenderingSystem::Update(float totalTime, float deltaTime, const InputDevice& input)
{
    UpdateObjectRotation(input);
    UpdateCamera(input, deltaTime);
    UpdateGeometryCB(totalTime);
    UpdateLightCB(totalTime);
}

void RenderingSystem::DrawGeometryPass(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE depthDsv)
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
    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);
    cmdList->SetGraphicsRootConstantBufferView(0, mGeometryCB->GetGPUVirtualAddress());

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());

    for (size_t i = 0; i < mDrawSubmeshes.size(); ++i)
    {
        UINT srvIdx = (i < mSubmeshSrvIndex.size()) ? mSubmeshSrvIndex[i] : 0;
        CD3DX12_GPU_DESCRIPTOR_HANDLE handle = hGpu;
        handle.Offset((INT)srvIdx, mCbvSrvUavDescriptorSize);

        cmdList->SetGraphicsRootDescriptorTable(1, handle);

        const ObjSubmesh& sm = mDrawSubmeshes[i];
        cmdList->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, 0, 0);
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
    DrawGeometryPass(cmdList, depthDsv);
    DrawLightingPass(cmdList, backBufferRtv);
}