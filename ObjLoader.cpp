// ObjLoader.cpp
#include "ObjLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace DirectX;

namespace
{
    struct IdxTriplet
    {
        int v = 0;   
        int vt = 0;  
        int vn = 0; 
        bool operator==(const IdxTriplet& o) const { return v == o.v && vt == o.vt && vn == o.vn; }
    };

    struct IdxHash
    {
        size_t operator()(const IdxTriplet& t) const noexcept
        {
            
            size_t h1 = (size_t)t.v * 73856093u;
            size_t h2 = (size_t)t.vt * 19349663u;
            size_t h3 = (size_t)t.vn * 83492791u;
            return h1 ^ h2 ^ h3;
        }
    };

    static std::string Trim(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    
    static void ParseFaceToken(const std::string& tok, int& v, int& vt, int& vn)
    {
        v = vt = vn = 0;

        size_t p1 = tok.find('/');
        if (p1 == std::string::npos)
        {
            v = std::stoi(tok);
            return;
        }

        size_t p2 = tok.find('/', p1 + 1);

        std::string sV = tok.substr(0, p1);
        if (!sV.empty()) v = std::stoi(sV);

        if (p2 == std::string::npos)
        {
            std::string sVT = tok.substr(p1 + 1);
            if (!sVT.empty()) vt = std::stoi(sVT);
            return;
        }

        std::string sVT = tok.substr(p1 + 1, p2 - (p1 + 1)); 
        std::string sVN = tok.substr(p2 + 1);

        if (!sVT.empty()) vt = std::stoi(sVT);
        if (!sVN.empty()) vn = std::stoi(sVN);
    }

  
    static int FixObjIndex(int idx, int size1Based)
    {
        if (idx > 0) return idx;
        if (idx < 0) return size1Based + idx; 
        return 0;
    }
}

bool ObjLoader::LoadObjPosNormalTex(const std::wstring& filename, ObjMeshData& out, bool /*convertToLH*/)
{
    out.Vertices.clear();
    out.Indices.clear();
    out.Submeshes.clear();
    out.MtlLibFile.clear();

    
    std::string path(filename.begin(), filename.end());
    std::ifstream fin(path);
    if (!fin.is_open())
        return false;

    
    std::vector<XMFLOAT3> positions(1);
    std::vector<XMFLOAT3> normals(1);
    std::vector<XMFLOAT2> texcoords(1);

    std::unordered_map<IdxTriplet, uint32_t, IdxHash> uniqueMap;

    
    std::unordered_map<std::string, std::vector<uint32_t>> matToIndices;
    std::vector<std::string> matOrder;

    std::string currentMat = "__default__";
    matToIndices[currentMat] = {};
    matOrder.push_back(currentMat);

    auto ensureMat = [&](const std::string& m)
        {
            if (matToIndices.find(m) == matToIndices.end())
            {
                matToIndices[m] = {};
                matOrder.push_back(m);
            }
            currentMat = m;
        };

    auto getIndex = [&](const std::string& tok) -> uint32_t
        {
            int v = 0, vt = 0, vn = 0;
            ParseFaceToken(tok, v, vt, vn);

            v = FixObjIndex(v, (int)positions.size());
            vt = FixObjIndex(vt, (int)texcoords.size());
            vn = FixObjIndex(vn, (int)normals.size());

            IdxTriplet key{ v, vt, vn };
            auto it = uniqueMap.find(key);
            if (it != uniqueMap.end())
                return it->second;

            VertexPosNormalTex vert{};

            if (v > 0 && v < (int)positions.size())
                vert.Pos = positions[v];
            else
                vert.Pos = XMFLOAT3(0, 0, 0);

            if (vn > 0 && vn < (int)normals.size())
                vert.Normal = normals[vn];
            else
                vert.Normal = XMFLOAT3(0, 1, 0);

            if (vt > 0 && vt < (int)texcoords.size())
                vert.TexC = texcoords[vt];
            else
                vert.TexC = XMFLOAT2(0, 0);

            uint32_t newIndex = (uint32_t)out.Vertices.size();
            out.Vertices.push_back(vert);
            uniqueMap[key] = newIndex;
            return newIndex;
        };

    std::string line;
    while (std::getline(fin, line))
    {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "mtllib")
        {
            std::string mtl;
            std::getline(ss, mtl);
            out.MtlLibFile = Trim(mtl);
        }
        else if (tag == "usemtl")
        {
            std::string m;
            std::getline(ss, m);
            ensureMat(Trim(m));
        }
        else if (tag == "v")
        {
            float x, y, z;
            ss >> x >> y >> z;
            positions.push_back(XMFLOAT3(x, y, z));
        }
        else if (tag == "vn")
        {
            float x, y, z;
            ss >> x >> y >> z;
            normals.push_back(XMFLOAT3(x, y, z));
        }
        else if (tag == "vt")
        {
            float u, v;
            ss >> u >> v;
           
            texcoords.push_back(XMFLOAT2(u, 1.0f - v));
        }
        else if (tag == "f")
        {
            std::vector<std::string> toks;
            std::string t;
            while (ss >> t) toks.push_back(t);
            if (toks.size() < 3) continue;

            
            uint32_t i0 = getIndex(toks[0]);
            for (size_t i = 1; i + 1 < toks.size(); ++i)
            {
                uint32_t i1 = getIndex(toks[i]);
                uint32_t i2 = getIndex(toks[i + 1]);

                auto& inds = matToIndices[currentMat];
                inds.push_back(i0);
                inds.push_back(i1);
                inds.push_back(i2);
            }
        }
    }

    
    out.Indices.clear();
    out.Submeshes.clear();

    uint32_t cursor = 0;
    for (const auto& matName : matOrder)
    {
        auto it = matToIndices.find(matName);
        if (it == matToIndices.end()) continue;

        const auto& inds = it->second;
        if (inds.empty()) continue;

        ObjSubmesh sm;
        sm.MaterialName = matName;
        sm.StartIndex = cursor;
        sm.IndexCount = (uint32_t)inds.size();

        out.Indices.insert(out.Indices.end(), inds.begin(), inds.end());

        cursor += sm.IndexCount;
        out.Submeshes.push_back(sm);
    }

    return !out.Vertices.empty() && !out.Indices.empty();
}
