#include "EnginePCH.h"
#include "RectLightActor.h"
#include "Asset/AssetManager.h"

ARectLightActor::ARectLightActor()
{
    RectLightComponent = CreateDefaultSubobject<URectLightComponent>("URectLightComponent");
	SetupLightRoot(RectLightComponent, "Engine/Icons/Rect.png");

    // Icon 옆에 붙어서 빛의 방향을 알려주는 기즈모 arrow (로컬 x축)
    ArrowComponent = CreateDefaultSubobject<UStaticMeshComponent>("UArrowComponent");
    ArrowComponent->SetEditorIcon(true);
    ArrowComponent->SetupAttachment(RectLightComponent);
    ArrowComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
    ArrowComponent->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
    ArrowComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
    ArrowComponent->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.6f));   // 길이 2.4: 아이콘(반지름 1) 밖으로 1.4만큼 나온다
    ArrowComponent->SetHiddenInDetails(true);
    ArrowComponent->SetCastShadow(false);
    ArrowComponent->SetReceiveShadow(false);

}