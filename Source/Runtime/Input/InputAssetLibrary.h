#pragma once

#include "Core/EngineString.h"

class UInputAction;
class UInputMappingContext;

// 입력 에셋(JSON) 로더. UE에서 IA_ / IMC_ 에셋을 콘텐츠 브라우저에 두고 참조하는 것과 같은 역할이다.
//   *.inputaction  : UInputAction        (ValueType, ConsumeInput, Modifiers, Triggers)
//   *.inputcontext : UInputMappingContext (Mappings: Action 경로 + Key + Modifiers + Triggers)
//
// 같은 경로는 한 객체를 공유한다(캐시). 파일이 바뀌었으면 다음 Load에서 같은 객체에 다시 읽으므로
// 바인딩에 쓰던 포인터는 그대로 유효하다. 캐시된 객체는 프로그램 종료까지 살아 있다.
class FInputAssetLibrary
{
public:
	FInputAssetLibrary() = delete;

	static constexpr const char* ActionExtension = ".inputaction";
	static constexpr const char* ContextExtension = ".inputcontext";

	// 경로는 "Assets/Input/IA_Move.inputaction" 또는 Assets 상대 키 "Input/IA_Move.inputaction"
	// 실패하면 로그를 남기고 nullptr
	static UInputAction* LoadInputAction(const FString& Path);
	static UInputMappingContext* LoadMappingContext(const FString& Path);

	// 키 리바인딩 결과 저장이나 에디터에서 쓰려고 둔다. 저장한 경로로 캐시도 갱신된다
	static bool SaveInputAction(UInputAction& Action, const FString& Path);
	static bool SaveMappingContext(UInputMappingContext& Context, const FString& Path);

	// ---- 에디터용 ----
	static bool IsInputActionPath(const FString& Path);
	static bool IsMappingContextPath(const FString& Path);

	// 저장될 JSON 문자열. 에디터가 저장된 상태와 비교해 * 표시를 정한다
	static FString SerializeInputAction(const UInputAction& Action);
	static FString SerializeMappingContext(const UInputMappingContext& Context);

	// 빈 에셋 파일을 만든다 (콘텐츠 드로어의 "새로 만들기")
	static bool CreateDefaultInputAction(const FString& Path, FString& OutError);
	static bool CreateDefaultMappingContext(const FString& Path, FString& OutError);

	// 캐시된 객체를 디스크 내용으로 되돌린다 (저장하지 않고 닫을 때). 같은 객체를 유지한다
	static void ReloadFromDisk(const FString& Path);
};
