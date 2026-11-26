#pragma once

#include <vector>

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Input/KeyCode.h"

// UE의 FKey에 해당한다. 키보드 / 마우스 버튼 / 마우스 축을 하나의 코드로 다룬다.
// 0~255 : Win32 가상 키 코드(EKeyCode). 마우스 버튼도 VK_LBUTTON 등으로 이 범위에 있다.
// 256~  : 마우스 축처럼 눌림이 아니라 "이번 프레임 이동량"을 값으로 갖는 키
struct FKey
{
	static constexpr int32 AxisKeyBegin = 256;

	int32 Code = 0;

	constexpr FKey() = default;
	constexpr explicit FKey(int32 InCode) : Code(InCode) {}
	constexpr FKey(EKeyCode InKey) : Code(static_cast<int32>(InKey)) {}

	constexpr bool IsValid() const { return Code > 0; }
	constexpr bool IsAxis() const { return Code >= AxisKeyBegin; }

	constexpr bool operator==(const FKey& Other) const { return Code == Other.Code; }
	constexpr bool operator!=(const FKey& Other) const { return Code != Other.Code; }
};

namespace EKeys
{
	inline constexpr FKey Invalid{};

	inline constexpr FKey LeftMouseButton{ EKeyCode::LButton };
	inline constexpr FKey RightMouseButton{ EKeyCode::RButton };
	inline constexpr FKey MiddleMouseButton{ EKeyCode::MButton };

	inline constexpr FKey MouseX{ FKey::AxisKeyBegin + 0 };
	inline constexpr FKey MouseY{ FKey::AxisKeyBegin + 1 };
	inline constexpr FKey MouseWheel{ FKey::AxisKeyBegin + 2 };

	// 원시 입력 버퍼 크기 (키 256 + 축 3)
	inline constexpr int32 NumKeys = FKey::AxisKeyBegin + 3;
}

// 에셋(JSON)에서 쓰는 키 이름 <-> FKey. 대소문자를 구분하지 않는다.
// 예: "W", "1", "Space", "LeftShift", "F5", "NumPad3", "LeftMouseButton", "MouseX"
namespace FInputKeyNames
{
	FKey FromString(const FString& Name);
	// 표에 없는 코드는 "Key_0x41"처럼 16진수로 돌려준다 (FromString이 다시 읽을 수 있다)
	FString ToString(FKey Key);
	// 에디터 키 선택 목록용. 영문, 숫자, F1~F12, 넘패드, 이름 있는 키, 마우스 순
	const std::vector<FKey>& GetAllKeys();
}
