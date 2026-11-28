#include "EnginePCH.h"
#include "Editor/Viewports/EditorViewportToolbar.h"
#include "SkeletalMeshViewportPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"

namespace
{
	// 뷰포트 위 글자. UE5처럼 어두운 그림자를 깔아 밝은 배경에서도 읽히게 한다.
	void OverlayText(ImDrawList* DrawList, const ImVec2& Pos, ImU32 Color, const char* Text)
	{
		DrawList->AddText(ImVec2(Pos.x + 1.0f, Pos.y + 1.0f), IM_COL32(0, 0, 0, 200), Text);
		DrawList->AddText(Pos, Color, Text);
	}

	// "이름: 값" 한 줄. 값은 강조색으로 쓴다.
	void OverlayKeyValue(ImDrawList* DrawList, const ImVec2& Pos, const char* Key, const char* Value, ImU32 ValueColor)
	{
		OverlayText(DrawList, Pos, EditorColor::OverlayText, Key);
		OverlayText(DrawList, ImVec2(Pos.x + ImGui::CalcTextSize(Key).x, Pos.y), ValueColor, Value);
	}
}

FSkeletalMeshViewportPanel::FSkeletalMeshViewportPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Viewport")
{
}

void FSkeletalMeshViewportPanel::DrawStatsOverlay(const ImVec2& ViewMin, const ImVec2& ViewMax)
{
	const USkeletalMesh* Mesh = Editor.GetMesh();
	if (!Mesh || Mesh->GetRenderData().LODRenderData.IsEmpty())
	{
		return;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(ViewMin, ViewMax, true);

	const float Pad = EditorStyle::Scale(8.0f);
	const float LineHeight = ImGui::GetTextLineHeight() + EditorStyle::Scale(2.0f);
	const float Top = ViewMin.y + EditorViewportUI::GetToolbarHeight() + Pad;

	// 왼쪽 위: 프리뷰 상태와 LOD0 통계
	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	const USkeletalMeshComponent* Component = Editor.GetMeshComponent();
	const char* State = Editor.IsClothPaintMode() ? "Painting Cloth Max Distance"
		: (Component && Component->HasClothSimulation()) ? "Previewing Cloth Simulation"
		: "Previewing Reference Pose";
	const FVector Size = LOD.BindAABB.Max - LOD.BindAABB.Min;
	const int32 BoneIndex = Editor.GetSelectedBoneIndex();
	const auto& Bones = Editor.GetBoneItems();

	const FString Lines[] = {
		"LOD: 0",
		std::format("Triangles: {}", LOD.Indices.Num() / 3),
		std::format("Vertices: {}", LOD.BindVertices.Num()),
		std::format("Sections: {}", LOD.Sections.Num()),
		std::format("Bones: {}", Mesh->GetNumBones()),
		std::format("Approx Size: {:.0f}x{:.0f}x{:.0f}", Size.X, Size.Y, Size.Z),
	};
	ImVec2 Pos(ViewMin.x + Pad, Top);
	OverlayText(DrawList, Pos, EditorColor::OverlayText, State);
	for (const FString& Line : Lines)
	{
		Pos.y += LineHeight;
		OverlayText(DrawList, Pos, EditorColor::OverlayText, Line.c_str());
	}
	if (BoneIndex >= 0 && BoneIndex < Bones.Num())
	{
		Pos.y += LineHeight;
		OverlayKeyValue(DrawList, Pos, "Selected Bone: ", Bones[BoneIndex].Name.c_str(), EditorColor::AccentBright);
	}

	// 오른쪽 위: 단축키 안내
	struct FHotkey { const char* Name; const char* Keys; };
	static constexpr FHotkey BoneHotkeys[] = {
		{ "Focus Mesh: ", "F" },
		{ "Translate / Rotate / Scale: ", "W / E / R" },
		{ "Cycle Gizmo: ", "Space" },
		{ "Cancel Drag: ", "Esc" },
		{ "Camera Speed: ", "LMB + Wheel" },
	};
	static constexpr FHotkey PaintHotkeys[] = {
		{ "Paint: ", "LMB Drag" },
		{ "Erase: ", "Shift + LMB Drag" },
		{ "Orbit Camera: ", "RMB Drag" },
		{ "Focus Mesh: ", "F" },
	};
	const bool bPaint = Editor.IsClothPaintMode();
	const FHotkey* Hotkeys = bPaint ? PaintHotkeys : BoneHotkeys;
	const int32 Count = bPaint ? IM_ARRAYSIZE(PaintHotkeys) : IM_ARRAYSIZE(BoneHotkeys);

	float Width = ImGui::CalcTextSize("Hotkeys:").x;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Width = FMath::Max(Width, ImGui::CalcTextSize(Hotkeys[Index].Name).x + ImGui::CalcTextSize(Hotkeys[Index].Keys).x);
	}
	// 뷰가 좁으면 왼쪽 통계와 겹치지 않도록 단축키 안내를 생략한다
	const float Left = ViewMax.x - Pad - Width;
	if (Left > ViewMin.x + Pad + ImGui::CalcTextSize("Approx Size: 0000x0000x0000").x + Pad)
	{
		Pos = ImVec2(Left, Top);
		OverlayText(DrawList, Pos, EditorColor::OverlayText, "Hotkeys:");
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Pos.y += LineHeight;
			OverlayKeyValue(DrawList, Pos, Hotkeys[Index].Name, Hotkeys[Index].Keys, EditorColor::CategoryBlue);
		}
	}

	DrawList->PopClipRect();
}

void FSkeletalMeshViewportPanel::DrawTransportBar(const ImVec2& BarMin, const ImVec2& BarMax)
{
	// UE5 뷰포트 아래 재생 막대. 스켈레탈 메시 에디터에서는 Cloth 프리뷰를 재생·정지한다.
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(BarMin, BarMax, EditorColor::BgPanel);
	DrawList->AddLine(BarMin, ImVec2(BarMax.x, BarMin.y), EditorColor::Border);

	USkeletalMesh* Mesh = Editor.GetMesh();
	USkeletalMeshComponent* Component = Editor.GetMeshComponent();
	const bool bHasCloth = Mesh && Component && !Mesh->GetClothSectionBindings().IsEmpty();
	const bool bRunning = Component && Component->HasClothSimulation();

	const float ButtonHeight = EditorViewportUI::GetButtonHeight();
	const float Y = BarMin.y + (BarMax.y - BarMin.y - ButtonHeight) * 0.5f;
	const float Pad = EditorStyle::Scale(6.0f);

	const char* Status = bRunning ? "Cloth Simulating" : (bHasCloth ? "Cloth Ready" : "No Cloth Binding");
	const ImVec2 TextSize = ImGui::CalcTextSize(Status);
	DrawList->AddText(ImVec2(BarMin.x + Pad * 2.0f, BarMin.y + (BarMax.y - BarMin.y - TextSize.y) * 0.5f),
		bRunning ? EditorColor::Success : EditorColor::TextMuted, Status);

	// 오른쪽 끝에 재생 / 정지 / 처음부터 버튼
	const float PlayWidth = EditorIcons::CalcButtonWidth(true, nullptr, false, ButtonHeight);
	const float Spacing = ImGui::GetStyle().ItemSpacing.x;
	float X = BarMax.x - Pad - PlayWidth * 3.0f - Spacing * 2.0f;

	ImGui::BeginDisabled(!bHasCloth);
	ImGui::SetCursorScreenPos(ImVec2(X, Y));
	if (EditorIcons::Button("##ClothRestart", EEditorIcon::ToStart, EditorColor::TextPrimary, nullptr, false, false, ButtonHeight)
		&& Component->RestartClothPreview())
	{
		Editor.SetClothPaintMode(false);
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Restart cloth preview");

	X += PlayWidth + Spacing;
	ImGui::SetCursorScreenPos(ImVec2(X, Y));
	if (EditorIcons::Button("##ClothPlay", EEditorIcon::Play, bRunning ? EditorColor::TextMuted : EditorIcons::PlayGreen,
		nullptr, false, bRunning, ButtonHeight) && !bRunning && Component->RestartClothPreview())
	{
		Editor.SetClothPaintMode(false);
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Simulate cloth");

	X += PlayWidth + Spacing;
	ImGui::SetCursorScreenPos(ImVec2(X, Y));
	ImGui::BeginDisabled(!bRunning);
	if (EditorIcons::Button("##ClothStop", EEditorIcon::Stop, EditorColor::Error, nullptr, false, false, ButtonHeight))
	{
		Component->StopClothPreview();
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop cloth simulation");
	ImGui::EndDisabled();
}

void FSkeletalMeshViewportPanel::DrawContent()
{

	constexpr int32 GizmoModeCount = 3;   // 이동, 회전, 크기 (툴바 버튼과 Space 순환)

	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 FullSize = ImGui::GetContentRegionAvail();
	// 아래쪽 재생 막대 자리를 빼고 뷰포트를 그린다
	const float TransportHeight = EditorViewportUI::GetToolbarHeight();
	const ImVec2 Size(FullSize.x, FullSize.y - TransportHeight);
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
	{
		return;
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 ViewMax(Origin.x + Size.x, Origin.y + Size.y);
	if (FTexture2D* Target = Client->Viewport ? Client->Viewport->GetRenderTargetTexture() : nullptr; Target && Target->GetSRV())
	{
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin, ViewMax);
	}

	DrawStatsOverlay(Origin, ViewMax);

	// UE5식 뷰포트 툴바(기즈모 도구, 뷰 종류, 카메라, 뷰 모드, 쇼 플래그)와 왼쪽 아래 축 표시.
	// 입력 영역보다 먼저 그려야 툴바 버튼이 마우스를 먼저 받는다. 툴바 위에서는 뷰포트 입력을 막는다.
	const bool bToolbarBusy = EditorViewportUI::DrawAssetViewportToolbar(Client, Origin, ViewMax,
		Editor.GetGizmoMode(), [this](UE::Widget::EWidgetMode Mode) { Editor.SetGizmoMode(Mode); },
		Editor.GetMutableCameraSpeed());

	DrawTransportBar(ImVec2(Origin.x, ViewMax.y), ImVec2(ViewMax.x, ViewMax.y + TransportHeight));

	// 이미지 영역을 입력 항목으로 등록하여 드래그 중에도 카메라 조작을 유지한다.
	ImGui::SetCursorScreenPos(Origin);
	ImGui::InvisibleButton("##SkeletalMeshViewport", Size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);


	const bool bHovered = ImGui::IsItemHovered() && !bToolbarBusy;
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
	{
		ImGui::SetWindowFocus();
	}

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered)
		EditorViewportUI::HandleViewTypeShortcuts(Client);
	if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		Editor.FocusMesh();
	}


	// 분리된 플랫폼 창의 입력도 받을 수 있도록 ImGui 입력을 사용합니다.
	//
	// TODO: 여기서도 FInputSystem을 사용할 수 있도록, 구조 개선하기
	// ImGui가 생성하는 HWND에 직접 WndProc을 연결하고 FInputSystem에 전달시켜야함

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const FViewportPointerInput PointerInput
	{
		.MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
		.bHovered = bHovered && (!Editor.IsClothPaintMode() || !bCameraDragging),
		.bFocused = bFocused && !IO.AppFocusLost,
		.bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
		.bMouseDown = bMouseDown,
		.bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
		.bShiftDown = IO.KeyShift,
	};

	// 프리뷰 위의 브러시 반경과 현재 편집 모드 표시
	if (Editor.IsClothPaintMode() && bHovered && !bCameraDragging)
	{
		const ImU32 BrushColor = IO.KeyShift ? IM_COL32(255, 110, 105, 255) : IM_COL32(255, 201, 65, 255);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->PushClipRect(Origin, ViewMax, true);
		DrawList->AddCircle(IO.MousePos, Editor.GetClothBrushRadius(), BrushColor, 0, 2.0f);
		DrawList->PopClipRect();
	}

	float EffectiveWheel = IO.MouseWheel * 120.0f;
	if (bHovered && bMouseDown && !bCameraDragging && !Editor.IsGizmoDragging() && !Editor.IsClothPaintMode() && IO.MouseWheel != 0.0f)
	{
		Editor.SetCameraSpeed(FMath::Clamp(Editor.GetCameraSpeed() * powf(1.2f, IO.MouseWheel), 0.1f, 100.0f));
		EffectiveWheel = 0.0f;
	}

	const bool bLeftDrag = bFocused && ImGui::IsItemActive() && bMouseDown && !bCameraDragging
		&& !Editor.IsGizmoDragging() && !Editor.IsClothPaintMode()
		&& ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f);
	FViewportCameraInput Input
	{
		.bCaptured		= bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !Editor.IsGizmoDragging(),
		.bLeftDrag		= bLeftDrag,
		.bAllowWheel	= bHovered && !bMouseDown && !Editor.IsGizmoDragging(),
		.bForward		= ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward		= ImGui::IsKeyDown(ImGuiKey_S),
		.bRight			= ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft			= ImGui::IsKeyDown(ImGuiKey_A),
		.bUp			= ImGui::IsKeyDown(ImGuiKey_E),
		.bDown			= ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX			= IO.MouseDelta.x,
		.MouseY			= IO.MouseDelta.y,

		// ImGui의 휠 단위를 기존 카메라 이동 함수가 사용하는 Windows 휠 단위로 변환합니다.
		.Wheel			= EffectiveWheel,
	};

	// 키보드로 Gizmo 키 변경 (Cloth Paint 중에는 비활성화)
	if (bFocused && bHovered && !bCameraDragging && !IO.WantTextInput && !Editor.IsClothPaintMode() && !Editor.IsGizmoDragging())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W, false))
		{
			Editor.SetGizmoMode(UE::Widget::WM_Translate);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
		{
			Editor.SetGizmoMode(UE::Widget::WM_Rotate);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
		{
			Editor.SetGizmoMode(UE::Widget::WM_Scale);
		}
		else if (!IO.AppFocusLost && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		{
			const int32 NextMode = (static_cast<int32>(Editor.GetGizmoMode()) + 1) % GizmoModeCount;
			Editor.SetGizmoMode(static_cast<UE::Widget::EWidgetMode>(NextMode));
		}
	}

	// 렌더 타깃 변경은 Editor의 다음 Render에서 수행합니다.
	uint32 Width = static_cast<uint32>(Size.x);
	uint32 Height = static_cast<uint32>(Size.y);
	Editor.SetViewportState(Width, Height, Input, PointerInput);
}
