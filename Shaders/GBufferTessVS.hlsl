struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BINORMAL;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL : TANGENT;
    float3 BitangentL : BINORMAL;
    float2 Tex : TEXCOORD0;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput vout;
    vout.PosL = vin.Pos;
    vout.NormalL = vin.Normal;
    vout.TangentL = vin.Tangent;
    vout.BitangentL = vin.Bitangent;
    vout.Tex = vin.Tex;
    return vout;
}
