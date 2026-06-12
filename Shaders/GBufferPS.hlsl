Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
Texture2D gRoughnessMap : register(t3);
Texture2D gMetallicMap : register(t4);
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

struct PSInput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float3 TangentW : TEXCOORD2;
    float3 BitangentW : TEXCOORD3;
    float2 Tex : TEXCOORD4;
};

struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 Position : SV_Target2;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-8f)
        return float3(0.0f, 0.0f, 1.0f);
    return v * rsqrt(len2);
}

PSOutput PSMain(PSInput pin)
{
    PSOutput o;

    float3 albedo = gDiffuseMap.Sample(gSam, pin.Tex).rgb;
    float roughness = gRoughnessMap.Sample(gSam, pin.Tex).r;
    float metallic = gMetallicMap.Sample(gSam, pin.Tex).r;

    float3 N = SafeNormalize(pin.NormalW);
    float3 T = SafeNormalize(pin.TangentW);
    float3 B = SafeNormalize(pin.BitangentW);

    float3 normalTS = gNormalMap.Sample(gSam, pin.Tex).xyz * 2.0f - 1.0f;
    if (gNormalMapFlipY > 0.5f)
        normalTS.y = -normalTS.y;

    float3 normalW = SafeNormalize(normalTS.x * T + normalTS.y * B + normalTS.z * N);

  
    o.Albedo = float4(albedo, roughness);
    o.Normal = float4(normalW * 0.5f + 0.5f, metallic);
    o.Position = float4(pin.PosW, 1.0f);
    return o;
}
