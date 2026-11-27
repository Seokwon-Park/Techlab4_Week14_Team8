#pragma once

#include "MaterialInterface.h"
#include "RenderResourceManager.h"
#include "Render/RenderStates.h"
#include "Render/Texture2D.h"
#include "Render/Buffer.h"
#include <algorithm>
#include "Material.generated.h"

class FShader;

enum class EMaterialParamLayout
{
	None,
	StaticMesh,
	ParticleSubUV
};

// 머티리얼이 어디에 쓰이는지. UE의 EMaterialDomain과 같은 자리다
enum class EMaterialDomain : uint8
{
	Surface,
	Particle,
	Decal,
	UI
};

// 화면에 섞는 방식. UE의 EBlendMode와 같은 자리다.
// Masked는 불투명 패스에서 그리되 알파가 OpacityMaskClipValue보다 작은 픽셀을 버린다
enum class EMaterialBlendMode : uint8
{
	Opaque,
	Masked,
	Translucent,
	Additive
};

// 조명 계산 방식. UE의 EMaterialShadingModel과 같은 자리다.
// Unlit은 Forward 경로에서만 머티리얼별로 적용된다(Deferred는 GBuffer 조명을 그대로 받는다)
enum class EMaterialShadingModel : uint8
{
	DefaultLit,
	Unlit
};

const char* MaterialDomainToString(EMaterialDomain Domain);
EMaterialDomain StringToMaterialDomain(const FString& Str);
const char* MaterialBlendModeToString(EMaterialBlendMode Mode);
EMaterialBlendMode StringToMaterialBlendMode(const FString& Str);
const char* MaterialShadingModelToString(EMaterialShadingModel Model);
EMaterialShadingModel StringToMaterialShadingModel(const FString& Str);
const char* SamplerStateToString(ESamplerState State);
ESamplerState StringToSamplerState(const FString& Str);

struct FStaticMeshMaterialParams
{
	FVector4 BaseColor;
	FVector2 UVOffset;
	FVector2 TextureFlags; // 패스에 따라 노멀맵 정보 또는 색 공간 변환 플래그
	FVector4 MaskParams;   // x: Masked 알파 기준값(0이면 끔), y: Unlit이면 1. StaticMeshGBufferShader.hlsl의 MaterialParams와 같은 순서
};
static_assert(sizeof(FStaticMeshMaterialParams) == 48);

UCLASS()
class UMaterial : public UMaterialInterface
{
	GENERATED_BODY()

public:
	UMaterial() :SortID(NextSortID++) {};
	virtual ~UMaterial() override = default;

	EMaterialParamLayout ParamLayout = EMaterialParamLayout::None;
	FShaderProgram* Shader = nullptr;
	TArray<UTexture2D*> Textures;
	TUniquePtr<FConstantBuffer> ParamBuffer;
	EPSOType PSOType = EPSOType::StaticMesh_Opaque;
	EBlendState BlendState = EBlendState::Opaque;
	UPROPERTY(EditAnywhere)
	bool bRenderBeforeDOF = false;
	// 뒷면 컬링을 끄고 뒷면 픽셀의 법선을 뒤집는다. 천처럼 면 한 장으로 된 메시에 쓴다
	UPROPERTY(EditAnywhere)
	bool bTwoSided = false;
	EDepthStencilState DepthStencilState = EDepthStencilState::Default;
	ESamplerState SamplerState = ESamplerState::LinearClamp;

	// 머티리얼 렌더링 속성. 메시 PSO(StaticMesh_Opaque/Translucent/Additive)는 SyncLegacyStates가 이 값으로 맞춘다.
	// 파티클/빌보드/데칼처럼 전용 PSO를 쓰는 머티리얼은 PSOType이 그대로 기준이다
	EMaterialDomain MaterialDomain = EMaterialDomain::Surface;
	EMaterialBlendMode BlendMode = EMaterialBlendMode::Opaque;
	EMaterialShadingModel ShadingModel = EMaterialShadingModel::DefaultLit;
	// Masked에서 알파(BaseColor 텍스처 a × BaseColor.a)가 이 값보다 작으면 픽셀을 버린다. UE 기본값과 같다
	float OpacityMaskClipValue = 0.333f;

	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FVector SpecularColor = FVector(1.0f, 1.0f, 1.0f);
	float Shininess = 32.0f;
	FVector2 UVScrollSpeed = FVector2(0.0f, 0.0f);
	bool bFlipNormalY = false;

	bool bIsInstance = false;

	// 인스턴스가 복제된 원본. 인스턴스는 경로가 없어서, 저장할 때 원본을 따라가 기준 에셋을 찾는다
	const UMaterial* Parent = nullptr;

	// Source의 내용을 복사한 편집용 복제본을 만든다
	static UMaterial* CreateInstance(const UMaterial* Source);

	// 경로가 있는(=에셋으로 등록된) 가장 가까운 원본. 자신이 에셋이면 자신
	const UMaterial* GetBaseAsset() const;
	bool ShouldRenderBeforeDOF() const
	{
		return (PSOType == EPSOType::StaticMesh_Translucent || PSOType == EPSOType::StaticMesh_Additive) && bRenderBeforeDOF;
	}

	// 다른 머티리얼의 속성을 복사한다 (Path, SortID, bIsInstance, Parent, ParamBuffer는 유지)
	void CopyPropertiesFrom(const UMaterial& Source);

	// 메시 PSO를 쓰는 머티리얼이면 BlendMode에 맞춰 PSOType/BlendState/DepthStencilState를 맞춘다
	void SyncLegacyStates();
	// 반대로 PSOType/BlendState(예전 씬 파일, 에디터의 블렌드 콤보)에서 BlendMode를 정한다. Masked는 유지한다
	void SyncBlendModeFromLegacy();

	bool UsesMeshPSO() const
	{
		return PSOType == EPSOType::StaticMesh_Opaque || PSOType == EPSOType::StaticMesh_Translucent
			|| PSOType == EPSOType::StaticMesh_Additive;
	}
	bool IsTranslucent() const
	{
		return BlendMode == EMaterialBlendMode::Translucent || BlendMode == EMaterialBlendMode::Additive;
	}
	// 불투명 메시 PSO로 그리면서 알파로 픽셀을 버리는지. 에디터가 PSOType만 바꿔도 어긋나지 않게 둘 다 본다
	bool IsMasked() const { return BlendMode == EMaterialBlendMode::Masked && PSOType == EPSOType::StaticMesh_Opaque; }
	bool IsUnlit() const { return ShadingModel == EMaterialShadingModel::Unlit; }
	// 셰이더에 넘길 Masked 기준값. Masked가 아니면 0(끔)
	float GetMaskClipValue() const { return IsMasked() ? std::max(OpacityMaskClipValue, 1.0e-4f) : 0.0f; }

	UTexture2D* GetTexture(int32 Slot) const;
	void SetTexture(int32 Slot, UTexture2D* Texture);

	const uint16 SortID;

	using Super::Serialize;
	// 인스턴스의 값(색, 텍스처, 상태). 객체는 이미 만들어져 있어야 한다.
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// 머티리얼 참조 하나를 Slot에 읽고 쓴다.
	// 에셋이면 경로만, 인스턴스면 기준 에셋 + 값을 그 자리에 쓰고, 불러올 때 인스턴스를 새로 만든다.
	// null이면 빈 Record({})로 기록되고, 불러오면 nullptr이 된다.
	static void SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterial*& Material);
private:
	inline static uint16 NextSortID = 0;
};
