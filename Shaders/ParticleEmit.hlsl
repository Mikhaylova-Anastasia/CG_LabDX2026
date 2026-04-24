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

AppendStructuredBuffer<Particle> OutputParticles : register(u1);

float Random01(uint seed)
{
    return frac(sin(seed * 12.9898f) * 43758.5453f);
}

float RandomRange(uint seed, float minVal, float maxVal)
{
    return lerp(minVal, maxVal, Random01(seed));
}

float3 RandomDirection(uint seed)
{
    float u1 = Random01(seed + 11);
    float u2 = Random01(seed + 29);
    float theta = 6.2831853f * u1;
    float z = 2.0f * u2 - 1.0f;
    float r = sqrt(saturate(1.0f - z * z));
    return float3(r * cos(theta), abs(z), r * sin(theta));
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint numToSpawn = Counts.y;
    if (id.x >= numToSpawn)
        return;

    uint seedBase = id.x + (uint) (SimParams.y * 1000.0f);

    float3 dir = RandomDirection(seedBase);
    float spawnRadius = EmitterPositionAndSpawnRadius.w;

    Particle p;
    p.Position = EmitterPositionAndSpawnRadius.xyz + dir * RandomRange(seedBase + 101, 0.0f, spawnRadius);
    p.Velocity = EmitterVelocityAndDeltaTime.xyz + dir * RandomRange(seedBase + 203, 2.0f, 6.0f);
    p.Age = 0.0f;
    p.Lifetime = RandomRange(seedBase + 307, 1.0f, max(1.0f, SimParams.z));
    p.Size = RandomRange(seedBase + 401, 0.12f, 0.22f);
    p.StartColor = float3(1.0f, 0.72f, 0.25f);
    p.EndColor = float3(0.85f, 0.18f, 0.05f);
    p.Color = p.StartColor;
    p.Rotation = RandomRange(seedBase + 503, 0.0f, 6.2831853f);
    p.Active = 1;

    OutputParticles.Append(p);
}