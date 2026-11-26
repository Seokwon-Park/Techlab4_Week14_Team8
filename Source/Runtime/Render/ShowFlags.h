#pragma once

#include "Core/Types.h"
#include <span>
#include <string_view>

// 뷰포트에 무엇을 보여 줄지 켜고 끄는 스위치 묶음. 품질이나 방식이 아니라 "보이느냐"만 담는다.
// 플래그 목록은 Render/ShowFlagsValues.h 한 곳에서 정의한다.

enum class EShowFlagInitMode : uint8
{
	Game,
	Editor,
};

enum class EShowFlagCategory : uint8
{
	Common,
	Physics,
	PostProcess,
	Developer,
	Render,
	Hidden,
};

struct FEngineShowFlags;

// 플래그 하나의 메타데이터. 메뉴·ini·콘솔 명령처럼 플래그를 이름으로 다루는 코드가 쓴다.
struct FShowFlagInfo
{
	const char* Name;
	const char* DisplayName;
	const char* Icon;
	EShowFlagCategory Category;
	bool bGameDefault;
	bool bEditorDefault;
	bool FEngineShowFlags::* Member;
};

struct FEngineShowFlags
{
#define SHOWFLAG(Name, bGameDefault, bEditorDefault, Category, Icon, DisplayName) bool Name = bGameDefault;
#include "Render/ShowFlagsValues.h"
#undef SHOWFLAG

	FEngineShowFlags() = default;
	explicit FEngineShowFlags(EShowFlagInitMode Mode) { Init(Mode); }

	// 모든 플래그를 해당 모드의 기본값으로 되돌린다.
	void Init(EShowFlagInitMode Mode);

	// 전체 플래그 목록. 선언 순서와 같다.
	static std::span<const FShowFlagInfo> GetAll();
	// 이름으로 찾는다. 대소문자를 구분하지 않는다. 없으면 nullptr.
	static const FShowFlagInfo* Find(std::string_view Name);

	bool Get(const FShowFlagInfo& Info) const { return this->*Info.Member; }
	void Set(const FShowFlagInfo& Info, bool bValue) { this->*Info.Member = bValue; }
	// 이름으로 설정한다. 이름이 없으면 false.
	bool SetByName(std::string_view Name, bool bValue);

	bool operator==(const FEngineShowFlags&) const = default;
};

// 뷰포트가 씬을 어떤 방식으로 보여 줄지. 한 번에 하나만 고른다.
// 값의 순서를 바꾸면 안 된다. 에디터가 이 값을 정수로 설정 파일에 저장하고, 뷰 모드 콤보 상자의 항목 순서로도 쓴다.
enum EViewModeIndex
{
	// 평소 화면
	VMI_Lit,
	VMI_Wireframe,
	// 깊이를 밝기로 보여 준다.
	VMI_SceneDepth,
	// 노멀을 색으로 보여 준다.
	VMI_WorldNormal,
	// 화면 칸마다 영향을 주는 광원 수를 색으로 보여 준다. UE의 VMI_LightComplexity에 가깝다.
	VMI_LightCulling,
	// 캐스케이드 그림자의 단계를 색으로 보여 준다.
	VMI_Cascade,
	// 화면 크기 그림자 마스크를 보여 준다.
	VMI_ShadowMask,
};
