#pragma once

struct alignas(16) FToneMapData
{
	float InvGamma = 1.0f / 2.2f;
	float bGammaCorretion = 1.0f;
	float Exposure = 1.0f; // 톤매핑 전에 곱하는 노출 배율
	float Padding = 0.0f;
};

static_assert(sizeof(FToneMapData) == 16);
