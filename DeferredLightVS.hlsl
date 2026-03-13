struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;

    float2 pos[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    float2 uv[3] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    o.PosH = float4(pos[id], 0.0f, 1.0f);
    o.Tex = uv[id];
    return o;
}