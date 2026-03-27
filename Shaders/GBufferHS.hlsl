cbuffer GeometryCB : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float2 gTiling;
    float2 gUVOffset;
    float3 gEyePosW;
    float gTessMin;
    float gTessMax;
    float gTessMaxDistance;
    float gDisplacementScale;
    float gNormalMapFlipY;
};

struct HSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL : TANGENT;
    float3 BitangentL : BINORMAL;
    float2 Tex : TEXCOORD0;
};

struct HSConstData
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HSConst")]
HSInput HSMain(InputPatch<HSInput, 3> patch, uint i : SV_OutputControlPointID)
{
    return patch[i];
}

HSConstData HSConst(InputPatch<HSInput, 3> patch)
{
    HSConstData o;

    float3 centerL = (patch[0].PosL + patch[1].PosL + patch[2].PosL) / 3.0f;
    float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

    float dist = distance(centerW, gEyePosW);
    float t = saturate(dist / max(gTessMaxDistance, 0.001f));
    float tess = lerp(gTessMax, gTessMin, t);
    tess = max(tess, 1.0f);

    o.Edges[0] = tess;
    o.Edges[1] = tess;
    o.Edges[2] = tess;
    o.Inside = tess;
    return o;
}
