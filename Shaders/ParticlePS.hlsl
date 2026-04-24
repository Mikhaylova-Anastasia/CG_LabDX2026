Texture2D ParticleTexture : register(t1);
SamplerState Sampler : register(s0);

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR0;
};

float4 PSMain(VSOutput input) : SV_Target0
{
    float4 texColor = ParticleTexture.Sample(Sampler, input.Tex);

    
    clip(texColor.a - 0.5f);

    return float4(input.Color.rgb * texColor.rgb, 1.0f);
}