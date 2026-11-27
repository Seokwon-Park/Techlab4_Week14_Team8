#include "EnginePCH.h"
#include "SpotLightActor.h"
#include "Asset/AssetManager.h"

ASpotLightActor::ASpotLightActor()
{
    SpotLightComponent = CreateDefaultSubobject<USpotLightComponent>("USpotLightComponent");
	SetupLightRoot(SpotLightComponent, "Engine/Icons/Spot.png");

    // Icon 옆에 붙어서 빛의 방향을 알려주는 기즈모 arrow (로컬 x축)
    ArrowComponent = CreateDefaultSubobject<UStaticMeshComponent>("UArrowComponent");
    ArrowComponent->SetEditorIcon(true);
    ArrowComponent->SetupAttachment(SpotLightComponent);
    ArrowComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
    ArrowComponent->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
    ArrowComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
    ArrowComponent->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.6f));   // 길이 2.4: 아이콘(반지름 1) 밖으로 1.4만큼 나온다
    ArrowComponent->SetHiddenInDetails(true);
    ArrowComponent->SetCastShadow(false);
    ArrowComponent->SetReceiveShadow(false);
}
