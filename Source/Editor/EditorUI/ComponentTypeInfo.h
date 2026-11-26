#pragma once

#include "Editor/EditorUI/EditorIcons.h"
#include "Core/Types.h"

class UClass;
class AActor;

// 컴포넌트와 액터 종류별 아이콘, 보기 좋은 이름, Add 메뉴 분류. Details 패널과 Outliner가 함께 쓴다.
namespace ComponentTypeInfo
{
	// UE5 Add 메뉴처럼 자주 쓰는 컴포넌트를 분류해 둔다. 여기 없는 컴포넌트는 Other에 들어간다.
	struct FEntry
	{
		const char* ClassName;
		const char* Category;
		EEditorIcon Icon;
		ImU32 Color;
	};

	// 목록 전체와 개수. Add 메뉴는 이 순서대로 보여 준다.
	const FEntry* GetEntries(int32& OutCount);
	// Add 메뉴의 분류 순서. 마지막은 "Other"다.
	const char* const* GetCategories(int32& OutCount);

	// 클래스 이름이 정확히 같은 항목. 없으면 nullptr.
	const FEntry* FindExactEntry(const FString& ClassName);
	// 직접 붙이는 용도가 아닌 부모 클래스인가 (UPrimitiveComponent 등). Add 메뉴에 내놓지 않는다.
	bool IsAbstractComponentClass(const FString& ClassName);

	// 클래스나 가장 가까운 부모에 맞는 아이콘. 게임에서 만든 메시 컴포넌트도 메시 아이콘을 받는다.
	void GetComponentIcon(const UClass* Class, EEditorIcon& OutIcon, ImU32& OutColor);
	// 액터를 대표하는 아이콘. 라이트, 메시, 카메라처럼 액터가 무엇인지 드러내는 컴포넌트를 찾아 쓴다.
	void GetActorIcon(const AActor* Actor, EEditorIcon& OutIcon, ImU32& OutColor);

	// "UStaticMeshComponent" -> "Static Mesh". 앞의 U와 뒤의 Component를 떼고 단어를 띄운다.
	FString GetComponentDisplayName(const FString& ClassName);
}
