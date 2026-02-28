// CubeRenderer.cpp
#include "CubeRenderer.h"
#include <fstream>
#include <sstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static std::wstring GetExeDir()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    return std::wstring(exePath);
}

static std::string TrimA(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}




static std::string ToLowerA(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static std::wstring WidenAscii(const std::string& s)
{
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w.push_back((wchar_t)c);
    return w;
}

CubeRenderer::CubeRenderer(ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    UINT cbvSrvUavDescriptorSize)
    : mDevice(device)
    , mCmdList(cmdList)
    , mCbvSrvUavDescriptorSize(cbvSrvUavDescriptorSize)
{
    float aspect = 1280.0f / 720.0f;
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 0.1f, 5000.0f);
    XMStoreFloat4x4(&mProj, P);

    mCameraPos = XMFLOAT3(0.0f, 2.0f, -5.0f);
}

std::unordered_map<std::string, std::string> CubeRenderer::ParseMtlDiffuseMaps(const std::wstring& mtlPath)
{
    std::unordered_map<std::string, std::string> out;


    std::string path(mtlPath.begin(), mtlPath.end());
    std::ifstream fin(path);
    if (!fin.is_open())
        return out;

    std::string line;
    std::string current;

    while (std::getline(fin, line))
    {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string tag;
        ss >> tag;

        if (ToLowerA(tag) == "newmtl")
        {
            std::string name;
            std::getline(ss, name);
            current = ToLowerA(TrimA(name));
        }
        else if (ToLowerA(tag) == "map_kd" && !current.empty())
        {

            std::string rest;
            std::getline(ss, rest);
            rest = TrimA(rest);

            std::stringstream ss2(rest);
            std::string tok, lastTok;
            while (ss2 >> tok) lastTok = tok;

            if (!lastTok.empty())
                out[current] = lastTok;
        }
    }

    return out;
}

void CubeRenderer::BuildResources()
{

    BuildMeshGeometry();


    BuildConstantBuffer();


    std::wstring exeDir = GetExeDir();
    std::wstring modelsDir = exeDir + L"Models\\";
    std::wstring objName = L"Capsule.obj";
    std::wstring objPath = modelsDir + objName;

    ObjMeshData tmp;

    ObjLoader::LoadObjPosNormalTex(objPath, tmp, false);

    std::wstring mtlPath = modelsDir + WidenAscii(tmp.MtlLibFile.empty() ? "Capsule.mtl" : tmp.MtlLibFile);
    auto matToTex = ParseMtlDiffuseMaps(mtlPath);


    std::vector<std::wstring> texturePaths;
    std::unordered_map<std::string, UINT> texIndexByFile;

    auto addTexture = [&](const std::string& file) -> UINT
        {
            auto it = texIndexByFile.find(file);
            if (it != texIndexByFile.end())
                return it->second;

            UINT idx = (UINT)texturePaths.size();
            texturePaths.push_back(modelsDir + WidenAscii(file));
            texIndexByFile[file] = idx;
            return idx;
        };


    const std::string Fallback = "check.png"; 

    mSubmeshSrvIndex.clear();
    mSubmeshSrvIndex.reserve(mDrawSubmeshes.size());

    for (const auto& sm : mDrawSubmeshes)
    {
        auto it = matToTex.find(ToLowerA(sm.MaterialName));
        if (it != matToTex.end())
        {
            UINT idx = addTexture(it->second);
            mSubmeshSrvIndex.push_back(idx);
        }
        else
        {
            UINT idx = addTexture(Fallback);
            mSubmeshSrvIndex.push_back(idx);
        }
    }

    if (texturePaths.empty())
    {

        addTexture(Fallback);
    }



    mAnimatedSrvIndex = addTexture("check.png");


    BuildSrvDescriptorHeap((UINT)texturePaths.size());


    mTextures.clear();
    mTextureUploads.clear();
    mTextures.resize(texturePaths.size());
    mTextureUploads.resize(texturePaths.size());

    for (UINT i = 0; i < (UINT)texturePaths.size(); ++i)
    {
        LoadTexture_WIC(texturePaths[i], mTextures[i], mTextureUploads[i]);
        CreateTextureSrv(i, mTextures[i].Get());
    }


    BuildRootSignature();
    BuildPSO();
}

void CubeRenderer::BuildMeshGeometry()
{
    ObjMeshData mesh;

    std::wstring exeDir = GetExeDir();
    std::wstring objPath = exeDir + L"Models\\Capsule.obj";

    if (!ObjLoader::LoadObjPosNormalTex(objPath, mesh, false))
    {
        MessageBoxW(nullptr, objPath.c_str(), L"OBJ NOT FOUND AT:", MB_OK | MB_ICONERROR);
        throw std::runtime_error("OBJ load failed");
    }

    mDrawSubmeshes = mesh.Submeshes;
    if (mDrawSubmeshes.empty())
    {
        // If OBJ had no usemtl, draw as one submesh
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

    CD3DX12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vBufferSize);
    CD3DX12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(iBufferSize);

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

    mCmdList->CopyBufferRegion(mVertexBuffer.Get(), 0, mVBUpload.Get(), 0, vBufferSize);
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mVertexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        mCmdList->ResourceBarrier(1, &barrier);
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

    mCmdList->CopyBufferRegion(mIndexBuffer.Get(), 0, mIBUpload.Get(), 0, iBufferSize);
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mIndexBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_INDEX_BUFFER);
        mCmdList->ResourceBarrier(1, &barrier);
    }

    mVBV.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVBV.StrideInBytes = sizeof(VertexPosNormalTex);
    mVBV.SizeInBytes = vBufferSize;

    mIBV.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    mIBV.Format = DXGI_FORMAT_R32_UINT;
    mIBV.SizeInBytes = iBufferSize;
}

void CubeRenderer::BuildConstantBuffer()
{
    UINT cbSize = (sizeof(ObjectConstants) + 255) & ~255;

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mConstantUploadBuffer)));

    mConstantBuffer = mConstantUploadBuffer;
}

void CubeRenderer::BuildSrvDescriptorHeap(UINT numDescriptors)
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = numDescriptors;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));
}

void CubeRenderer::LoadTexture_WIC(const std::wstring& filePath, ComPtr<ID3D12Resource>& tex, ComPtr<ID3D12Resource>& upload)
{
    ThrowIfFailed(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(
        filePath.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);

    if (FAILED(hr))
    {
        MessageBoxW(nullptr, filePath.c_str(), L"Cannot load texture (WIC):", MB_OK | MB_ICONERROR);
        ThrowIfFailed(hr);
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame));

    UINT width = 0, height = 0;
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

    CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
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
    CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

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

    UpdateSubresources(mCmdList, tex.Get(), upload.Get(), 0, 0, 1, &subData);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCmdList->ResourceBarrier(1, &barrier);
}

void CubeRenderer::CreateTextureSrv(UINT srvIndex, ID3D12Resource* tex)
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

void CubeRenderer::BuildRootSignature()
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
        IID_PPV_ARGS(&mRootSignature)));
}

void CubeRenderer::BuildPSO()
{
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompileFromFile(L"Shaders/CubeVS.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &vs, &errors);

    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "VS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    hr = D3DCompileFromFile(L"Shaders/CubePS.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &ps, &errors);

    if (FAILED(hr))
    {
        if (errors) MessageBoxA(nullptr, (char*)errors->GetBufferPointer(), "PS compile error", MB_OK);
        ThrowIfFailed(hr);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

    CD3DX12_RASTERIZER_DESC rast(D3D12_DEFAULT);
    rast.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState = rast;

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void CubeRenderer::UpdateCubeRotation(const InputDevice& input, float)
{
    if (input.IsMouseDown(0))
    {
        const float rotSpeed = 0.01f;
        POINT md = input.MouseDelta();
        mCubeYaw += md.x * rotSpeed;
        mCubePitch += md.y * rotSpeed;

        const float limit = XM_PIDIV2 - 0.01f;
        if (mCubePitch > limit)  mCubePitch = limit;
        if (mCubePitch < -limit) mCubePitch = -limit;
    }
}

void CubeRenderer::UpdateCamera(const InputDevice& input, float dt)
{
    const float moveSpeed = 5.0f;
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

void CubeRenderer::Update(float totalTime, float deltaTime, const InputDevice& input)
{
    mTotalTime = totalTime;

    bool k1 = input.IsKeyDown('1');
    bool k2 = input.IsKeyDown('2');
    if (k1 && !mPrevKey1) mMode = DemoMode::Capsule;
    if (k2 && !mPrevKey2) mMode = DemoMode::Animated;
    mPrevKey1 = k1;
    mPrevKey2 = k2;

    UpdateCubeRotation(input, deltaTime);
    UpdateCamera(input, deltaTime);

    XMMATRIX world =
        XMMatrixRotationX(mCubePitch) *
        XMMatrixRotationY(mCubeYaw);

    XMVECTOR pos = XMLoadFloat3(&mCameraPos);
    XMVECTOR forward = XMVectorSet(
        cosf(mPitch) * sinf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * cosf(mYaw),
        0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, pos + forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX proj = XMLoadFloat4x4(&mProj);
    XMMATRIX wvp = world * view * proj;

    XMStoreFloat4x4(&mConstants.WorldViewProj, XMMatrixTranspose(wvp));
    XMStoreFloat4x4(&mConstants.World, XMMatrixTranspose(world));

    mConstants.EyePosW = mCameraPos;

    mConstants.LightDir = XMFLOAT4(0.5f, -1.0f, -0.3f, 0.0f);
    mConstants.DiffuseColor = XMFLOAT4(1, 1, 1, 1);
    mConstants.SpecularColor = XMFLOAT4(1, 1, 1, 1);
    mConstants.Shininess = 16.0f;

    float camDist = sqrtf(
        mCameraPos.x * mCameraPos.x +
        mCameraPos.y * mCameraPos.y +
        mCameraPos.z * mCameraPos.z
    );

    const float nearDist = 2.0f;
    const float farDist = 20.0f;
    const float minScale = 1.0f;
    const float maxScale = 6.0f;

    float t = (camDist - nearDist) / (farDist - nearDist);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float scale = minScale + (maxScale - minScale) * t;
    mConstants.Tiling = XMFLOAT2(scale, scale);

    if (mMode == DemoMode::Animated)
    {
        float ox = fmodf(totalTime * 0.20f, 1.0f);
        float oy = fmodf(totalTime * 0.10f, 1.0f);
        mConstants.UVOffset = XMFLOAT2(ox, oy);
    }
    else
    {
        mConstants.UVOffset = XMFLOAT2(0.0f, 0.0f);
    }

    void* mapped = nullptr;
    ThrowIfFailed(mConstantUploadBuffer->Map(0, nullptr, &mapped));
    memcpy(mapped, &mConstants, sizeof(ObjectConstants));
    mConstantUploadBuffer->Unmap(0, nullptr);
}

void CubeRenderer::Draw(ID3D12GraphicsCommandList* cmdList)
{
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);

    cmdList->SetGraphicsRootConstantBufferView(0, mConstantUploadBuffer->GetGPUVirtualAddress());

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpu(mSrvHeap->GetGPUDescriptorHandleForHeapStart());


    for (size_t i = 0; i < mDrawSubmeshes.size(); ++i)
    {
        UINT srvIdx = 0;

        if (mMode == DemoMode::Animated)
        {
            srvIdx = mAnimatedSrvIndex;
        }
        else
        {
            srvIdx = (i < mSubmeshSrvIndex.size()) ? mSubmeshSrvIndex[i] : 0;
        }

        CD3DX12_GPU_DESCRIPTOR_HANDLE handle = hGpu;
        handle.Offset((INT)srvIdx, mCbvSrvUavDescriptorSize);

        cmdList->SetGraphicsRootDescriptorTable(1, handle);

        const ObjSubmesh& sm = mDrawSubmeshes[i];
        cmdList->DrawIndexedInstanced(sm.IndexCount, 1, sm.StartIndex, 0, 0);
    }
}