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

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BINORMAL;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput vout;

    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosH = mul(posW, gViewProj);

    return vout;
}