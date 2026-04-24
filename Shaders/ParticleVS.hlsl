struct Particle
{
    float3 Position;
    float Age;

    float3 Velocity;
    float Lifetime;

    float3 Color;
    float Size;

    float3 StartColor;
    float Rotation;

    float3 EndColor;
    uint Active;
};

StructuredBuffer<Particle> ParticleBuffer : register(t0);

struct VSOutput
{
    float3 Position : POSITION;
    float3 Color : COLOR0;
    float Size : SIZE;
    uint Active : ACTIVE;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    Particle p = ParticleBuffer[vertexID];

    VSOutput output;
    output.Position = p.Position;
    output.Color = p.Color;
    output.Size = p.Size;
    output.Active = p.Active;

    return output;
}