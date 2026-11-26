#pragma once

#include "KeyCode.h"
#include "MouseButton.h"
#include "Core/Types.h"

enum class EGameInputAxis : uint8 { MouseX, MouseY, MouseWheel };

class FInputSystem
{
public:
    static bool IsKeyPressed(EKeyCode KeyCode);
    static bool IsKeyReleased(EKeyCode KeyCode);
    static bool IsKeyDown(EKeyCode KeyCode);

    static bool IsMouseDown(EMouseButton MouseButton);
    static bool IsMousePressed(EMouseButton MouseButton);
    static bool IsMouseReleased(EMouseButton MouseButton);

    static void UpdateInputStates();
    static void ClearAllStates();
    static void SyncMousePosition(int32 x, int32 y);
    static void OnKeyUp(int VirtualKey);
    static void OnMouseUp(EMouseButton MouseButton);
    static void OnKeyDown(int VirtualKey);
    static void OnMouseDown(EMouseButton MouseButton);
    static void OnMouseMove(int32 x, int32 y);
    static void OnMouseWheelDelta(int32 Wheel);

    // EMouseButton -> Win32 가상 키 코드 (VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2)
    static constexpr int32 MouseButtonToVirtualKey(EMouseButton Button)
    {
        constexpr int32 Table[] = { 0x01, 0x02, 0x04, 0x05, 0x06 };
        return Table[static_cast<int>(Button)];
    }
    static constexpr bool IsMouseVirtualKey(int32 Code)
    {
        return Code == 0x01 || Code == 0x02 || Code == 0x04 || Code == 0x05 || Code == 0x06;
    }

    static inline int32 GetMouseX() { return MouseX; }
    static inline int32 GetMouseY() { return MouseY; }

    static inline int32 GetMouseDeltaX() { return DeltaX; }
    static inline int32 GetMouseDeltaY() { return DeltaY; }

    static inline int32 GetWheelDelta() 
    { 
        int32 Delta = WheelDelta;
        WheelDelta = 0;
        return Delta;
    }

private:
    FInputSystem() = delete;

    static inline bool bMouseStates[5] = {};
    static inline bool bPrevMouseStates[5] = {};

    static inline bool bKeyStates[256] = {};
    static inline bool bPrevKeyStates[256] = {};

    static inline int32 MouseX = 0, MouseY = 0;
    static inline int32 PrevMouseX = 0, PrevMouseY = 0;
    static inline int32 DeltaX = 0, DeltaY = 0;

    static inline int32 WheelDelta = 0;
};
