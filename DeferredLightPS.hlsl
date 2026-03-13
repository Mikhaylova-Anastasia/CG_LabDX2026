Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gPositionTex : register(t2);
SamplerState gSam : register(s0);

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
        return float3(0.0f, 0.0f, 1.0f);
    return v * rsqrt(len2);
}

float3 ApplyDirectional(float3 albedo, float3 normal)
{
    float3 L = SafeNormalize(-gDirLight.Direction);
    float ndotl = saturate(dot(normal, L));
    return albedo * gDirLight.Color * ndotl * gDirLight.Intensity;
}

float3 ApplyPoint(float3 albedo, float3 normal, float3 posW, PointLight light)
{
    float3 toLight = light.Position - posW;
    float dist = length(toLight);

    if (dist > light.Range)
        return 0.0f;

    float3 L = toLight / max(dist, 0.0001f);
    float att = saturate(1.0f - dist / light.Range);
    att *= att;

    float ndotl = saturate(dot(normal, L));
    return albedo * light.Color * ndotl * att * light.Intensity;
}

float3 ApplySpot(float3 albedo, float3 normal, float3 posW, SpotLight light)
{
    float3 toLight = light.Position - posW;
    float dist = length(toLight);

    if (dist > light.Range)
        return 0.0f;

    float3 L = toLight / max(dist, 0.0001f);
    float3 spotDir = SafeNormalize(-light.Direction);

    float cone = saturate(dot(L, spotDir));
    cone = pow(cone, light.SpotPower);

    float att = saturate(1.0f - dist / light.Range);
    att *= att;

    float ndotl = saturate(dot(normal, L));
    return albedo * light.Color * ndotl * att * cone * light.Intensity;
}

float4 PSMain(PSIn pin) : SV_Target
{
    float3 albedo = gAlbedoTex.Sample(gSam, pin.Tex).rgb;
    float3 normal = gNormalTex.Sample(gSam, pin.Tex).rgb * 2.0f - 1.0f;
    float3 posW = gPositionTex.Sample(gSam, pin.Tex).rgb;

    normal = SafeNormalize(normal);

    float3 color = albedo * gAmbientColor;
    color += ApplyDirectional(albedo, normal);
    color += ApplyPoint(albedo, normal, posW, gPointLights[0]);
    color += ApplyPoint(albedo, normal, posW, gPointLights[1]);
    color += ApplySpot(albedo, normal, posW, gSpotLight);

    return float4(color, 1.0f);
}