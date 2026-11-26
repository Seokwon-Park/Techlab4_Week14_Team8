#pragma once

#include <imgui.h>
#include <imgui_internal.h>

class IEditorPanel
{
public:
	virtual ~IEditorPanel() = default;
	virtual bool Init() = 0;
	virtual void Tick(float DeltaTime) = 0;
	virtual void OnRender() = 0;

	bool IsOpen() const { return bIsOpen; }
	void SetOpen(bool bOpen) { bIsOpen = bOpen; }
	// Begin에 넘기면 도킹 탭에 닫기(X)가 생기고, 누르면 패널이 닫힌다 (Window 메뉴에서 다시 연다).
	bool* GetOpenFlag() { return &bIsOpen; }
	virtual const char* GetPanelName() const = 0;

private:
	bool bIsOpen = true;
};

// 도킹 탭 줄의 높이는 Begin/DockSpace 시점의 FramePadding.y로 정해진다.
// 위젯 패딩은 촘촘하게 두고 탭만 키우려고, 그 두 호출 동안에만 세로 패딩을 키운다.
namespace EditorPanelUI
{
	// 탭 세로 패딩 (DPI 배율 1 기준 px). 탭 높이 = 글자 크기 + 2 * 이 값.
	inline constexpr float TabPaddingY = 7.0f;

	inline void PushTabPadding()
	{
		const ImGuiStyle& Style = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Style.FramePadding.x, TabPaddingY * Style.FontScaleDpi));
	}

	inline void PopTabPadding() { ImGui::PopStyleVar(); }

	// UE5처럼 도킹 탭 이름 앞에 패널 종류 아이콘을 그린다 (EditorTabContainer.cpp).
	// 아이콘은 표시 이름("Details###..."의 "Details")으로 고르고, 표에 없는 패널은 아이콘 없이 그린다.
	// Begin 전에 부르면 탭에 아이콘 자리를 비워 두고, Begin 뒤에 부르면 그 자리에 아이콘을 그린다.
	void ReserveDockTabIcon(const char* WindowName);
	void DrawDockTabIcon();

	// ImGui::Begin 대신 쓴다. 패널 내용은 원래 패딩으로 그려진다.
	inline bool BeginPanel(const char* Name, bool* bOpen = nullptr, ImGuiWindowFlags Flags = 0)
	{
		ReserveDockTabIcon(Name);
		PushTabPadding();
		const bool bVisible = ImGui::Begin(Name, bOpen, Flags);
		PopTabPadding();
		DrawDockTabIcon();
		return bVisible;
	}

	// 모든 에디터 탭(레벨, 에셋 에디터)의 도킹 공간이 같이 쓰는 플래그.
	// UE5처럼 탭마다 닫기(X)만 두고, 탭 줄 왼쪽의 창 메뉴(Hide Tab Bar)와 노드 오른쪽 끝의 닫기는 없앤다.
	inline constexpr ImGuiDockNodeFlags DockSpaceFlags = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;

	// ImGui::DockSpace 대신 쓴다. 공용 플래그를 더하고 탭 높이를 패널 Begin과 맞춘다.
	inline void DockSpace(ImGuiID Id, const ImVec2& Size, ImGuiDockNodeFlags Flags, const ImGuiWindowClass* WindowClass = nullptr)
	{
		PushTabPadding();
		ImGui::DockSpace(Id, Size, Flags | DockSpaceFlags, WindowClass);
		PopTabPadding();
	}
}