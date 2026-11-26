#include "EnginePCH.h"
#include "Engine/PostProcessSettings.h"

#include <algorithm>
#include <cmath>

void FPostProcessSettings::OverrideBlend(const FPostProcessSettings& Src, float Weight)
{
	Weight = std::clamp(Weight, 0.0f, 1.0f);
	if (Weight <= 0.0f) return;

	auto BlendFloat = [Weight](bool bOverride, float& Dest, float Value)
	{
		if (bOverride) Dest += (Value - Dest) * Weight;
	};

	BlendFloat(Src.bOverride_VignetteIntensity, VignetteIntensity, Src.VignetteIntensity);
	BlendFloat(Src.bOverride_LensVignetteAmount, LensVignetteAmount, Src.LensVignetteAmount);
	BlendFloat(Src.bOverride_LensMaxAperture, LensMaxAperture, Src.LensMaxAperture);
	BlendFloat(Src.bOverride_LensBarrelRadius, LensBarrelRadius, Src.LensBarrelRadius);
	BlendFloat(Src.bOverride_DepthOfFieldFocalDistance, DepthOfFieldFocalDistance, Src.DepthOfFieldFocalDistance);
	BlendFloat(Src.bOverride_DepthOfFieldFocusRange, DepthOfFieldFocusRange, Src.DepthOfFieldFocusRange);
	BlendFloat(Src.bOverride_DepthOfFieldFstop, DepthOfFieldFstop, Src.DepthOfFieldFstop);
	BlendFloat(Src.bOverride_DepthOfFieldSensorWidth, DepthOfFieldSensorWidth, Src.DepthOfFieldSensorWidth);
	if (Src.bOverride_DepthOfFieldBladeCount && Weight >= 0.5f)
		DepthOfFieldBladeCount = Src.DepthOfFieldBladeCount;
	BlendFloat(Src.bOverride_LensFlareIntensity, LensFlareIntensity, Src.LensFlareIntensity);
	BlendFloat(Src.bOverride_LensFlareThreshold, LensFlareThreshold, Src.LensFlareThreshold);
	if (Src.bOverride_LensFlareGhostCount && Weight >= 0.5f)
		LensFlareGhostCount = Src.LensFlareGhostCount;
	BlendFloat(Src.bOverride_LensFlareGhostDispersal, LensFlareGhostDispersal, Src.LensFlareGhostDispersal);
	BlendFloat(Src.bOverride_LensFlareHaloWidth, LensFlareHaloWidth, Src.LensFlareHaloWidth);
	BlendFloat(Src.bOverride_LensFlareHaloIntensity, LensFlareHaloIntensity, Src.LensFlareHaloIntensity);
	BlendFloat(Src.bOverride_LensFlareBokehSize, LensFlareBokehSize, Src.LensFlareBokehSize);
	BlendFloat(Src.bOverride_LensFlareChromaticDistortion, LensFlareChromaticDistortion, Src.LensFlareChromaticDistortion);
	BlendFloat(Src.bOverride_LensFlareDirtIntensity, LensFlareDirtIntensity, Src.LensFlareDirtIntensity);
	BlendFloat(Src.bOverride_CameraShutterSpeed, CameraShutterSpeed, Src.CameraShutterSpeed);
	BlendFloat(Src.bOverride_CameraISO, CameraISO, Src.CameraISO);
	if (Src.bOverride_bApplyPhysicalCameraExposure && Weight >= 0.5f)
		bApplyPhysicalCameraExposure = Src.bApplyPhysicalCameraExposure;
	if (Src.bOverride_bAutoExposure && Weight >= 0.5f)
		bAutoExposure = Src.bAutoExposure;
	BlendFloat(Src.bOverride_AutoExposureMinEV100, AutoExposureMinEV100, Src.AutoExposureMinEV100);
	BlendFloat(Src.bOverride_AutoExposureMaxEV100, AutoExposureMaxEV100, Src.AutoExposureMaxEV100);
	BlendFloat(Src.bOverride_AutoExposureSpeedUp, AutoExposureSpeedUp, Src.AutoExposureSpeedUp);
	BlendFloat(Src.bOverride_AutoExposureSpeedDown, AutoExposureSpeedDown, Src.AutoExposureSpeedDown);
	BlendFloat(Src.bOverride_AutoExposureBias, AutoExposureBias, Src.AutoExposureBias);
}

namespace
{
	// 씬 라이트가 물리 단위가 아니라서 "지금 화면 밝기 = 맑은 날 낮"이라고 정한 기준점
	constexpr float SunnySixteenFstop = 16.0f;
	constexpr float SunnySixteenShutter = 200.0f;
	constexpr float SunnySixteenISO = 200.0f;
	// 노출계가 평균 밝기를 맞추려는 목표(톤매핑 전 선형 값)
	constexpr float MiddleGray = 0.36f;

	float GetReferenceEV100()
	{
		static const float Reference = FPostProcessSettings::ComputeEV100(SunnySixteenFstop, SunnySixteenShutter, SunnySixteenISO);
		return Reference;
	}

	// ToneMapShader.hlsl의 ACESFilm과 같은 상수여야 화면 밝기를 거꾸로 풀 수 있다
	constexpr float AcesA = 2.51f, AcesB = 0.03f, AcesC = 2.43f, AcesD = 0.59f, AcesE = 0.14f;

	float ACESFilm(float X)
	{
		return std::clamp((X * (AcesA * X + AcesB)) / (X * (AcesC * X + AcesD) + AcesE), 0.0f, 1.0f);
	}

	float InverseACESFilm(float Y)
	{
		Y = std::clamp(Y, 0.0f, 0.99f);
		const float A = AcesA - AcesC * Y;
		const float B = AcesB - AcesD * Y;
		return (-B + std::sqrt(B * B + 4.0f * A * AcesE * Y)) / (2.0f * A);
	}

	float GetMiddleGrayForGamma(float DisplayGamma)
	{
		if (DisplayGamma <= 1.0f) return MiddleGray;
		return InverseACESFilm(std::pow(ACESFilm(MiddleGray), DisplayGamma));
	}
}

float FPostProcessSettings::ComputeEV100(float Fstop, float ShutterSpeed, float ISO)
{
	return std::log2(Fstop * Fstop * ShutterSpeed * 100.0f / ISO);
}

float FPostProcessSettings::GetMeteredEV100(float AverageLuminance, float DisplayGamma)
{
	return GetReferenceEV100() + std::log2(std::max(AverageLuminance, 1e-6f) / GetMiddleGrayForGamma(DisplayGamma));
}

float FPostProcessSettings::GetCameraEV100() const
{
	return ComputeEV100(std::max(DepthOfFieldFstop, 0.5f), std::max(CameraShutterSpeed, 1.0f), std::max(CameraISO, 1.0f));
}

float FPostProcessSettings::GetPhysicalCameraExposureScale() const
{
	if (!bApplyPhysicalCameraExposure) return 1.0f;
	return EV100ToExposureScale(GetCameraEV100());
}

float FPostProcessSettings::EV100ToExposureScale(float EV100)
{
	return std::exp2(GetReferenceEV100() - EV100);
}

float FPostProcessSettings::GetExposureScale(float AdaptedEV100) const
{
	if (bAutoExposure) return EV100ToExposureScale(AdaptedEV100 - AutoExposureBias);
	if (bApplyPhysicalCameraExposure) return EV100ToExposureScale(GetCameraEV100() - AutoExposureBias);
	return 1.0f;
}
