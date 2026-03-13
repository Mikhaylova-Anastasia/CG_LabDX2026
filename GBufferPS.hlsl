Texture2D gDiffuseMap : register(t0);
SamplerState gSam : register(s0);

struct PSInput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
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
    float3 N = SafeNormalize(pin.NormalW);

    o.Albedo = float4(albedo, 1.0f);
    o.Normal = float4(N * 0.5f + 0.5f, 1.0f);
    o.Position = float4(pin.PosW, 1.0f);

    return o;
}