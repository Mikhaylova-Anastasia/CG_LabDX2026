cbuffer ObjectCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;

    float3 gEyePosW;
    float pad0;

    float4 gLightDir;
    float4 gDiffuseColor;
    float4 gSpecularColor;
    float gShininess;
    float pad1;
    float2 pad2;

    float2 gTiling;
    float2 gUVOffset;
};

Texture2D gDiffuseMap : register(t0);
SamplerState gSam : register(s0);

struct PSInput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-8f)
        return float3(0.0f, 0.0f, 1.0f);
    return v * rsqrt(len2);
}

float4 PSMain(PSInput pin) : SV_Target
{
    float3 N = SafeNormalize(pin.NormalW);

    // --- NEW: tiled + animated UV ---
    float2 uv = pin.Tex * gTiling + gUVOffset;

    float3 albedo = gDiffuseMap.Sample(gSam, uv).rgb;

    float3 L = SafeNormalize(-gLightDir.xyz);
    float3 V = SafeNormalize(gEyePosW - pin.PosW);
    float3 H = SafeNormalize(L + V);

    float NdotL = saturate(dot(N, L));
    float3 diffuse = albedo * NdotL;

    float3 specular = 0.0f;
    if (NdotL > 0.0f)
    {
        float specFactor = pow(saturate(dot(N, H)), gShininess);
        specular = gSpecularColor.rgb * specFactor;
    }

    float3 ambient = 0.20f * albedo;

    float3 color = ambient + diffuse + specular;
    return float4(color, 1.0f);
}
