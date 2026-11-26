#pragma once

#include "Core/Types.h"
#include "Render/PipelineState.h"

#include <d3d11.h>
#include <wrl/client.h>

class FRenderer;
class FTexture2D;

// 카메라 노출계. HDR 장면의 중앙 중점 평균 휘도를 GPU에서 구하고 몇 프레임 뒤에 CPU로 읽어 온다.
class FExposureMeterRenderer
{
public:
	void Init(FRenderer* InRenderer);

	FTexture2D* GetTarget() const { return Target.get(); }
	bool Render(FTexture2D* SceneHDR);

	static bool GetAverageLuminance(float& OutLuminance);
	static float UpdateAdaptedEV100(const struct FPostProcessSettings& Settings);
	static void SetDisplayGamma(float InGamma) { DisplayGamma = InGamma; }
	static float GetDisplayGamma() { return DisplayGamma; }
	static bool GetAdaptedEV100(float& OutEV100)
	{
		OutEV100 = AdaptedEV100;
		return bHasAdapted;
	}

private:
	void ReadBack();

	static constexpr uint32 TargetSize = 256;
	static constexpr uint32 NumStaging = 3;

	FRenderer* Renderer = nullptr;
	FShaderProgram* Shader = nullptr;
	FPipelineState PipelineState;
	TUniquePtr<FTexture2D> Target;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> MipChain;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> MipChainSRV;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> Staging[NumStaging];
	bool bStagingPending[NumStaging]{};
	uint32 WriteIndex = 0;
	uint32 LastMip = 0;

	static inline float AverageLuminance = 0.18f;
	static inline bool bHasReading = false;
	static inline float AdaptedEV100 = 0.0f;
	static inline bool bHasAdapted = false;
	static inline float LastAdaptTime = -1.0f;
	static inline float DisplayGamma = 1.0f;
};
