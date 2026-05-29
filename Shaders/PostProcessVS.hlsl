struct VSOutput
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput vout;

    // Full-screen quad generated only from SV_VertexID.
    // No vertex buffer is required.
    float2 pos[6] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 1.0f),
        float2(1.0f, 1.0f),

        float2(-1.0f, -1.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, -1.0f)
    };

    float2 uv[6] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),

        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f)
    };

    vout.PosH = float4(pos[vertexID], 0.0f, 1.0f);
    vout.Tex = uv[vertexID];

    return vout;
}