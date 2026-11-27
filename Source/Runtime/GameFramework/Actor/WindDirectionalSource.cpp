#include "EnginePCH.h"
#include "GameFramework/Actor/WindDirectionalSource.h"
#include "Asset/AssetManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WindDirectionalSourceComponent.h"

AWindDirectionalSource::AWindDirectionalSource()
{
	// 액터의 회전을 풍향으로 사용하는 루트
	auto* Wind = CreateDefaultSubobject<UWindDirectionalSourceComponent>("WindComponent");
	SetRootComponent(Wind);
	SetActorEnableCollision(false);

	// 기본 화살표 메시의 +Z를 바람의 로컬 +X에 정렬
	auto* Arrow = CreateDefaultSubobject<UStaticMeshComponent>("WindArrow");
	Arrow->SetupAttachment(Wind);
	Arrow->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
	Arrow->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
	Arrow->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	Arrow->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.6f));   // 길이 2.4: 아이콘(반지름 1) 밖으로 1.4만큼 나온다

	// 레벨 편집용 표시와 게임플레이의 분리
	Arrow->SetEditorIcon(true);
	Arrow->SetHiddenInDetails(true);
	Arrow->SetHiddenInGame(true);
	Arrow->SetCastShadow(false);
	Arrow->SetReceiveShadow(false);
	Arrow->bGenerateOverlapEvents = false;
}
