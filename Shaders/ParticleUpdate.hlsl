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

cbuffer ParticleConstants : register(b0)
{
    float4 EmitterPositionAndSpawnRadius;
    float4 EmitterVelocityAndDeltaTime;
    float4 SimParams;
    uint4 Counts; 
};

ConsumeStructuredBuffer<Particle> InputParticles : register(u0);
AppendStructuredBuffer<Particle> OutputParticles : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint sourceCount = Counts.z;
    if (id.x >= sourceCount)
        return;

    Particle p = InputParticles.Consume();

    float dt = EmitterVelocityAndDeltaTime.w;
    float gravity = SimParams.x;

    p.Age += dt;

    if (p.Age >= p.Lifetime)
        return;

    float t = saturate(p.Age / max(p.Lifetime, 0.0001f));

    p.Velocity.y += gravity * dt;
    p.Position += p.Velocity * dt;
    p.Color = lerp(p.StartColor, p.EndColor, t);
    p.Size = lerp(0.22f, 0.05f, t);
    p.Active = 1;

    OutputParticles.Append(p);
}