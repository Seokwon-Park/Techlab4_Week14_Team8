#pragma once

struct alignas(16) FDecalData
{
	FMatrix VP;
	float Fade;
	float bAdditive;
	float Padding[2];
};

static_assert(sizeof(FDecalData) == 80);

struct alignas(16) FTireMark
{
	static constexpr int32 MaxCount = 320;

	FVector4 CenterAndDepth;
	FVector4 AlongAndLength;
	FVector4 AcrossAndWidth;
	FVector4 NormalAndOpacity;
};

struct alignas(16) FTireMarkViewData
{
	FMatrix ViewProjection;
	FMatrix InverseViewProjection;
	FVector4 InvViewSize;
};

static_assert(sizeof(FTireMark) == 64);
static_assert(sizeof(FTireMarkViewData) == 144);

