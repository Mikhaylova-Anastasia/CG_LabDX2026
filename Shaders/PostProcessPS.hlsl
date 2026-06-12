#define DEBUG_CASCADES 0

Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gPositionTex : register(t2);
Texture2DArray gShadowMap : register(t3);
Texture2D gShadowMaskTex : register(t4);
TextureCube gIrradianceMap : register(t5);
TextureCube gPrefilterMap : register(t6);
Texture2D gBrdfLUT : register(t7);

SamplerState gSam : register(s0);
SamplerComparisonState gShadowSam : register(s1);

static const int CASCADE_COUNT = 4;
static const float PI = 3.14159265359f;

struct DirectionalLight
{
    float3 Direction;
    float Intensity;
    float3 Color;
    float pad0;
};

struct PointLight
{
    float3 Position;
    float Range;
    float3 Color;
    float Intensity;
};

struct SpotLight
{
    float3 Position;
    float Range;
    float3 Direction;
    float SpotPower;
    float3 Color;
    float Intensity;
};

cbuffer LightCB : register(b0)
{
    float3 gEyePosW;
    float pad0;

    DirectionalLight gDirLight;
    PointLight gPointLights[2];
    SpotLight gSpotLight;

    float3 gAmbientColor;
    float pad1;

    float4 gCameraForward;

    float4x4 gShadowViewProj[CASCADE_COUNT];
    float4 gCascadeSplits;
    float2 gShadowMapSize;
    float2 pad2;
};

struct PSIn
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);

    if (len2 < 1e-8f)
        return float3(0.0f, 1.0f, 0.0f);

    return v * rsqrt(len2);
}

float3 ApplyReinhardToneMapping(float3 hdrColor)
{
    return hdrColor / (hdrColor + 1.0f);
}

float3 ApplyGammaCorrection(float3 ldrColor)
{
    return pow(saturate(ldrColor), 1.0f / 2.2f);
}

int SelectCascade(float3 posW)
{
    float viewDepth = dot(posW - gEyePosW, gCameraForward.xyz);

    if (viewDepth < 0.0f)
        return -1;

    if (viewDepth <= gCascadeSplits.x)
        return 0;

    if (viewDepth <= gCascadeSplits.y)
        return 1;

    if (viewDepth <= gCascadeSplits.z)
        return 2;

    if (viewDepth <= gCascadeSplits.w)
        return 3;

    return -1;
}

float CalcShadowFactor(float3 posW, float3 normalW)
{
    int cascade = SelectCascade(posW);

    if (cascade < 0)
        return 1.0f;

    float4 shadowH = mul(float4(posW, 1.0f), gShadowViewProj[cascade]);

    if (abs(shadowH.w) < 0.0001f)
        return 1.0f;

    float3 shadowNdc = shadowH.xyz / shadowH.w;
    float2 uv = shadowNdc.xy * float2(0.5f, -0.5f) + 0.5f;

    if (uv.x < 0.0f || uv.x > 1.0f ||
        uv.y < 0.0f || uv.y > 1.0f)
    {
        return 1.0f;
    }

    float currentDepth = shadowNdc.z;

    if (currentDepth < 0.0f || currentDepth > 1.0f)
        return 1.0f;

    float3 lightDir = SafeNormalize(-gDirLight.Direction);
    float ndotl = saturate(dot(normalW, lightDir));

    float bias = max(0.018f * (1.0f - ndotl), 0.006f);
    float compareDepth = currentDepth - bias;

    float shadowSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            shadowSum += gShadowMap.SampleCmpLevelZero(
                gShadowSam,
                float3(uv, cascade),
                compareDepth,
                int2(x, y)
            );
        }
    }

    return shadowSum / 9.0f;
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.000001f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;

    float denom = NdotV * (1.0f - k) + k;
    return NdotV / max(denom, 0.000001f);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);

    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) *
        pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 CalcPBRDirectionalLight(
    float3 albedo,
    float roughness,
    float metallic,
    float3 N,
    float3 V,
    float3 posW,
    float shadow
)
{
    float3 L = SafeNormalize(-gDirLight.Direction);
    float3 H = SafeNormalize(V + L);

    float3 radiance = gDirLight.Color * gDirLight.Intensity;

    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 numerator = NDF * G * F;
    float denominator =
        4.0f *
        max(dot(N, V), 0.0f) *
        max(dot(N, L), 0.0f) +
        0.0001f;

    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = 1.0f - kS;
    kD *= 1.0f - metallic;

    float NdotL = max(dot(N, L), 0.0f);

    return (kD * albedo / PI + specular) * radiance * NdotL * shadow;
}

float3 GetQuickSkyboxColor(float2 uv)
{
   
    float2 screenUv = uv * 2.0f - 1.0f;
    screenUv.y = -screenUv.y;

    float3 skyDir = SafeNormalize(float3(screenUv.x, screenUv.y, 1.0f));

    float3 skyColor = gPrefilterMap.SampleLevel(gSam, skyDir, 0.0f).rgb;

    skyColor = ApplyReinhardToneMapping(skyColor);
    skyColor = ApplyGammaCorrection(skyColor);

    return saturate(skyColor);
}

float4 PSMain(PSIn pin) : SV_Target
{
    float4 albedoRoughness = gAlbedoTex.Sample(gSam, pin.Tex);
    float3 albedo = albedoRoughness.rgb;
    float roughness = clamp(albedoRoughness.a, 0.04f, 1.0f);

    float4 normalMetallic = gNormalTex.Sample(gSam, pin.Tex);
    float3 normalW = SafeNormalize(normalMetallic.rgb * 2.0f - 1.0f);
    float metallic = saturate(normalMetallic.a);

    float3 posW = gPositionTex.Sample(gSam, pin.Tex).rgb;

    
    bool isBackground = dot(normalMetallic.rgb, normalMetallic.rgb) < 0.0001f;

    if (isBackground)
    {
        float3 skyColor = GetQuickSkyboxColor(pin.Tex);
        return float4(skyColor, 1.0f);
    }

#if DEBUG_CASCADES
    int debugCascade = SelectCascade(posW);

    if (debugCascade == 0)
        return float4(1.0f, 0.0f, 0.0f, 1.0f);

    if (debugCascade == 1)
        return float4(0.0f, 1.0f, 0.0f, 1.0f);

    if (debugCascade == 2)
        return float4(0.0f, 0.0f, 1.0f, 1.0f);

    if (debugCascade == 3)
        return float4(1.0f, 1.0f, 0.0f, 1.0f);

    return float4(1.0f, 0.0f, 1.0f, 1.0f);
#endif

    float3 V = SafeNormalize(gEyePosW - posW);
    float NdotV = max(dot(normalW, V), 0.0f);

    float shadow = CalcShadowFactor(posW, normalW);

    

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);

    
    float3 irradiance = gIrradianceMap.Sample(gSam, normalW).rgb;
    float3 diffuseIBL = irradiance * albedo;

    
    float3 R = reflect(-V, normalW);

    const float MAX_REFLECTION_LOD = 7.0f;
    float3 prefilteredColor = gPrefilterMap.SampleLevel(
        gSam,
        R,
        roughness * MAX_REFLECTION_LOD
    ).rgb;

    float2 brdf = gBrdfLUT.Sample(gSam, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    float ao = 1.0f;
    float3 ambient = (kD * diffuseIBL + specularIBL) * ao;

    

    float3 color = ambient;
    color += CalcPBRDirectionalLight(
        albedo,
        roughness,
        metallic,
        normalW,
        V,
        posW,
        shadow
    );

    
    color *= 1.15f;

    color = ApplyReinhardToneMapping(color);
    color = ApplyGammaCorrection(color);

    return float4(saturate(color), 1.0f);
}