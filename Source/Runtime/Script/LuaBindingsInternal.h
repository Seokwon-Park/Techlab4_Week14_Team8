#pragma once

#include <sol/sol.hpp>

class AActor;
class UWorld;
struct FLuaActorRef;
struct FLuaWidgetRef;

// LuaBindings*.cpp 분할 파일들이 공유하는 내부 헬퍼 및 등록 함수
namespace LuaBindingsDetail
{
	AActor* OwnerFromEnv(sol::this_environment& ThisEnv);
	UWorld* WorldFromEnv(sol::this_environment& ThisEnv, const char* FuncName);
	void AddPendingDestroy(const FLuaActorRef& Ref);
	void ReleaseAnimGraphBindings(AActor* Owner);

	// LuaBindings.cpp
	void RegisterAudio(sol::state& Lua);
	void RegisterGameplay(sol::state& Lua);
	void RegisterWidget(sol::state& Lua);
	void RegisterCoroutine(sol::state& Lua);

	// LuaBindingsActor.cpp
	void RegisterMath(sol::state& Lua);
	void RegisterActor(sol::state& Lua);
	void RegisterComponent(sol::state& Lua);

	// LuaBindingsInput.cpp
	void RegisterInput(sol::state& Lua);
	void RegisterCameraAction(sol::state& Lua);

	// LuaBindingsAnimation.cpp
	void RegisterAnimation(sol::state& Lua);
}
