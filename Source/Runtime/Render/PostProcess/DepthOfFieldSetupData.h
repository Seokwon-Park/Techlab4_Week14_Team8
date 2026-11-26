#pragma once

struct alignas(16) DepthOfFieldSetupData
{
	float ProjectionA;
	float ProjectionB;
	float FocusDistance;
	float InfinityCoCRadius;
};

static_assert(sizeof(DepthOfFieldSetupData) == 16, "DepthOfFieldSetupData size is not correct");