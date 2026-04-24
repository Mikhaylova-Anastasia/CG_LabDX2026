cbuffer RenderConstants : register(b0)
{
    float4x4 ViewProj;
    float4 CameraPosAndSize;
};

struct VSOutput
{
    float3 Position : POSITION;
    float3 Color : COLOR0;
    float Size : SIZE;
    uint Active : ACTIVE;
};

struct GSOutput
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
    float4 Color : COLOR0;
};

[maxvertexcount(6)]
void GSMain(point VSOutput input[1], inout TriangleStream<GSOutput> stream)
{
    if (input[0].Active == 0)
        return;

    float3 position = input[0].Position;

    float3 toCamera = normalize(CameraPosAndSize.xyz - position);

    float3 worldUp = float3(0.0f, 1.0f, 0.0f);
    float3 right = normalize(cross(worldUp, toCamera));
    float3 up = cross(toCamera, right);

    float size = max(input[0].Size * 2.0f, 0.05f);
    float4 color = float4(input[0].Color, 1.0f);

    float2 corners[4] =
    {
        float2(-1, -1),
        float2(-1, 1),
        float2(1, 1),
        float2(1, -1)
    };

    float2 uvs[4] =
    {
        float2(0, 1),
        float2(0, 0),
        float2(1, 0),
        float2(1, 1)
    };

    int idx[6] = { 0, 1, 2, 0, 2, 3 };

    [unroll]
    for (int i = 0; i < 6; i++)
    {
        int id = idx[i];

        float3 worldPos =
            position +
            right * corners[id].x * size +
            up * corners[id].y * size;

        GSOutput o;
        o.PosH = mul(float4(worldPos, 1), ViewProj);
        o.Tex = uvs[id];
        o.Color = color;

        stream.Append(o);
    }
}