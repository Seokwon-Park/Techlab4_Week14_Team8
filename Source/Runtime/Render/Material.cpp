#include "EnginePCH.h"

#include "RenderCommand.h"

#include "Material.h"

#include "Asset/AssetManager.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"

namespace
{
	// 저장된 경로로 텍스처를 찾고, 아직 로드 전이면 로드한다
	UTexture2D* FindOrLoadTexture(const FString& Path)
	{
		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Path))
		{
			return Texture;
		}
		return UAssetManager::Get().LoadTexture(Path);
	}
}

UMaterial* UMaterial::CreateInstance(const UMaterial* Source)
{
	if (Source == nullptr)
	{
		return nullptr;
	}

	UMaterial* Instance = NewObject<UMaterial>();
	Instance->CopyPropertiesFrom(*Source);

	// ParamBuffer는 TUniquePtr라 복사할 수 없다.
	// 원본이 갖고 있으면 같은 크기로 새로 만들어 준다. 내용은 매 프레임 갱신되므로 옮기지 않는다.
	if (Source->ParamBuffer)
	{
		Instance->ParamBuffer = RenderCommand::CreateConstantBuffer(Source->ParamBuffer->GetBufferSize());
	}

	Instance->bIsInstance = true;
	Instance->Parent = Source;

	return Instance;
}

const char* MaterialDomainToString(EMaterialDomain Domain)
{
	switch (Domain)
	{
	case EMaterialDomain::Particle: return "Particle";
	case EMaterialDomain::Decal:    return "Decal";
	case EMaterialDomain::UI:       return "UI";
	case EMaterialDomain::Surface:
	default:                        return "Surface";
	}
}

EMaterialDomain StringToMaterialDomain(const FString& Str)
{
	if (Str == "Particle") return EMaterialDomain::Particle;
	if (Str == "Decal")    return EMaterialDomain::Decal;
	if (Str == "UI")       return EMaterialDomain::UI;
	return EMaterialDomain::Surface;
}

const char* MaterialBlendModeToString(EMaterialBlendMode Mode)
{
	switch (Mode)
	{
	case EMaterialBlendMode::Masked:      return "Masked";
	case EMaterialBlendMode::Translucent: return "Translucent";
	case EMaterialBlendMode::Additive:    return "Additive";
	case EMaterialBlendMode::Opaque:
	default:                              return "Opaque";
	}
}

EMaterialBlendMode StringToMaterialBlendMode(const FString& Str)
{
	if (Str == "Masked")      return EMaterialBlendMode::Masked;
	if (Str == "Translucent") return EMaterialBlendMode::Translucent;
	if (Str == "Additive")    return EMaterialBlendMode::Additive;
	return EMaterialBlendMode::Opaque;
}

const char* MaterialShadingModelToString(EMaterialShadingModel Model)
{
	return Model == EMaterialShadingModel::Unlit ? "Unlit" : "DefaultLit";
}

EMaterialShadingModel StringToMaterialShadingModel(const FString& Str)
{
	return Str == "Unlit" ? EMaterialShadingModel::Unlit : EMaterialShadingModel::DefaultLit;
}

const char* SamplerStateToString(ESamplerState State)
{
	switch (State)
	{
	case ESamplerState::LinearWrap:        return "LinearWrap";
	case ESamplerState::PointClamp:        return "PointClamp";
	case ESamplerState::LinearWrapUClampV: return "LinearWrapUClampV";
	case ESamplerState::LinearClamp:
	default:                               return "LinearClamp";
	}
}

ESamplerState StringToSamplerState(const FString& Str)
{
	if (Str == "LinearWrap")        return ESamplerState::LinearWrap;
	if (Str == "PointClamp")        return ESamplerState::PointClamp;
	if (Str == "LinearWrapUClampV") return ESamplerState::LinearWrapUClampV;
	return ESamplerState::LinearClamp;
}

void UMaterial::CopyPropertiesFrom(const UMaterial& Source)
{
	ParamLayout = Source.ParamLayout;
	Shader = Source.Shader;
	Textures = Source.Textures;
	PSOType = Source.PSOType;
	BlendState = Source.BlendState;
	bRenderBeforeDOF = Source.bRenderBeforeDOF;
	bTwoSided = Source.bTwoSided;
	DepthStencilState = Source.DepthStencilState;
	SamplerState = Source.SamplerState;

	MaterialDomain = Source.MaterialDomain;
	BlendMode = Source.BlendMode;
	ShadingModel = Source.ShadingModel;
	OpacityMaskClipValue = Source.OpacityMaskClipValue;

	BaseColor = Source.BaseColor;
	SpecularColor = Source.SpecularColor;
	Shininess = Source.Shininess;
	UVScrollSpeed = Source.UVScrollSpeed;
	bFlipNormalY = Source.bFlipNormalY;
}

void UMaterial::SyncLegacyStates()
{
	// 파티클/빌보드/데칼 등 전용 PSO는 PSOType이 기준이다
	if (!UsesMeshPSO()) return;

	switch (BlendMode)
	{
	case EMaterialBlendMode::Translucent:
		PSOType = EPSOType::StaticMesh_Translucent;
		BlendState = EBlendState::AlphaBlend;
		DepthStencilState = EDepthStencilState::ReadOnly;
		break;
	case EMaterialBlendMode::Additive:
		PSOType = EPSOType::StaticMesh_Additive;
		BlendState = EBlendState::Additive;
		DepthStencilState = EDepthStencilState::ReadOnly;
		break;
	case EMaterialBlendMode::Masked:
	case EMaterialBlendMode::Opaque:
	default:
		PSOType = EPSOType::StaticMesh_Opaque;
		BlendState = EBlendState::Opaque;
		DepthStencilState = EDepthStencilState::Default;
		break;
	}
}

void UMaterial::SyncBlendModeFromLegacy()
{
	switch (PSOType)
	{
	case EPSOType::StaticMesh_Translucent:
	case EPSOType::Particle_AlphaBlend:
	case EPSOType::ParticleSprite_AlphaBlend:
	case EPSOType::ParticleTrail_AlphaBlend:
		BlendMode = EMaterialBlendMode::Translucent;
		break;
	case EPSOType::StaticMesh_Additive:
	case EPSOType::Particle_Additive:
	case EPSOType::ParticleSprite_Additive:
	case EPSOType::ParticleTrail_Additive:
		BlendMode = EMaterialBlendMode::Additive;
		break;
	case EPSOType::StaticMesh_Opaque:
		// 에디터 블렌드 콤보는 BlendState만 바꿀 수도 있다
		if (BlendState == EBlendState::AlphaBlend)
			BlendMode = EMaterialBlendMode::Translucent;
		else if (BlendMode != EMaterialBlendMode::Masked)
			BlendMode = EMaterialBlendMode::Opaque;
		break;
	default:
		break;
	}

	switch (PSOType)
	{
	case EPSOType::Particle_AlphaBlend:
	case EPSOType::Particle_Additive:
	case EPSOType::ParticleSprite_AlphaBlend:
	case EPSOType::ParticleSprite_Additive:
	case EPSOType::ParticleTrail_AlphaBlend:
	case EPSOType::ParticleTrail_Additive:
		MaterialDomain = EMaterialDomain::Particle;
		break;
	case EPSOType::Decal:
	case EPSOType::DecalAdditive:
		MaterialDomain = EMaterialDomain::Decal;
		break;
	default:
		break;
	}
}

UTexture2D* UMaterial::GetTexture(int32 Slot) const
{
	return Slot >= 0 && Slot < Textures.Num() ? Textures[Slot] : nullptr;
}

void UMaterial::SetTexture(int32 Slot, UTexture2D* Texture)
{
	if (Slot < 0) return;
	while (Textures.Num() <= Slot)
		Textures.Add(nullptr);
	Textures[Slot] = Texture;
}

const UMaterial* UMaterial::GetBaseAsset() const
{
	for (const UMaterial* Current = this; Current; Current = Current->Parent)
	{
		if (!Current->GetPath().empty())
		{
			return Current;
		}
	}
	return nullptr;
}

// Material.cpp
void UMaterial::SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterial*& Material)
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	// ── 에셋 참조 ──
	// 저장과 불러오기가 같은 순서로 같은 함수를 부르게 TryEnterField를 쓴다 (바이너리 포맷에서도 맞도록)
	const bool bIsAsset = Ar.IsSaving() && Material && !Material->GetPath().empty();
	if (TOptional<FStructuredArchive::FSlot> AssetSlot = Record.TryEnterField("Asset", bIsAsset))
	{
		FString Path = Ar.IsSaving() ? Material->GetPath() : FString();
		AssetSlot.GetValue() << Path;
		if (Ar.IsLoading())
			Material = UAssetManager::GetAssetByPath<UMaterial>(Path);
		return;
	}

	// ── 인스턴스 ──
	const bool bIsInstance = Ar.IsSaving() && Material != nullptr;
	TOptional<FStructuredArchive::FSlot> BaseSlot = Record.TryEnterField("Base", bIsInstance);
	if (!BaseSlot.IsSet())
	{
		if (Ar.IsLoading())
			Material = nullptr;     // 빈 Record = 머티리얼 없음
		return;
	}

	FString BasePath;
	if (Ar.IsSaving())
	{
		const UMaterial* Base = Material->GetBaseAsset();
		BasePath = Base ? Base->GetPath() : FString("DefaultMaterial");
	}
	BaseSlot.GetValue() << BasePath;

	if (Ar.IsLoading())
	{
		UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>(BasePath);
		if (!Base)
		{
			HTR_LOG(Warning, "Load: base material '{}' not found, using DefaultMaterial", BasePath);
			Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		}
		Material = CreateInstance(Base);   // 먼저 만들고
	}

	if (Material)
		Material->Serialize(Record.EnterRecord("Instance"));   // 그다음 값을 채운다
}

void UMaterial::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties" (리플렉션 프로퍼티)

	FArchive& Ar = Record.GetUnderlyingArchive();

	// 저장 직전에 실제로 그리는 상태(PSOType/BlendState)에서 BlendMode를 맞춘다.
	// 에디터 디테일 패널은 예전 필드만 바꾸므로, 그대로 저장하면 BlendMode가 어긋난다
	if (Ar.IsSaving())
		SyncBlendModeFromLegacy();

	// ── 머티리얼 상태 속성. 예전 씬 파일에는 없으므로 TryEnterField로 읽는다 ──
	bool bHasExplicitBlendMode = false;
	if (TOptional<FStructuredArchive::FSlot> DomainSlot = Record.TryEnterField("MaterialDomain", true))
	{
		FString Domain = Ar.IsSaving() ? FString(MaterialDomainToString(MaterialDomain)) : FString();
		DomainSlot.GetValue() << Domain;
		if (Ar.IsLoading())
			MaterialDomain = StringToMaterialDomain(Domain);
	}
	if (TOptional<FStructuredArchive::FSlot> BlendModeSlot = Record.TryEnterField("BlendMode", true))
	{
		FString Mode = Ar.IsSaving() ? FString(MaterialBlendModeToString(BlendMode)) : FString();
		BlendModeSlot.GetValue() << Mode;
		if (Ar.IsLoading())
		{
			BlendMode = StringToMaterialBlendMode(Mode);
			bHasExplicitBlendMode = true;
		}
	}
	if (TOptional<FStructuredArchive::FSlot> ShadingSlot = Record.TryEnterField("ShadingModel", true))
	{
		FString Model = Ar.IsSaving() ? FString(MaterialShadingModelToString(ShadingModel)) : FString();
		ShadingSlot.GetValue() << Model;
		if (Ar.IsLoading())
			ShadingModel = StringToMaterialShadingModel(Model);
	}
	if (TOptional<FStructuredArchive::FSlot> ClipSlot = Record.TryEnterField("OpacityMaskClipValue", true))
	{
		ClipSlot.GetValue() << OpacityMaskClipValue;
	}

	Record 	<< SA_VALUE("BaseColor", BaseColor)
			<< SA_VALUE("SpecularColor", SpecularColor)
			<< SA_VALUE("Shininess", Shininess)
			<< SA_VALUE("UVScrollSpeed", UVScrollSpeed)
			<< SA_VALUE("FlipNormalY", bFlipNormalY);

	// enum은 이름 문자열로 (UE 텍스트 포맷도 enum을 이름으로 쓴다)
	FString Sampler = SamplerStateToString(SamplerState);
	Record << SA_VALUE("SamplerState", Sampler);
	if (Ar.IsLoading())
		SamplerState = StringToSamplerState(Sampler);

	// 예전 필드. 씬 파일 호환을 위해 계속 쓰고 읽는다(PSOType 번호는 EPSOType 순서가 밀리지 않아야 한다)
	FString Blend = BlendState == EBlendState::AlphaBlend ? "AlphaBlend"
		: BlendState == EBlendState::Additive ? "Additive" : "Opaque";
	Record << SA_VALUE("BlendState", Blend);
	if (Ar.IsLoading())
		BlendState = Blend == "AlphaBlend" ? EBlendState::AlphaBlend
			: Blend == "Additive" ? EBlendState::Additive : EBlendState::Opaque;

	uint8 PSOVal = static_cast<uint8>(PSOType);
	Record << SA_VALUE("PSOType", PSOVal);
	if (Ar.IsLoading())
	{
		PSOType = PSOVal < static_cast<uint8>(EPSOType::Count) ? static_cast<EPSOType>(PSOVal) : EPSOType::StaticMesh_Opaque;
		// BlendMode가 없는 예전 파일은 PSOType/BlendState에서 정하고, 있으면 BlendMode로 메시 PSO를 맞춘다
		if (bHasExplicitBlendMode)
			SyncLegacyStates();
		else
			SyncBlendModeFromLegacy();
	}

	// 텍스처: 경로 문자열 배열. 없는 칸은 빈 문자열
	int32 Num = Textures.Num();
	FStructuredArchive::FArray TextureArray = Record.EnterArray("Textures", Num);
	if (Ar.IsLoading())
		Textures.Reset();

	for (int32 i = 0; i < Num; ++i)
	{
		FString Path;
		if (Ar.IsSaving() && Textures[i])
			Path = Textures[i]->GetPath();

		TextureArray.EnterElement() << Path;

		if (Ar.IsLoading())
			Textures.Add(Path.empty() ? nullptr : FindOrLoadTexture(Path));
	}
}
