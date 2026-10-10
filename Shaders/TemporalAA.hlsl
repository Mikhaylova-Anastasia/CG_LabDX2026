Texture2D<float4> Current : register(t0);
// XY: previous unjittered UV, Z: previous view depth, W: current view depth.
Texture2D<float4> Reprojection : register(t1);
Texture2D<float4> History : register(t2);
// Ten independent reconstructed frames for the deliberately ghosted mode.
Texture2DArray<float4> GhostFrames : register(t3);
SamplerState LinearClamp : register(s0);
cbuffer TAAConstants : register(b0)
{
    float2 InvSize;
    float2 JitterUV;
    uint HistoryIndex;
    uint UseHistory;
    uint DisplayIndex;
    float CurrentWeight;
    uint GhostMode;
    uint GhostCount;
    uint Padding0;
    uint Padding1;
};
struct ScreenPixel
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};
ScreenPixel VSMain(uint id : SV_VertexID)
{
    ScreenPixel o;
    o.UV = float2((id << 1) & 2, id & 2);
    o.Position = float4(o.UV * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 ReadHistory(float2 uv)
{
    return History.SampleLevel(LinearClamp, uv, 0);
}
float ReadDepth(int2 p)
{
    return History.Load(int3(p, 0)).a;
}
float4 PSResolve(ScreenPixel pin) : SV_Target
{
    int2 size = int2(round(1.0 / InvSize));
    int2 pixel = clamp(int2(pin.Position.xy), int2(0, 0), size - 1);
   
    float2 sourceUV = clamp(pin.UV + JitterUV, 0.5 * InvSize, 1.0 - 0.5 * InvSize);
    float3 current = Current.SampleLevel(LinearClamp, sourceUV, 0).rgb;
    int2 motionPixel = pixel;
    float4 motion = Reprojection.Load(int3(pixel, 0));
    
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 q = clamp(pixel + int2(dx, dy), int2(0, 0), size - 1);
            float4 candidate = Reprojection.Load(int3(q, 0));
            if (candidate.w > 0 && (motion.w <= 0 || candidate.w < motion.w))
            {
                motion = candidate;
                motionPixel = q;
            }
        }
    }
    float depth = motion.w;
    
    if (depth <= 0)
        return float4(Current.Load(int3(pixel, 0)).rgb, 0);
    
    if (UseHistory == 0 || motion.z <= 0)
        return float4(current, depth);

    
    float2 motionUV = (float2(motionPixel) + 0.5) * InvSize;
    float2 velocity = (motionUV - JitterUV) - motion.xy;
    float2 previousUV = pin.UV - velocity;
    if (any(previousUV < 0.5 * InvSize) || any(previousUV > 1.0 - 0.5 * InvSize))
        return float4(current, depth);

    int2 previousPixel = clamp(int2(previousUV * size), int2(0, 0), size - 1);
    float oldDepth = ReadDepth(previousPixel);
 
    float tolerance = max(0.02, 0.01 * motion.z);
    if (oldDepth <= 0 || abs(oldDepth - motion.z) > tolerance)
        return float4(current, depth);

    float3 low = current;
    float3 high = current;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 q = clamp(pixel + int2(x, y), int2(0, 0), size - 1);
            float3 color = Current.Load(int3(q, 0)).rgb;
            low = min(low, color);
            high = max(high, color);
        }
    }
    float3 history = clamp(ReadHistory(previousUV).rgb, low, high);
    float speed = length((previousUV - pin.UV) / InvSize);
    float weight = lerp(CurrentWeight, 0.3, saturate(speed / 16.0));
    return float4(lerp(history, current, weight), depth);
}
float4 PSDisplay(ScreenPixel pin) : SV_Target
{
   
    int2 p = int2(pin.Position.xy);
    if (GhostMode != 0 && GhostCount != 0)
    {
        
        float3 sum = float3(0, 0, 0);
        uint count = min(GhostCount, 10u);
        [loop]
        for (uint i = 0; i < count; ++i)
            sum += GhostFrames.Load(int4(p, int(i), 0)).rgb;
        
        float3 live = History.Load(int3(p, 0)).rgb;
        return float4(lerp(sum / float(count), live, 0.35), 1);
    }
    return float4(History.Load(int3(p, 0)).rgb, 1);
}
