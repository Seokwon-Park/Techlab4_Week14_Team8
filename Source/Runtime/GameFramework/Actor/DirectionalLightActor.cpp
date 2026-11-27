#include "EnginePCH.h"
#include "DirectionalLightActor.h"
#include "Asset/AssetManager.h"

ADirectionalLightActor::ADirectionalLightActor()
{
    DirectionalLightComponent = CreateDefaultSubobject<UDirectionalLightComponent>("UDirectionalLightComponent");
	SetupLightRoot(DirectionalLightComponent, "Engine/Icons/Directional.png");
    DirectionalLightComponent->SetRelativeRotation(FRotator(45.0f, 0.0f, 0.0f));

    // Icon 옆에 붙어서 빛의 방향을 알려주는 기즈모 arrow (로컬 x축)
    ArrowComponent = CreateDefaultSubobject<UStaticMeshComponent>("UArrowComponent");
    ArrowComponent->SetEditorIcon(true);
    ArrowComponent->SetupAttachment(DirectionalLightComponent);
    ArrowComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
    ArrowComponent->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
    ArrowComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
    ArrowComponent->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.6f));   // 길이 2.4: 아이콘(반지름 1) 밖으로 1.4만큼 나온다
    ArrowComponent->SetHiddenInDetails(true);
    ArrowComponent->SetCastShadow(false);
    ArrowComponent->SetReceiveShadow(false);

}