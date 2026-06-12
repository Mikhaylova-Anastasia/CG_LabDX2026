#include "ObjLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

using namespace DirectX;

namespace
{
    static std::string TrimA(const std::string& s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    static std::string ToLowerA(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static std::wstring GetDirPart(const std::wstring& path)
    {
        size_t p = path.find_last_of(L"\\/");
        if (p == std::wstring::npos) return L"";
        return path.substr(0, p + 1);
    }

    static std::wstring WidenAscii(const std::string& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (char c : s)
            out.push_back((wchar_t)(unsigned char)c);
        return out;
    }

    static int FixIndex(int idx, int count)
    {
        if (idx > 0) return idx - 1;
        if (idx < 0) return count + idx;
        return -1;
    }

    struct VertexKey
    {
        int v = 0;
        int vt = 0;
        int vn = 0;

        bool operator==(const VertexKey& other) const
        {
            return v == other.v && vt == other.vt && vn == other.vn;
        }
    };

    struct VertexKeyHash
    {
        size_t operator()(const VertexKey& k) const noexcept
        {
            size_t h1 = std::hash<int>{}(k.v);
            size_t h2 = std::hash<int>{}(k.vt);
            size_t h3 = std::hash<int>{}(k.vn);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    static bool ParseFaceVertex(const std::string& token, int& v, int& vt, int& vn)
    {
        v = vt = vn = 0;

        size_t s1 = token.find('/');
        if (s1 == std::string::npos)
        {
            v = std::stoi(token);
            return true;
        }

        size_t s2 = token.find('/', s1 + 1);

        std::string a = token.substr(0, s1);
        std::string b;
        std::string c;

        if (s2 == std::string::npos)
        {
            b = token.substr(s1 + 1);
        }
        else
        {
            b = token.substr(s1 + 1, s2 - s1 - 1);
            c = token.substr(s2 + 1);
        }

        if (!a.empty()) v = std::stoi(a);
        if (!b.empty()) vt = std::stoi(b);
        if (!c.empty()) vn = std::stoi(c);
        return true;
    }

    static void ParseMtlFile(const std::wstring& mtlPath, std::unordered_map<std::string, ObjMaterialInfo>& outMaterials)
    {
        std::ifstream fin(mtlPath);
        if (!fin.is_open())
            return;

        std::string line;
        std::string current;

        auto readLastToken = [](std::stringstream& ss) -> std::string
            {
                std::string tok;
                std::string last;
                while (ss >> tok)
                    last = tok;
                return last;
            };

        while (std::getline(fin, line))
        {
            line = TrimA(line);
            if (line.empty() || line[0] == '#')
                continue;

            std::stringstream ss(line);
            std::string tag;
            ss >> tag;
            tag = ToLowerA(tag);

            if (tag == "newmtl")
            {
                std::string name;
                std::getline(ss, name);
                current = ToLowerA(TrimA(name));
                outMaterials[current] = ObjMaterialInfo{};
            }
            else if (!current.empty())
            {
                std::string file = readLastToken(ss);
                if (file.empty())
                    continue;

                if (tag == "map_kd")
                    outMaterials[current].DiffuseMap = file;
                else if (tag == "map_bump" || tag == "bump" || tag == "norm")
                    outMaterials[current].NormalMap = file;
                else if (tag == "disp" || tag == "map_disp")
                    outMaterials[current].DisplacementMap = file;
                else if (tag == "map_pr" || tag == "map_roughness" || tag == "roughness")
                    outMaterials[current].RoughnessMap = file;
                else if (tag == "map_pm" || tag == "map_metallic" || tag == "map_metalness" || tag == "metallic" || tag == "metalness")
                    outMaterials[current].MetallicMap = file;
            }
        }
    }

    static void ComputeTangents(ObjMeshData& mesh)
    {
        for (auto& v : mesh.Vertices)
        {
            v.Tangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
            v.Bitangent = XMFLOAT3(0.0f, 0.0f, 0.0f);
        }

        for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
        {
            auto& v0 = mesh.Vertices[mesh.Indices[i + 0]];
            auto& v1 = mesh.Vertices[mesh.Indices[i + 1]];
            auto& v2 = mesh.Vertices[mesh.Indices[i + 2]];

            XMVECTOR p0 = XMLoadFloat3(&v0.Pos);
            XMVECTOR p1 = XMLoadFloat3(&v1.Pos);
            XMVECTOR p2 = XMLoadFloat3(&v2.Pos);

            XMVECTOR e1 = p1 - p0;
            XMVECTOR e2 = p2 - p0;

            float du1 = v1.TexC.x - v0.TexC.x;
            float dv1 = v1.TexC.y - v0.TexC.y;
            float du2 = v2.TexC.x - v0.TexC.x;
            float dv2 = v2.TexC.y - v0.TexC.y;

            float det = du1 * dv2 - dv1 * du2;
            if (fabsf(det) < 1e-8f)
                continue;

            float invDet = 1.0f / det;
            XMVECTOR tangent = (e1 * dv2 - e2 * dv1) * invDet;
            XMVECTOR bitangent = (e2 * du1 - e1 * du2) * invDet;

            XMFLOAT3 t, b;
            XMStoreFloat3(&t, tangent);
            XMStoreFloat3(&b, bitangent);

            auto add = [](XMFLOAT3& a, const XMFLOAT3& b)
                {
                    a.x += b.x; a.y += b.y; a.z += b.z;
                };

            add(v0.Tangent, t); add(v1.Tangent, t); add(v2.Tangent, t);
            add(v0.Bitangent, b); add(v1.Bitangent, b); add(v2.Bitangent, b);
        }

        for (auto& v : mesh.Vertices)
        {
            XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&v.Normal));
            XMVECTOR t = XMLoadFloat3(&v.Tangent);
            XMVECTOR b = XMLoadFloat3(&v.Bitangent);

            t = XMVector3Normalize(t - n * XMVectorGetX(XMVector3Dot(n, t)));
            b = XMVector3Normalize(b - n * XMVectorGetX(XMVector3Dot(n, b)));

            XMFLOAT3 tn, bn;
            XMStoreFloat3(&tn, t);
            XMStoreFloat3(&bn, b);
            v.Tangent = tn;
            v.Bitangent = bn;
        }
    }
}

bool ObjLoader::LoadObjPosNormalTex(const std::wstring& filename, ObjMeshData& out, bool /*convertToLH*/)
{
    out.Vertices.clear();
    out.Indices.clear();
    out.Submeshes.clear();
    out.Materials.clear();
    out.MtlLibFile.clear();

    std::ifstream fin(filename);
    if (!fin.is_open())
        return false;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVerts;

    std::string line;
    std::string currentMaterial = "__default__";

    ObjSubmesh currentSubmesh;
    currentSubmesh.MaterialName = currentMaterial;
    currentSubmesh.StartIndex = 0;
    currentSubmesh.IndexCount = 0;

    auto flushSubmesh = [&]()
        {
            if (currentSubmesh.IndexCount > 0)
            {
                out.Submeshes.push_back(currentSubmesh);
                currentSubmesh.StartIndex = (uint32_t)out.Indices.size();
                currentSubmesh.IndexCount = 0;
            }
        };

    while (std::getline(fin, line))
    {
        line = TrimA(line);
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream iss(line);
        std::string key;
        iss >> key;

        if (key == "mtllib")
        {
            std::string rest;
            std::getline(iss, rest);
            rest = TrimA(rest);
            if (!rest.empty() && rest.front() == '"' && rest.back() == '"' && rest.size() >= 2)
                rest = rest.substr(1, rest.size() - 2);
            out.MtlLibFile = rest;
        }
        else if (key == "usemtl")
        {
            std::string rest;
            std::getline(iss, rest);
            rest = TrimA(rest);
            if (rest.empty()) rest = "__default__";

            flushSubmesh();
            currentMaterial = rest;
            currentSubmesh.MaterialName = currentMaterial;
        }
        else if (key == "v")
        {
            float x, y, z;
            iss >> x >> y >> z;
            positions.emplace_back(x, y, z);
        }
        else if (key == "vt")
        {
            float u = 0.0f, v = 0.0f;
            iss >> u >> v;
            texcoords.emplace_back(u, 1.0f - v);
        }
        else if (key == "vn")
        {
            float x, y, z;
            iss >> x >> y >> z;
            normals.emplace_back(x, y, z);
        }
        else if (key == "f")
        {
            std::vector<std::string> faceTokens;
            std::string tok;
            while (iss >> tok)
                faceTokens.push_back(tok);

            if (faceTokens.size() < 3)
                continue;

            std::vector<uint32_t> faceIndices;
            faceIndices.reserve(faceTokens.size());

            for (const std::string& ft : faceTokens)
            {
                int iv = 0, ivt = 0, ivn = 0;
                if (!ParseFaceVertex(ft, iv, ivt, ivn))
                    continue;

                int posIndex = FixIndex(iv, (int)positions.size());
                int texIndex = FixIndex(ivt, (int)texcoords.size());
                int nrmIndex = FixIndex(ivn, (int)normals.size());

                VertexKey keyv{ posIndex, texIndex, nrmIndex };
                auto it = uniqueVerts.find(keyv);
                if (it != uniqueVerts.end())
                {
                    faceIndices.push_back(it->second);
                }
                else
                {
                    VertexPosNormalTangentTex vert{};
                    vert.Pos = (posIndex >= 0 && posIndex < (int)positions.size()) ? positions[posIndex] : XMFLOAT3(0, 0, 0);
                    vert.Normal = (nrmIndex >= 0 && nrmIndex < (int)normals.size()) ? normals[nrmIndex] : XMFLOAT3(0, 1, 0);
                    vert.TexC = (texIndex >= 0 && texIndex < (int)texcoords.size()) ? texcoords[texIndex] : XMFLOAT2(0, 0);
                    vert.Tangent = XMFLOAT3(0, 0, 0);
                    vert.Bitangent = XMFLOAT3(0, 0, 0);

                    uint32_t newIndex = (uint32_t)out.Vertices.size();
                    out.Vertices.push_back(vert);
                    uniqueVerts[keyv] = newIndex;
                    faceIndices.push_back(newIndex);
                }
            }

            for (size_t i = 1; i + 1 < faceIndices.size(); ++i)
            {
                out.Indices.push_back(faceIndices[0]);
                out.Indices.push_back(faceIndices[i]);
                out.Indices.push_back(faceIndices[i + 1]);
                currentSubmesh.IndexCount += 3;
            }
        }
    }

    flushSubmesh();

    if (out.Submeshes.empty())
    {
        ObjSubmesh sm;
        sm.MaterialName = "__default__";
        sm.StartIndex = 0;
        sm.IndexCount = (uint32_t)out.Indices.size();
        out.Submeshes.push_back(sm);
    }

    if (!out.MtlLibFile.empty())
    {
        std::wstring mtlPath = GetDirPart(filename) + WidenAscii(out.MtlLibFile);
        ParseMtlFile(mtlPath, out.Materials);
    }

    ComputeTangents(out);
    return !out.Vertices.empty() && !out.Indices.empty();
}
