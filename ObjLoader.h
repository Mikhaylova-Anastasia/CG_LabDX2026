#pragma once
#include "Common.h"
#include <string>
#include <unordered_map>

struct VertexPosNormalTangentTex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT3 Tangent;
    DirectX::XMFLOAT3 Bitangent;
    DirectX::XMFLOAT2 TexC;
};

struct ObjSubmesh
{
    std::string MaterialName;
    uint32_t    StartIndex = 0;
    uint32_t    IndexCount = 0;
};

struct ObjMaterialInfo
{
    std::string DiffuseMap;
    std::string NormalMap;
    std::string DisplacementMap;
    std::string RoughnessMap;
    std::string MetallicMap;
};

struct ObjMeshData
{
    std::vector<VertexPosNormalTangentTex> Vertices;
    std::vector<uint32_t>                  Indices;

    std::string                            MtlLibFile;
    std::vector<ObjSubmesh>                Submeshes;
    std::unordered_map<std::string, ObjMaterialInfo> Materials;
};

class ObjLoader
{
public:
    static bool LoadObjPosNormalTex(const std::wstring& filename, ObjMeshData& out, bool convertToLH = true);
};
