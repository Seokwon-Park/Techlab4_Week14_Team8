#pragma once

#include <sol/forward.hpp>
#include "Script/CoroutineScheduler.h"

class AActor;
class UWorld;

// 엔진 API를 Lua에 노출한다. 바인딩 구현은 LuaBindings*.cpp에 도메인별로 나뉘어 있다(LuaBindingsInternal.h 참고). 담당: B, 소비자: A
namespace FLuaBindings
{
	// FVector, FRotator, AActor(안전 핸들), Input, Time
	void Register(sol::state& Lua);

	// Lua에서 Destroy를 부른 액터를 실제로 지운다. UWorld::Tick이 UpdateOverlaps() 직후 1회 호출.
	void FlushPendingDestroy(UWorld* World);

	// 해당 액터의 Lua 콜백을 전부 푼다.
	void ReleaseCallbacks(AActor* Owner);
}
