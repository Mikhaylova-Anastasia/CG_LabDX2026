
#define DEBUG_CASCADES 0

Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gPositionTex : register(t2);
Texture2DArray gShadowMap : register(t3);
Texture2D gShadowMaskTex : register(t4); 

SamplerState gSam : register(s0);
SamplerComparisonState gShadowSam : register(s1);

static const int CASCADE_COUNT = 4;

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

float3 CalcDirectionalLight(float3 albedo, float3 normalW, float3 posW, float shadow)
{
    float3 lightDir = SafeNormalize(-gDirLight.Direction);
    float ndotl = saturate(dot(normalW, lightDir));

    return albedo * gDirLight.Color * gDirLight.Intensity * ndotl * shadow;
}

float3 ApplyShadowMask(float3 color, float3 posW, float shadow)
{
    
    float shadowArea = saturate((1.0f - shadow) * 1.35f);

   
    float2 maskUV = frac(posW.xz * 0.35f);
    float3 maskColor = gShadowMaskTex.Sample(gSam, maskUV).rgb;

  
    return lerp(color, color * 0.55f + maskColor * 0.45f, shadowArea);
}

float4 PSMain(PSIn pin) : SV_Target
{
    float3 albedo = gAlbedoTex.Sample(gSam, pin.Tex).rgb;

    float3 normalEncoded = gNormalTex.Sample(gSam, pin.Tex).rgb;
    float3 normalW = SafeNormalize(normalEncoded * 2.0f - 1.0f);

    float3 posW = gPositionTex.Sample(gSam, pin.Tex).rgb;

#if DEBUG_CASCADES
    int debugCascade = SelectCascade(posW);

    if (debugCascade == 0)
        return float4(1.0f, 0.0f, 0.0f, 1.0f); // near cascade

    if (debugCascade == 1)
        return float4(0.0f, 1.0f, 0.0f, 1.0f);

    if (debugCascade == 2)
        return float4(0.0f, 0.0f, 1.0f, 1.0f);

    if (debugCascade == 3)
        return float4(1.0f, 1.0f, 0.0f, 1.0f);

    return float4(1.0f, 0.0f, 1.0f, 1.0f); //  
#endif

    float shadow = CalcShadowFactor(posW, normalW);

    float3 color = albedo * gAmbientColor;
    color += CalcDirectionalLight(albedo, normalW, posW, shadow);

    color = ApplyShadowMask(color, posW, shadow);

    return float4(saturate(color), 1.0f);
}