#pragma once

#include "Shader.h"
#include "PipelineState.h"
#include "VertexFactory.h"
#include "Core/Misc/Paths.h"

// 렌더링에 사용할 파이프라인 종류 정의
enum class EPSOType : uint8
{
	StaticMesh_Opaque,       // 기본 불투명
	StaticMesh_Translucent,  // 반투명
	StaticMesh_Wireframe,    
	Particle_AlphaBlend,     
	Particle_Additive,       
	Skybox,                  
	Grid,                    
	Outline_Mask,            
	Outline_Draw,
	StaticMesh_GBuffer,
	DeferredLighting,
	DeferredPointLighting,      // 이전 재질 파일의 PSOType 숫자와 호환을 위해 ID 유지
	ToneMap,
	Reserved_13,                // 삭제된 FireBall 자리. 씬 파일의 PSOType 숫자(Billboard=14 등)가 밀리지 않게 남긴다
	Billboard,
	StaticMesh_Forward,
	StaticMesh_WorldNormal,
	StaticMesh_DepthOnly,
	StaticMesh_LightShadow,
	LightCullingDebug,
	ShadowTileClear,         // 그림자 캐시: 다시 그릴 아틀라스 타일만 깊이 1로 지운다
	ShadowDepth,
	ShadowProjection,        // 그림자 한 장을 화면 마스크(ScreenShadowMask)에 그린다
	ShadowMaskVisualize,     // Shadow Mask 뷰 모드: 마스크를 화면에 띄운다
	Decal, 
	DecalAdditive,
	ParticleSprite_AlphaBlend,   // 새로 추가
	ParticleSprite_Additive,     // 새로 추가
	ParticleTrail_AlphaBlend,    // 새로 추가
	ParticleTrail_Additive,      // 새로 추가
	DepthOfFieldSetup,            // 새로 추가
	DepthOfFieldGather,
	DepthOfFieldPostfilter,
	DepthOfFieldRecombine,
	DepthOfFieldVisualize,
	// 기존 씬/에셋의 PSOType 번호가 밀리지 않도록 새 항목은 여기(Count 앞)에만 추가한다
	StaticMesh_Additive,         // 가산 블렌드 메시 (EMaterialBlendMode::Additive)
	Count
};

class FRenderResourceManager
{
public:
	static FRenderResourceManager& Get()
	{
		static FRenderResourceManager* Instance = new FRenderResourceManager();
		return *Instance;
	}

	static void Init()
	{
		Get().ScanShaders(HITORI_ENGINE_SHADERS_FOLDER);
		Get().InitPipelineStates(); // PSO 초기화 추가
	}

	void ScanShaders(const fs::path& ShaderRoot);
	static void Shutdown();
	static FShaderProgram* GetShaderProgram(const FString& InPath);

	static FPipelineState* GetPSO(const EPSOType& InType);
	static FShaderProgram* GetForwardShaderProgram(ELightingModel Model);
	static FShaderProgram* FindMeshParticleShader(const FShaderProgram& Base);
	// Local 변형인 Base를 정점 타입에 맞는 변형으로 바꾼다.
	// 전용 셰이더 정점 타입(스프라이트, 트레일)은 Base를 그대로 돌려준다. 변형이 없으면 nullptr
	static FShaderProgram* ResolveShader(FShaderProgram* Base, EVertexFactoryType VF);
private:
	
	void LoadOrCompileShader(const FString& Path);
	void CreateVertexShaderVariant(const FString& Path, const char* FeatureMacro,
		const char* LightingMacro, const FShaderProgram& Base);
	void InitPipelineStates();

	TMap<FString, TUniquePtr<FVertexShader>> VertexShaderMap;
	TMap<FString, TUniquePtr<FPixelShader>>  PixelShaderMap;
	TMap<FString, TUniquePtr<FShaderProgram>> ShaderProgramMap;

	TMap<EPSOType, TUniquePtr<FPipelineState>> PipelineStateMap;
};
