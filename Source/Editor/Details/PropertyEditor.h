#pragma once

#include <functional>

struct ImFont;
class UObject;
class UStruct;
class FProperty;
class AActor;
class UActorComponent;

namespace FPropertyEditor
{
	// DetailsPanel.cpp의 DrawProperty를 그대로 옮긴다 (값이 바뀌면 PostEditChangeProperty 호출).
	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont = nullptr);
	// 기반 클래스부터 CPF_Edit 속성을 CollapsingHeader로 묶어 그린다 (DetailsPanel의 루프를 옮김).
	// Filter가 비어 있지 않으면 이름에 그 글자가 들어간 속성만 그린다 (Details 검색 칸).
	void DrawObjectProperties(UObject* Object, ImFont* CustomFont = nullptr, const char* Filter = nullptr);

	// Details 패널이 프로퍼티 아래에 따로 그리는 칸들. Blueprint 에디터도 같은 UI를 쓴다.
	// 액터: Lua 스크립트 칸. 컴포넌트: 스켈레탈 메시, 애님 블루프린트, 애니메이션, 머티리얼 슬롯.
	void DrawActorExtras(AActor* Actor);
	void DrawComponentExtras(UActorComponent* Component);
	// Add Component 팝업 내용 (분류와 검색). BeginPopup 안에서 부른다. 붙인 컴포넌트를 돌려준다.
	UActorComponent* DrawAddComponentMenu(AActor* Actor, char* Search, size_t SearchSize);

	// USTRUCT 멤버 표시 규칙 (UE의 EditCondition / IPropertyTypeCustomization을 줄인 것).
	// 그 구조체를 그릴 때 멤버마다 불러, 숨길 멤버는 bVisible = false, 라벨을 바꾸려면 OutLabel을 채운다.
	// StructValue는 그리고 있는 구조체 값의 주소다 (예: FRawDistributionFloat*).
	struct FStructMemberDisplay
	{
		bool bVisible = true;
		const char* Label = nullptr;   // nullptr이면 속성 이름
	};
	using FStructMemberRule = std::function<FStructMemberDisplay(const void* StructValue, const FProperty& Member)>;
	void RegisterStructMemberRule(const UStruct* Struct, FStructMemberRule Rule);

	// 클래스 이름을 담는 FString 프로퍼티를 BaseClass와 그 자식(Blueprint 포함) 중에서 고르는 콤보로 그린다.
	// UE의 TSubclassOf<BaseClass> 칸에 해당한다. OwnerClass의 PropertyName을 찾아 등록한다.
	void RegisterClassPicker(class UClass* OwnerClass, const char* PropertyName, class UClass* BaseClass);
}
// DetailsPanel.cpp는 이 헤더를 include하고 원래 함수 대신 FPropertyEditor::를 부르도록 바꾼다.
