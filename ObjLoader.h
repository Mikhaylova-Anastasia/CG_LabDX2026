// ObjLoader.h
#pragma once
#include "Common.h"
#include <string>


struct VertexPosNormalTex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
};


struct ObjSubmesh
{
    std::string MaterialName;
    uint32_t    StartIndex = 0;
    uint32_t    IndexCount = 0;
};

struct ObjMeshData
{
    std::vector<VertexPosNormalTex> Vertices;
    std::vector<uint32_t>           Indices;


    std::string                     MtlLibFile;
    std::vector<ObjSubmesh>         Submeshes;
};

class ObjLoader
{
public:


    static bool LoadObjPosNormalTex(const std::wstring& filename, ObjMeshData& out, bool convertToLH = true);
};
