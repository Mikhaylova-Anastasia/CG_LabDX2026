Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
SamplerState gSam : register(s0);

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

struct DSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float3 TangentW : TEXCOORD2;
    float3 BitangentW : TEXCOORD3;
    float2 Tex : TEXCOORD4;
};

[domain("tri")]
DSOutput DSMain(
    HSConstData input,
    const OutputPatch<HSInput, 3> patch,
    float3 bary : SV_DomainLocation)
{
    DSOutput o;

    float3 posL =
        patch[0].PosL * bary.x +
        patch[1].PosL * bary.y +
        patch[2].PosL * bary.z;

    float3 nL = normalize(
        patch[0].NormalL * bary.x +
        patch[1].NormalL * bary.y +
        patch[2].NormalL * bary.z);

    float3 tL = normalize(
        patch[0].TangentL * bary.x +
        patch[1].TangentL * bary.y +
        patch[2].TangentL * bary.z);

    float3 bL = normalize(
        patch[0].BitangentL * bary.x +
        patch[1].BitangentL * bary.y +
        patch[2].BitangentL * bary.z);

    float2 tex =
        (patch[0].Tex * bary.x +
         patch[1].Tex * bary.y +
         patch[2].Tex * bary.z) * gTiling + gUVOffset;

    float h = gDisplacementMap.SampleLevel(gSam, tex, 0).r;

    
    float3 basePosW = mul(float4(posL, 1.0f), gWorld).xyz;
    float distToEye = distance(basePosW, gEyePosW);


    float nearFactor = 1.0f - saturate((distToEye - 1.0f) / 6.0f);
    nearFactor = nearFactor * nearFactor;

   
    float skullMask = saturate((h - 0.30f) / 0.20f);
    skullMask = skullMask * skullMask;

    
    float dispScale = lerp(gDisplacementScale * 0.35f, gDisplacementScale * 3.5f, nearFactor);

    
    posL += nL * (skullMask * dispScale);

    float4 posW = mul(float4(posL, 1.0f), gWorld);

    o.PosW = posW.xyz;
    o.PosH = mul(posW, gViewProj);
    o.NormalW = normalize(mul(float4(nL, 0.0f), gWorld).xyz);
    o.TangentW = normalize(mul(float4(tL, 0.0f), gWorld).xyz);
    o.BitangentW = normalize(mul(float4(bL, 0.0f), gWorld).xyz);
    o.Tex = tex;

    return o;
}