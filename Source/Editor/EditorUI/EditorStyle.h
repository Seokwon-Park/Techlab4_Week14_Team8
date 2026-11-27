#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

// 에디터 UI 테마. Dark가 기본이다.
enum class EEditorTheme : unsigned char
{
    Dark,
    Mid,
    Light,
    Hitori,     // 히토리 테마: 딥 다크 바탕에 로즈/크림슨 액센트
};

// 테마 하나의 색 표. 이름의 뜻은 아래 EditorColor 주석을 따른다.
struct FEditorPalette
{
    // 배경 계층
    ImU32 BgDeepest, BgPanel, BgRaised, BgElevated, BgElevatedAlt, BgHeader;
    ImU32 BgHover, BgAccentHover, BgSelected, BgPopup, BgOverlay;
    // 테두리
    ImU32 Border, BorderStrong;
    // 글자와 아이콘
    ImU32 TextPrimary, TextSecondary, TextMuted, TextDisabled, Ink;
    // 강조색
    ImU32 Accent, AccentHover, AccentActive, AccentBright, AccentDim, AccentText;
    // 상태와 분류
    ImU32 Error, Warning, Success, SuccessDim, CategoryBlue, CategoryPurple;
    // 배경보다 밝게(어두운 테마) 또는 어둡게(밝은 테마) 덮는 색. 알파는 쓰는 곳에서 정한다.
    ImU32 Contrast;

    // ImGui 기본 위젯 전용
    ImU32 ImText, ImTextDisabled, ImWindowBg, ImFrameBg, ImFrameBgHovered, ImFrameBgActive;
    ImU32 ImTitleBg, ImMenuBarBg, ImScrollbarBg, ImScrollbarGrab, ImScrollbarGrabHovered;
    ImU32 ImSliderGrab, ImButton, ImButtonHovered, ImHeader, ImHeaderHovered, ImHeaderActive;
    ImU32 ImTab, ImTabHovered, ImTabSelected, ImTabDimmed, ImTabDimmedSelected, ImTabDimmedSelectedOverline;
    ImU32 ImPlotLines, ImPlotHistogram, ImTableHeaderBg, ImCheckMark;
    // 하단 상태 표시줄(콘텐츠 드로어 버튼 줄) 배경
    ImU32 BgStatusBar;
    // 상태 표시줄 위 버튼(콘텐츠 드로어, 출력 로그). 기본 / 드로어가 도킹된 상태
    ImU32 BgBarButton, BgBarButtonDocked;
    // 제목 표시줄 아래 1px 경계선
    ImU32 TitleBarLine;
};

namespace EditorPalettes
{
    // 다크 테마: 어두운 회색 바탕 + 밝은 분홍 강조
    inline constexpr FEditorPalette Dark{
        IM_COL32(13, 13, 16, 255), IM_COL32(18, 18, 21, 255), IM_COL32(24, 24, 28, 255), IM_COL32(30, 30, 34, 255), IM_COL32(36, 36, 41, 255), IM_COL32(42, 42, 47, 255),
        IM_COL32(58, 54, 58, 255), IM_COL32(64, 40, 52, 255), IM_COL32(124, 62, 88, 225), IM_COL32(28, 28, 31, 255), IM_COL32(16, 16, 19, 238),
        IM_COL32(46, 46, 50, 255), IM_COL32(92, 94, 100, 255),
        IM_COL32(244, 244, 246, 255), IM_COL32(204, 204, 208, 255), IM_COL32(162, 164, 170, 255), IM_COL32(118, 120, 126, 255), IM_COL32(20, 20, 22, 255),
        IM_COL32(236, 140, 170, 255), IM_COL32(246, 166, 192, 255), IM_COL32(212, 112, 146, 255), IM_COL32(250, 184, 206, 255), IM_COL32(112, 52, 74, 255), IM_COL32(252, 190, 210, 255),
        IM_COL32(242, 97, 87, 255), IM_COL32(236, 186, 74, 255), IM_COL32(112, 196, 124, 255), IM_COL32(30, 84, 46, 255), IM_COL32(90, 190, 230, 255), IM_COL32(170, 120, 220, 255),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(255, 255, 255, 255), IM_COL32(128, 128, 128, 255), IM_COL32(17, 17, 19, 255), IM_COL32(26, 26, 29, 255), IM_COL32(37, 38, 42, 255), IM_COL32(62, 46, 54, 255),
        IM_COL32(13, 13, 15, 255), IM_COL32(19, 19, 22, 255), IM_COL32(14, 14, 16, 255), IM_COL32(51, 51, 55, 255), IM_COL32(72, 74, 80, 255),
        IM_COL32(220, 140, 166, 255), IM_COL32(36, 36, 40, 255), IM_COL32(56, 52, 56, 255), IM_COL32(37, 38, 42, 255), IM_COL32(66, 50, 58, 255), IM_COL32(124, 62, 88, 255),
        IM_COL32(14, 14, 16, 255), IM_COL32(54, 44, 50, 255), IM_COL32(33, 34, 38, 255), IM_COL32(13, 13, 15, 255), IM_COL32(27, 28, 31, 255), IM_COL32(190, 104, 136, 255),
        IM_COL32(120, 120, 120, 255), IM_COL32(236, 140, 170, 255), IM_COL32(29, 29, 33, 255), IM_COL32(72, 24, 40, 255),
        IM_COL32(18, 18, 21, 255), IM_COL32(30, 30, 34, 255), IM_COL32(56, 44, 50, 255), IM_COL32(132, 66, 92, 255),
    };

    // 회색 바탕에 진한 회색 선 + 밝은 분홍 강조
    inline constexpr FEditorPalette Mid{
        IM_COL32(52, 52, 55, 255), IM_COL32(62, 62, 65, 255), IM_COL32(70, 70, 74, 255), IM_COL32(78, 78, 82, 255), IM_COL32(85, 85, 90, 255), IM_COL32(92, 92, 97, 255),
        IM_COL32(108, 102, 106, 255), IM_COL32(104, 80, 90, 255), IM_COL32(150, 86, 112, 230), IM_COL32(74, 74, 78, 255), IM_COL32(60, 60, 64, 238),
        IM_COL32(38, 38, 41, 255), IM_COL32(24, 24, 26, 255),
        IM_COL32(244, 243, 243, 255), IM_COL32(218, 216, 217, 255), IM_COL32(184, 182, 184, 255), IM_COL32(138, 138, 142, 255), IM_COL32(40, 40, 43, 255),
        IM_COL32(240, 148, 178, 255), IM_COL32(248, 172, 198, 255), IM_COL32(216, 120, 154, 255), IM_COL32(252, 190, 210, 255), IM_COL32(128, 66, 88, 255), IM_COL32(255, 198, 216, 255),
        IM_COL32(250, 118, 108, 255), IM_COL32(242, 198, 92, 255), IM_COL32(132, 210, 142, 255), IM_COL32(50, 102, 64, 255), IM_COL32(118, 204, 238, 255), IM_COL32(190, 150, 232, 255),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(244, 243, 243, 255), IM_COL32(150, 150, 152, 255), IM_COL32(62, 62, 65, 255), IM_COL32(48, 48, 51, 255), IM_COL32(58, 58, 62, 255), IM_COL32(92, 72, 82, 255),
        IM_COL32(46, 46, 49, 255), IM_COL32(56, 56, 59, 255), IM_COL32(54, 54, 57, 255), IM_COL32(100, 100, 104, 255), IM_COL32(120, 122, 128, 255),
        IM_COL32(232, 150, 176, 255), IM_COL32(84, 84, 88, 255), IM_COL32(106, 100, 104, 255), IM_COL32(82, 82, 86, 255), IM_COL32(104, 88, 96, 255), IM_COL32(150, 86, 112, 255),
        IM_COL32(50, 50, 53, 255), IM_COL32(98, 84, 92, 255), IM_COL32(74, 74, 78, 255), IM_COL32(46, 46, 49, 255), IM_COL32(66, 66, 70, 255), IM_COL32(204, 116, 148, 255),
        IM_COL32(170, 170, 172, 255), IM_COL32(240, 148, 178, 255), IM_COL32(72, 72, 76, 255), IM_COL32(72, 24, 40, 255),
        IM_COL32(62, 62, 65, 255), IM_COL32(78, 78, 82, 255), IM_COL32(104, 88, 96, 255), IM_COL32(150, 84, 110, 255),
    };

    // 흰색 바탕에 회색 선 + 분홍 강조 (밝은 바탕에서 읽히도록 한 톤 진하게)
    inline constexpr FEditorPalette Light{
        IM_COL32(232, 232, 235, 255), IM_COL32(246, 246, 248, 255), IM_COL32(238, 238, 241, 255), IM_COL32(252, 252, 253, 255), IM_COL32(242, 242, 245, 255), IM_COL32(226, 226, 230, 255),
        IM_COL32(230, 222, 226, 255), IM_COL32(252, 230, 238, 255), IM_COL32(246, 194, 212, 255), IM_COL32(252, 252, 253, 255), IM_COL32(246, 246, 248, 238),
        IM_COL32(204, 204, 210, 255), IM_COL32(150, 150, 158, 255),
        IM_COL32(26, 26, 30, 255), IM_COL32(64, 64, 70, 255), IM_COL32(104, 104, 112, 255), IM_COL32(156, 156, 162, 255), IM_COL32(214, 214, 220, 255),
        IM_COL32(226, 120, 156, 255), IM_COL32(238, 146, 178, 255), IM_COL32(200, 92, 132, 255), IM_COL32(232, 128, 164, 255), IM_COL32(248, 206, 220, 255), IM_COL32(176, 64, 106, 255),
        IM_COL32(196, 44, 36, 255), IM_COL32(170, 112, 0, 255), IM_COL32(34, 132, 56, 255), IM_COL32(204, 234, 210, 255), IM_COL32(16, 112, 168, 255), IM_COL32(116, 66, 176, 255),
        IM_COL32(0, 0, 0, 255),
        IM_COL32(26, 26, 30, 255), IM_COL32(150, 150, 158, 255), IM_COL32(246, 246, 248, 255), IM_COL32(255, 255, 255, 255), IM_COL32(236, 236, 240, 255), IM_COL32(250, 228, 236, 255),
        IM_COL32(226, 226, 230, 255), IM_COL32(238, 238, 241, 255), IM_COL32(236, 236, 240, 255), IM_COL32(196, 196, 202, 255), IM_COL32(160, 162, 170, 255),
        IM_COL32(226, 120, 156, 255), IM_COL32(232, 232, 236, 255), IM_COL32(242, 212, 222, 255), IM_COL32(208, 210, 218, 255), IM_COL32(246, 214, 226, 255), IM_COL32(244, 184, 206, 255),
        IM_COL32(226, 226, 230, 255), IM_COL32(250, 228, 236, 255), IM_COL32(252, 252, 253, 255), IM_COL32(222, 222, 226, 255), IM_COL32(240, 240, 243, 255), IM_COL32(234, 156, 184, 255),
        IM_COL32(120, 120, 128, 255), IM_COL32(226, 120, 156, 255), IM_COL32(236, 236, 240, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(246, 246, 248, 255), IM_COL32(252, 252, 253, 255), IM_COL32(244, 220, 228, 255), IM_COL32(240, 176, 198, 255),
    };

    // 히토리 테마: 딥 다크 바탕 + 로즈 핑크/크림슨 액센트
    inline constexpr FEditorPalette Hitori{
        IM_COL32(11, 11, 14, 255), IM_COL32(17, 17, 21, 255), IM_COL32(24, 22, 27, 255), IM_COL32(31, 28, 34, 255), IM_COL32(38, 34, 41, 255), IM_COL32(46, 39, 46, 255),
        IM_COL32(60, 48, 56, 255), IM_COL32(74, 34, 52, 255), IM_COL32(106, 36, 68, 235), IM_COL32(26, 22, 28, 255), IM_COL32(20, 15, 20, 238),
        IM_COL32(58, 36, 48, 255), IM_COL32(128, 72, 96, 255),
        IM_COL32(244, 242, 246, 255), IM_COL32(214, 208, 216, 255), IM_COL32(148, 142, 154, 255), IM_COL32(104, 98, 110, 255), IM_COL32(14, 12, 16, 255),
        IM_COL32(214, 72, 124, 255), IM_COL32(236, 98, 148, 255), IM_COL32(162, 46, 90, 255), IM_COL32(248, 126, 170, 255), IM_COL32(108, 34, 62, 255), IM_COL32(252, 152, 188, 255),
        IM_COL32(255, 96, 96, 255), IM_COL32(240, 190, 82, 255), IM_COL32(112, 200, 132, 255), IM_COL32(28, 80, 44, 255), IM_COL32(98, 180, 230, 255), IM_COL32(182, 132, 226, 255),
        IM_COL32(255, 255, 255, 255),
        IM_COL32(244, 242, 246, 255), IM_COL32(128, 124, 134, 255), IM_COL32(17, 17, 21, 255), IM_COL32(26, 22, 28, 255), IM_COL32(42, 32, 40, 255), IM_COL32(74, 34, 52, 255),
        IM_COL32(94, 28, 56, 255), IM_COL32(22, 18, 23, 255), IM_COL32(13, 12, 15, 255), IM_COL32(64, 46, 56, 255), IM_COL32(116, 56, 82, 255),
        IM_COL32(148, 142, 154, 255), IM_COL32(36, 30, 38, 255), IM_COL32(90, 40, 62, 255), IM_COL32(44, 34, 42, 255), IM_COL32(92, 42, 64, 255), IM_COL32(138, 48, 84, 255),
        IM_COL32(34, 24, 32, 255), IM_COL32(74, 36, 54, 255), IM_COL32(44, 34, 42, 255), IM_COL32(30, 22, 28, 255), IM_COL32(40, 30, 38, 255), IM_COL32(168, 52, 96, 255),
        IM_COL32(148, 142, 154, 255), IM_COL32(214, 72, 124, 255), IM_COL32(30, 24, 32, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(84, 24, 48, 255), IM_COL32(44, 34, 42, 255), IM_COL32(62, 44, 56, 255), IM_COL32(174, 56, 100, 255),
    };

    inline constexpr const FEditorPalette& Get(EEditorTheme Theme)
    {
        switch (Theme)
        {
        case EEditorTheme::Mid: return Mid;
        case EEditorTheme::Light: return Light;
        case EEditorTheme::Hitori: return Hitori;
        default: return Dark;
        }
    }
}

namespace EditorColor
{
    inline FEditorPalette GPalette = EditorPalettes::Dark;
    inline EEditorTheme GTheme = EEditorTheme::Dark;

    inline const ImU32& BgDeepest = GPalette.BgDeepest;
    inline const ImU32& BgPanel = GPalette.BgPanel;
    inline const ImU32& BgRaised = GPalette.BgRaised;
    inline const ImU32& BgElevated = GPalette.BgElevated;
    inline const ImU32& BgElevatedAlt = GPalette.BgElevatedAlt;
    inline const ImU32& BgHeader = GPalette.BgHeader;
    inline const ImU32& BgHover = GPalette.BgHover;
    inline const ImU32& BgAccentHover = GPalette.BgAccentHover;
    inline const ImU32& BgSelected = GPalette.BgSelected;
    inline const ImU32& BgPopup = GPalette.BgPopup;
    inline const ImU32& BgOverlay = GPalette.BgOverlay;
    inline constexpr ImU32 Scrim = IM_COL32(0, 0, 0, 140);
    inline constexpr ImU32 Transparent = IM_COL32(0, 0, 0, 0);

    inline const ImU32& Border = GPalette.Border;
    inline const ImU32& BorderStrong = GPalette.BorderStrong;

    inline const ImU32& TextPrimary = GPalette.TextPrimary;
    inline const ImU32& TextSecondary = GPalette.TextSecondary;
    inline const ImU32& TextMuted = GPalette.TextMuted;
    inline const ImU32& TextDisabled = GPalette.TextDisabled;
    inline const ImU32& Ink = GPalette.Ink;

    inline const ImU32& Accent = GPalette.Accent;
    inline const ImU32& AccentHover = GPalette.AccentHover;
    inline const ImU32& AccentActive = GPalette.AccentActive;
    inline const ImU32& AccentBright = GPalette.AccentBright;
    inline const ImU32& AccentDim = GPalette.AccentDim;
    inline const ImU32& AccentText = GPalette.AccentText;

    inline const ImU32& Error = GPalette.Error;
    inline const ImU32& Warning = GPalette.Warning;
    inline const ImU32& Success = GPalette.Success;
    inline const ImU32& SuccessDim = GPalette.SuccessDim;
    inline const ImU32& CategoryBlue = GPalette.CategoryBlue;
    inline const ImU32& CategoryPurple = GPalette.CategoryPurple;

    inline constexpr ImU32 CloseHover = IM_COL32(232, 17, 35, 255);
    inline constexpr ImU32 CloseHeld = IM_COL32(241, 112, 122, 255);

    inline constexpr ImU32 OverlayText = IM_COL32(246, 244, 244, 255);
    inline constexpr ImU32 OverlayTextAccent = IM_COL32(235, 128, 86, 255);

    constexpr ImU32 WithAlpha(ImU32 Color, int Alpha)
    {
        return (Color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha) << IM_COL32_A_SHIFT);
    }
    constexpr ImU32 White(int Alpha) { return IM_COL32(255, 255, 255, Alpha); }
    inline ImU32 Contrast(int Alpha) { return WithAlpha(GPalette.Contrast, Alpha); }
    inline ImVec4 ToVec4(ImU32 Color) { return ImGui::ColorConvertU32ToFloat4(Color); }
}

namespace EditorStyle
{
    inline float Scale(float Value)
    {
        const float Dpi = ImGui::GetCurrentContext() ? ImGui::GetStyle().FontScaleDpi : 1.0f;
        return Value * (Dpi > 0.0f ? Dpi : 1.0f);
    }

    inline bool ContainsIgnoreCase(const char* Text, const char* Pattern)
    {
        if (!Pattern || !*Pattern) return true;
        if (!Text) return false;
        for (; *Text; ++Text)
        {
            const char* T = Text;
            const char* P = Pattern;
            while (*T && *P && ImToUpper(*T) == ImToUpper(*P)) { ++T; ++P; }
            if (!*P) return true;
        }
        return false;
    }

    inline void ApplyColors(ImGuiStyle& style)
    {
        using namespace EditorColor;
        const FEditorPalette& P = GPalette;
        ImVec4* colors = style.Colors;
        auto V = [](ImU32 Color) { return ToVec4(Color); };
        auto VA = [](ImU32 Color, float Alpha) { ImVec4 C = ToVec4(Color); C.w = Alpha; return C; };

        const ImVec4 AccentV = V(P.Accent);
        const ImVec4 AccentHoverV = V(P.AccentHover);
        const ImVec4 AccentActiveV = V(P.AccentActive);

        colors[ImGuiCol_Text] = V(P.ImText);
        colors[ImGuiCol_TextDisabled] = V(P.ImTextDisabled);
        colors[ImGuiCol_TextLink] = AccentHoverV;
        colors[ImGuiCol_TextSelectedBg] = VA(P.Contrast, 0.156f);

        colors[ImGuiCol_WindowBg] = V(P.ImWindowBg);
        colors[ImGuiCol_ChildBg] = VA(P.ImWindowBg, 0.0f);
        colors[ImGuiCol_PopupBg] = V(P.BgPopup);
        colors[ImGuiCol_DockingEmptyBg] = colors[ImGuiCol_WindowBg];

        colors[ImGuiCol_Border] = V(P.Border);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg] = V(P.ImFrameBg);
        colors[ImGuiCol_FrameBgHovered] = V(P.ImFrameBgHovered);
        colors[ImGuiCol_FrameBgActive] = V(P.ImFrameBgActive);
        colors[ImGuiCol_InputTextCursor] = V(P.ImText);

        colors[ImGuiCol_TitleBg] = V(P.ImTitleBg);
        colors[ImGuiCol_TitleBgActive] = V(P.ImTitleBg);
        colors[ImGuiCol_TitleBgCollapsed] = V(P.ImTitleBg);
        colors[ImGuiCol_MenuBarBg] = V(P.ImMenuBarBg);

        colors[ImGuiCol_ScrollbarBg] = V(P.ImScrollbarBg);
        colors[ImGuiCol_ScrollbarGrab] = V(P.ImScrollbarGrab);
        colors[ImGuiCol_ScrollbarGrabHovered] = V(P.ImScrollbarGrabHovered);
        colors[ImGuiCol_ScrollbarGrabActive] = AccentActiveV;

        colors[ImGuiCol_CheckboxSelectedBg] = ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.85f);
        colors[ImGuiCol_CheckMark] = V(P.ImCheckMark);

        colors[ImGuiCol_SliderGrab] = V(P.ImSliderGrab);
        colors[ImGuiCol_SliderGrabActive] = AccentHoverV;

        colors[ImGuiCol_Button] = V(P.ImButton);
        colors[ImGuiCol_ButtonHovered] = V(P.ImButtonHovered);
        colors[ImGuiCol_ButtonActive] = AccentActiveV;

        colors[ImGuiCol_Header] = V(P.ImHeader);
        colors[ImGuiCol_HeaderHovered] = V(P.ImHeaderHovered);
        colors[ImGuiCol_HeaderActive] = V(P.ImHeaderActive);

        colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
        colors[ImGuiCol_SeparatorHovered] = V(P.ImSliderGrab);
        colors[ImGuiCol_SeparatorActive] = AccentV;

        colors[ImGuiCol_ResizeGrip] = VA(P.Contrast, 0.25f);
        colors[ImGuiCol_ResizeGripHovered] = VA(P.Contrast, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = AccentV;

        colors[ImGuiCol_Tab] = V(P.ImTab);
        colors[ImGuiCol_TabHovered] = V(P.ImTabHovered);
        colors[ImGuiCol_TabSelected] = V(P.ImTabSelected);
        colors[ImGuiCol_TabDimmed] = V(P.ImTabDimmed);
        colors[ImGuiCol_TabDimmedSelected] = V(P.ImTabDimmedSelected);
        colors[ImGuiCol_TabSelectedOverline] = AccentV;
        colors[ImGuiCol_TabDimmedSelectedOverline] = V(P.ImTabDimmedSelectedOverline);

        colors[ImGuiCol_DockingPreview] = ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.65f);

        colors[ImGuiCol_PlotLines] = V(P.ImPlotLines);
        colors[ImGuiCol_PlotLinesHovered] = AccentHoverV;
        colors[ImGuiCol_PlotHistogram] = V(P.ImPlotHistogram);
        colors[ImGuiCol_PlotHistogramHovered] = AccentHoverV;

        colors[ImGuiCol_TableHeaderBg] = V(P.ImTableHeaderBg);
        colors[ImGuiCol_TableBorderStrong] = colors[ImGuiCol_Border];
        colors[ImGuiCol_TableBorderLight] = V(P.ImHeader);
        colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt] = VA(P.Contrast, 0.03f);

        colors[ImGuiCol_TreeLines] = colors[ImGuiCol_Border];
        colors[ImGuiCol_DragDropTarget] = AccentHoverV;
        colors[ImGuiCol_DragDropTargetBg] = ImVec4(AccentV.x, AccentV.y, AccentV.z, 0.18f);
        colors[ImGuiCol_UnsavedMarker] = AccentV;
        colors[ImGuiCol_NavCursor] = AccentV;
        colors[ImGuiCol_NavWindowingHighlight] = AccentV;
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.586f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.586f);
    }

    inline EEditorTheme GetTheme() { return EditorColor::GTheme; }

    inline constexpr EEditorTheme AllThemes[] = { EEditorTheme::Dark, EEditorTheme::Mid, EEditorTheme::Light, EEditorTheme::Hitori };

    inline void SetTheme(EEditorTheme Theme)
    {
        EditorColor::GTheme = Theme;
        EditorColor::GPalette = EditorPalettes::Get(Theme);
        if (ImGui::GetCurrentContext())
            ApplyColors(ImGui::GetStyle());
    }

    inline const char* GetThemeName(EEditorTheme Theme)
    {
        switch (Theme)
        {
        case EEditorTheme::Mid: return "Mid";
        case EEditorTheme::Light: return "Light";
        case EEditorTheme::Hitori: return "Hitori";
        default: return "Dark";
        }
    }
}
