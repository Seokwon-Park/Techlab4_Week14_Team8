#include "EnginePCH.h"
#include "Input/InputCoreTypes.h"

#include <cctype>
#include <cstdlib>
#include <utility>
#include <algorithm>

namespace
{
	FString ToLower(FString Text)
	{
		for (char& C : Text) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
		return Text;
	}

	// 대표 이름이 앞에 오도록 둔다. ToString은 처음 찾은 이름을 쓴다
	const std::vector<std::pair<const char*, FKey>>& GetNamedKeys()
	{
		static const std::vector<std::pair<const char*, FKey>> Keys = {
			{ "LeftMouseButton", EKeys::LeftMouseButton },
			{ "RightMouseButton", EKeys::RightMouseButton },
			{ "MiddleMouseButton", EKeys::MiddleMouseButton },
			{ "ThumbMouseButton", FKey(0x05) },
			{ "ThumbMouseButton2", FKey(0x06) },
			{ "MouseX", EKeys::MouseX },
			{ "MouseY", EKeys::MouseY },
			{ "MouseWheel", EKeys::MouseWheel },

			{ "Backspace", EKeyCode::Backspace },
			{ "Tab", EKeyCode::Tab },
			{ "Enter", EKeyCode::Enter },
			{ "Shift", EKeyCode::Shift },
			{ "Control", EKeyCode::Control },
			{ "Ctrl", EKeyCode::Control },
			{ "Alt", EKeyCode::Alt },
			{ "Pause", EKeyCode::Pause },
			{ "CapsLock", EKeyCode::CapsLock },
			{ "Escape", EKeyCode::Escape },
			{ "Esc", EKeyCode::Escape },
			{ "Space", EKeyCode::Space },
			{ "SpaceBar", EKeyCode::Space },
			{ "PageUp", EKeyCode::PageUp },
			{ "PageDown", EKeyCode::PageDown },
			{ "End", EKeyCode::End },
			{ "Home", EKeyCode::Home },
			{ "Left", EKeyCode::Left },
			{ "Up", EKeyCode::Up },
			{ "Right", EKeyCode::Right },
			{ "Down", EKeyCode::Down },
			{ "PrintScreen", EKeyCode::PrintScreen },
			{ "Insert", EKeyCode::Insert },
			{ "Delete", EKeyCode::Delete },

			{ "Multiply", EKeyCode::Multiply },
			{ "Add", EKeyCode::Add },
			{ "Subtract", EKeyCode::Subtract },
			{ "Decimal", EKeyCode::Decimal },
			{ "Divide", EKeyCode::Divide },
			{ "NumLock", EKeyCode::NumLock },
			{ "ScrollLock", EKeyCode::ScrollLock },

			{ "LeftShift", EKeyCode::LShift },
			{ "RightShift", EKeyCode::RShift },
			{ "LeftControl", EKeyCode::LControl },
			{ "RightControl", EKeyCode::RControl },
			{ "LeftAlt", EKeyCode::LAlt },
			{ "RightAlt", EKeyCode::RAlt },

			{ "Semicolon", EKeyCode::Semicolon },
			{ "Equals", EKeyCode::Equals },
			{ "Comma", EKeyCode::Comma },
			{ "Minus", EKeyCode::Minus },
			{ "Period", EKeyCode::Period },
			{ "Slash", EKeyCode::Slash },
			{ "Tilde", EKeyCode::Tilde },
			{ "LeftBracket", EKeyCode::LeftBracket },
			{ "Backslash", EKeyCode::Backslash },
			{ "RightBracket", EKeyCode::RightBracket },
			{ "Quote", EKeyCode::Quote },
		};
		return Keys;
	}
}

FKey FInputKeyNames::FromString(const FString& Name)
{
	if (Name.empty()) return EKeys::Invalid;

	// 한 글자 영문/숫자 -> 가상 키 코드와 같다 ('A' = 0x41, '0' = 0x30)
	if (Name.size() == 1 && std::isalnum(static_cast<unsigned char>(Name[0])))
		return FKey(std::toupper(static_cast<unsigned char>(Name[0])));

	const FString Lower = ToLower(Name);

	// F1 ~ F12
	if (Lower.size() >= 2 && Lower.size() <= 3 && Lower[0] == 'f' && std::isdigit(static_cast<unsigned char>(Lower[1])))
	{
		const int32 N = std::atoi(Lower.c_str() + 1);
		if (N >= 1 && N <= 12) return FKey(static_cast<int32>(EKeyCode::F1) + N - 1);
	}
	// NumPad0 ~ NumPad9
	if (Lower.size() == 7 && Lower.rfind("numpad", 0) == 0 && std::isdigit(static_cast<unsigned char>(Lower[6])))
		return FKey(static_cast<int32>(EKeyCode::NumPad0) + (Lower[6] - '0'));
	// Key_0x41 (ToString이 이름 없는 키에 쓰는 형식)
	if (Lower.rfind("key_0x", 0) == 0)
	{
		const int32 Code = static_cast<int32>(std::strtol(Lower.c_str() + 6, nullptr, 16));
		if (Code > 0 && Code < EKeys::NumKeys) return FKey(Code);
	}

	for (const auto& [KeyName, Key] : GetNamedKeys())
	{
		if (ToLower(KeyName) == Lower) return Key;
	}
	return EKeys::Invalid;
}

FString FInputKeyNames::ToString(FKey Key)
{
	const int32 Code = Key.Code;
	if ((Code >= 'A' && Code <= 'Z') || (Code >= '0' && Code <= '9'))
		return FString(1, static_cast<char>(Code));
	if (Code >= static_cast<int32>(EKeyCode::F1) && Code <= static_cast<int32>(EKeyCode::F12))
		return "F" + std::to_string(Code - static_cast<int32>(EKeyCode::F1) + 1);
	if (Code >= static_cast<int32>(EKeyCode::NumPad0) && Code <= static_cast<int32>(EKeyCode::NumPad9))
		return "NumPad" + std::to_string(Code - static_cast<int32>(EKeyCode::NumPad0));

	for (const auto& [KeyName, NamedKey] : GetNamedKeys())
	{
		if (NamedKey == Key) return KeyName;
	}

	char Buffer[16];
	std::snprintf(Buffer, sizeof(Buffer), "Key_0x%02X", Code);
	return Buffer;
}

const std::vector<FKey>& FInputKeyNames::GetAllKeys()
{
	static const std::vector<FKey> Keys = []
	{
		std::vector<FKey> Out;
		for (int32 C = 'A'; C <= 'Z'; ++C) Out.push_back(FKey(C));
		for (int32 C = '0'; C <= '9'; ++C) Out.push_back(FKey(C));
		for (int32 C = static_cast<int32>(EKeyCode::F1); C <= static_cast<int32>(EKeyCode::F12); ++C) Out.push_back(FKey(C));
		for (int32 C = static_cast<int32>(EKeyCode::NumPad0); C <= static_cast<int32>(EKeyCode::NumPad9); ++C) Out.push_back(FKey(C));
		for (const auto& [Name, Key] : GetNamedKeys())
		{
			// 별칭(Ctrl, Esc, SpaceBar)은 같은 키가 이미 들어 있으므로 건너뛴다
			if (std::find(Out.begin(), Out.end(), Key) == Out.end()) Out.push_back(Key);
		}
		return Out;
	}();
	return Keys;
}
