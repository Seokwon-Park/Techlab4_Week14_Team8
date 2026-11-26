#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"

#include <filesystem>
#include <memory>

class UClass;
class UObject;
class AActor;
class UActorComponent;
struct FBPGraphSet;

// 데이터 Blueprint. UE의 Data Only Blueprint와 같은 자리다.
//
// .bp 에셋 하나가 클래스 하나다. 부모 클래스(C++ 클래스나 다른 BP)를 잇고, 기본값과 컴포넌트 구성만 바꾼다.
// 로직은 Lua로 붙인다 (기본값의 ScriptPath).
//
//   { "Version": 1, "ParentClass": "AStaticMeshActor",
//     "Defaults": { AActor::Serialize와 같은 형식 (프로퍼티 + 컴포넌트) } }
//
// 부모는 액터(Actor Blueprint) 또는 액터 컴포넌트(Component Blueprint)다. 컴포넌트 BP의 Defaults는
// 컴포넌트 하나의 Serialize 형식이고, AActor::AddComponent가 만든 컴포넌트에 적용한다. 노드 그래프는 액터 BP만 갖는다.
//
// 엔진이 시작할 때 Assets 아래 .bp를 모두 읽어 파일 이름("BP_Door")으로 UClass를 만들어 등록한다.
// 그래서 FindClass, SpawnActor, 클래스 목록, 씬 파일이 BP 클래스를 C++ 클래스처럼 쓴다.
// BP 클래스는 부모의 생성자와 크기를 그대로 쓰고, 만든 뒤에 기본값을 덮어쓴다.
namespace FBlueprint
{
	constexpr const char* Extension = ".bp";
	constexpr int32 FileVersion = 1;

	bool IsBlueprintPath(const FString& Path);

	// AssetRoot 아래 .bp를 모두 읽어 클래스를 등록한다. 엔진 시작 때 한 번 부른다.
	void RegisterAll(const std::filesystem::path& AssetRoot);

	// .bp 하나를 읽어 클래스를 등록한다. 이미 등록된 BP면 기본값만 새로 읽는다 (부모는 바꾸지 않는다).
	// 실패하면 nullptr이고 이유를 OutError에 쓴다.
	UClass* LoadBlueprint(const std::filesystem::path& File, FString* OutError = nullptr);

	// BP 부모가 될 수 있는 클래스인가: 추상이 아닌 AActor 또는 UActorComponent 계열.
	bool IsValidParentClass(const UClass* Class);

	// Template(액터 또는 컴포넌트)의 지금 상태를 기본값으로 삼아 .bp를 쓰고 다시 읽는다. Template이 없으면 부모 클래스의 CDO를 쓴다.
	// ParentClass는 IsValidParentClass여야 하고 Template은 그 클래스여야 한다. 쓴 뒤 등록된 클래스를 돌려준다.
	// Graphs가 있으면 "Graphs"(EventGraph, Construction Script)로 함께 쓰고, nullptr이면 파일에 있던 그래프를 남긴다.
	// 컴포넌트 BP는 그래프를 쓰지 않는다.
	UClass* SaveBlueprint(const std::filesystem::path& File, UClass* ParentClass, UObject* Template, FString& OutError,
		const FBPGraphSet* Graphs = nullptr);

	// BP 클래스의 노드 그래프 (BlueprintGraph.h). 그래프가 없거나 BP가 아니면 nullptr. 부모 BP는 보지 않는다.
	std::shared_ptr<const FBPGraphSet> GetGraphs(const UClass* Class);

	// BP 기본값의 문자열 프로퍼티 하나만 바꿔 .bp에 쓰고 다시 읽는다 (World Settings에서 GameMode BP 칸을 고칠 때).
	// 실패하면 false이고 이유를 OutError에 쓴다.
	bool SetDefaultStringProperty(UClass* BlueprintClass, const FString& PropertyName, const FString& Value, FString& OutError);

	bool IsBlueprintClass(const UClass* Class);
	// 부모가 UActorComponent 계열인 BP 클래스인가.
	bool IsComponentBlueprint(const UClass* Class);
	// BP 클래스의 에셋 경로(Assets 기준). BP가 아니면 빈 문자열.
	FString GetAssetPath(const UClass* Class);
	// BP가 아닌 가장 가까운 조상 (C++ 클래스). BP가 아니면 자기 자신.
	UClass* GetNativeClass(UClass* Class);

	// 스폰한 액터에 BP 기본값(프로퍼티와 컴포넌트)을 덮어쓴다. 부모 BP의 기본값부터 차례로 적용한다.
	// UWorld::SpawnActor가 부른다.
	void ApplyActorDefaults(AActor* Actor);
	// 새로 만든 컴포넌트에 컴포넌트 BP 기본값을 덮어쓴다. 부모 BP부터 차례로 적용한다. AActor::AddComponent가 부른다.
	void ApplyComponentDefaults(UActorComponent* Component);

	// ---------------- 놓인 BP 액터의 차이 저장 (UE의 delta serialization) ----------------
	// 씬에는 BP 액터를 통째로 쓰지 않고, BP 기본값과 다른 값만 쓴다. 불러올 때는 지금의 BP 기본값 위에
	// 그 차이를 얹는다. 그래서 BP를 고치면 인스턴스가 따로 바꾸지 않은 값은 새 기본값을 따라간다.
	//
	// 차이는 AActor::Serialize가 쓰는 JSON에서 계산한다.
	//   - 프로퍼티: 기본값과 다른 키만 남긴다 (구조체는 안쪽까지 비교).
	//   - 컴포넌트: 이름으로 짝지어 같은 방식으로 비교하고, BP에 없는 컴포넌트는 통째로 남긴다.
	//   - 루트 컴포넌트의 Transform은 항상 남긴다 (스폰 위치가 기본값의 위치를 덮어쓰기 때문이다).
	// 인스턴스에서 지운 BP 컴포넌트는 기록하지 않는다 (다시 열면 BP 컴포넌트가 돌아온다).

	// Actor(BP 액터)와 그 BP 기본값의 차이를 JSON 문자열로 만든다. BP 액터가 아니면 false.
	bool MakeInstanceDelta(AActor* Actor, FString& OutDelta);
	// 이미 BP 기본값이 적용된 Actor에 차이를 얹는다. MakeInstanceDelta가 만든 문자열을 받는다.
	bool ApplyInstanceDelta(AActor* Actor, const FString& Delta);
}
