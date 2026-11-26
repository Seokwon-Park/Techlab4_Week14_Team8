#pragma pack_matrix(row_major)
cbuffer constants : register(b0)
{
    matrix World;
    matrix ViewProjection;
    float4 Color;
    float4 ViewDirShade;   // xyz: 카메라 -> 기즈모 방향, w: 음영 세기 (0이면 단색)
    float4 OriginClip;     // xyz: 기즈모 중심, w: 1이면 카메라 반대편 절반을 그리지 않는다 (회전 링)
    float4 Params;         // x: 사각형 테두리 두께 (UV 단위, 0이면 끔)
};

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    float4 worldPos = mul(float4(input.position, 1.0f), World);
    output.position = mul(worldPos, ViewProjection);
    output.worldPos = worldPos.xyz;
    output.normal = mul(input.normal, (float3x3)World);
    output.uv = input.uv;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // 회전 링의 뒤쪽 절반을 잘라 앞쪽만 보이게 한다.
    if (OriginClip.w > 0.5f && dot(input.worldPos - OriginClip.xyz, ViewDirShade.xyz) > 0.0f)
        discard;

    float4 result = Color;

    // 카메라를 향한 면은 밝게, 비스듬한 면은 살짝 어둡게 해 입체감만 준다.
    if (ViewDirShade.w > 0.0f)
    {
        float3 n = normalize(input.normal);
        float facing = abs(dot(n, ViewDirShade.xyz));
        result.rgb *= lerp(1.0f, 0.55f + 0.45f * facing, ViewDirShade.w);
    }

    // 평면 핸들은 테두리를 진하게 칠해 윤곽을 또렷하게 만든다.
    if (Params.x > 0.0f)
    {
        float2 uv = input.uv;
        float edge = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
        float aa = fwidth(edge) + 1e-5f;
        float border = 1.0f - smoothstep(Params.x - aa, Params.x + aa, edge);
        result.a = lerp(Color.a, saturate(Color.a * 2.6f), border);
    }

    return result;
}
