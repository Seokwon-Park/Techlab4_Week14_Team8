#pragma once

#include "Core/Types.h"

class AActor;
class UWorld;

// 레벨 에디터의 액터 복사, 붙여넣기, 복제. 액터 하나를 씬 파일과 같은 JSON 형식으로 바꿔 시스템 클립보드에 넣는다.
// 그래서 에디터를 껐다 켜거나 다른 씬을 열어도 붙여넣을 수 있다. UE의 edactCopySelected/edactPasteSelected와 같은 자리다.
// 다른 액터를 가리키는 참조는 그대로 복사되므로 붙여넣은 액터도 원본이 가리키던 대상을 가리킨다.
namespace FActorClipboard
{
	// 액터를 텍스트로 만든다. 실패하면 false.
	bool ExportActor(AActor* Actor, FString& OutText);

	// ExportActor가 만든 텍스트로 World에 새 액터를 만든다. 이름은 겹치지 않게 새로 받는다. 실패하면 nullptr.
	AActor* ImportActor(UWorld* World, const FString& Text);

	// 이 형식의 텍스트처럼 보이는지 빠르게 확인한다. 메뉴 항목을 켤지 정할 때 쓴다.
	bool LooksLikeActorText(const char* Text);

	// 시스템 클립보드를 쓴다.
	bool CopyToClipboard(AActor* Actor);
	AActor* PasteFromClipboard(UWorld* World);

	// 클립보드를 건드리지 않고 같은 자리에 하나 더 만든다.
	AActor* Duplicate(AActor* Actor, UWorld* World);
}
