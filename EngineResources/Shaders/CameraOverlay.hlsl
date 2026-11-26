cbuffer CameraOverlayConstants : register(b0)
{
    float4 FadeColor; // rgb = 색, a = FadeAmount
    float2 ViewSize;
    float VignetteIntensity; // 0 = 없음
    float VignettePower; // 가장자리 경도
    float LetterboxAmount; // 0~1
    float LetterboxAspect; // 2.39 = 시네마스코프
    float2 TanHalfFov; // 화면 가장자리까지의 tan(반화각). 0이면 렌즈 비네팅 끔
    float PupilRadius; // 경통 반지름 1 기준 동공 반지름 = 최대 개방 N / 현재 N
    float LensVignetteAmount; // 경통 깊이 k. 어긋남 d = k * tan(theta)
    float BarrelRadius; // 최대 개방 동공 반지름 1 기준 경통 반지름 (>= 1)
    float Padding;
};

// 조리개와 상관없이 화각만으로 정해지는 자연 비네팅 감쇠 (cos^4)
float NaturalVignette(float TanTheta)
{
    const float InvCosSquared = 1.0f + TanTheta * TanTheta;
    return 1.0f / (InvCosSquared * InvCosSquared);
}

// 경통 원과 동공 원의 겹치는 넓이 비율에 따른 광학 비네팅
float OpticalVignette(float TanTheta)
{
    const float PupilOffset = LensVignetteAmount * TanTheta;
    if (PupilRadius + PupilOffset <= BarrelRadius)
    {
        return 1.0f;
    }
    else if (PupilOffset >= BarrelRadius + PupilRadius)
    {
        return 0.0f;
    }
    else
    {
        const float OffsetSq = PupilOffset * PupilOffset;
        const float BarrelSq = BarrelRadius * BarrelRadius;
        const float PupilSq = PupilRadius * PupilRadius;
        const float BarrelHalfAngle = acos(clamp((OffsetSq + BarrelSq - PupilSq) / (2.0f * PupilOffset * BarrelRadius), -1.0f, 1.0f));
        const float PupilHalfAngle = acos(clamp((OffsetSq + PupilSq - BarrelSq) / (2.0f * PupilOffset * PupilRadius), -1.0f, 1.0f));
        const float BarrelSegment = BarrelSq * (BarrelHalfAngle - sin(BarrelHalfAngle) * cos(BarrelHalfAngle));
        const float PupilSegment = PupilSq * (PupilHalfAngle - sin(PupilHalfAngle) * cos(PupilHalfAngle));
        const float OverlapArea = BarrelSegment + PupilSegment;
        return saturate(OverlapArea / (3.14159265f * PupilSq));
    }
}

struct FPSInput
{
    float4 Position : SV_POSITION;
};

FPSInput mainVS(uint VertexID : SV_VertexID)
{
    FPSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FPSInput Input) : SV_Target
{
    const float2 UV = Input.Position.xy / ViewSize;
    const float ViewAspect = ViewSize.x / ViewSize.y;

    const float BarHeight = saturate(0.5f * (1.0f - ViewAspect / LetterboxAspect)) * LetterboxAmount;
    if (UV.y < BarHeight || UV.y > 1.0f - BarHeight)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    const float2 CenterOffset = (UV - 0.5f) * float2(ViewAspect, 1.0f);
    float Vignette = pow(saturate(1.0f - dot(CenterOffset, CenterOffset) * VignetteIntensity), VignettePower);

    if (LensVignetteAmount > 0.0f)
    {
        const float2 NDC = UV * 2.0f - 1.0f;
        const float TanTheta = length(NDC * TanHalfFov);
        const float LensVignette = NaturalVignette(TanTheta) * OpticalVignette(TanTheta);
        Vignette *= pow(saturate(LensVignette), 1.0f / 2.2f);
    }

    const float FadeAmount = FadeColor.a;
    const float OutAlpha = 1.0f - Vignette * (1.0f - FadeAmount);
    const float3 OutColor = (OutAlpha > 1e-4f) ? FadeColor.rgb * (FadeAmount / OutAlpha) : float3(0.0f, 0.0f, 0.0f);
    return float4(OutColor, OutAlpha);
}
