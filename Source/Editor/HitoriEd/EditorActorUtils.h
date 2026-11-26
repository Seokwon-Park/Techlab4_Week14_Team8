#pragma once

#include "Core/Types.h"

class AActor;

namespace FEditorActorUtils
{
	// 액터 이름을 바꾼다. Outliner와 Details 패널이 함께 쓴다.
	// 앞뒤 공백은 지우고, 비었거나 같은 레벨에 같은 이름이 있으면 바꾸지 않고 false를 돌려준다.
	// 에디터 월드의 액터만 바꾼다. PIE 액터는 끝나면 사라지므로 바꿔도 남지 않는다.
	// 이 엔진은 UE의 액터 레이블(표시 이름)이 따로 없어서 오브젝트 이름 자체를 바꾼다.
	// 그래서 이름으로 액터를 찾는 곳(시퀀서 바인딩, Lua GetActorByName)은 새 이름을 따라오지 않는다.
	bool RenameActor(AActor* Actor, const FString& NewName);

	// Child를 Parent에 붙인다. 월드 위치는 그대로 두고, 폴더도 부모와 같게 맞춘다 (UE와 같다).
	// 에디터 월드의 액터끼리만 붙이고, 순환이면 로그만 남기고 false를 돌려준다.
	bool AttachActor(AActor* Child, AActor* Parent);
	// 다른 액터에 붙어 있으면 월드 위치를 그대로 두고 뗀다.
	bool DetachActor(AActor* Child);
	// Candidate가 Actor 자신이거나 Actor에 (몇 단계든) 붙어 있는 자식인가. 붙일 대상 목록에서 뺄 때 쓴다.
	bool IsSelfOrAttachedDescendant(const AActor* Candidate, const AActor* Actor);
}
