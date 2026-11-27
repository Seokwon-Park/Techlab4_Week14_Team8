#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/EditorViewportToolbar.h"

#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/HitoriEd/Editor.h"
#include "Editor/Settings/SettingsPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	// ELevelViewportType 순서. 아이콘과 UE5 단축키(Alt+G/J/H/K, 반대쪽은 Shift 추가)를 같이 둔다.
	struct FViewportTypeInfo
	{
		const char* Label;
		EEditorIcon Icon;
		const char* Shortcut;
		ImGuiKey Key;
		bool bShift;
	};
	constexpr FViewportTypeInfo ViewportTypes[] = {
		{ "Perspective", EEditorIcon::Camera,     "ALT+G",       ImGuiKey_G, false },
		{ "Top",         EEditorIcon::ViewTop,    "ALT+J",       ImGuiKey_J, false },
		{ "Bottom",      EEditorIcon::ViewBottom, "ALT+SHIFT+J", ImGuiKey_J, true },
		{ "Left",        EEditorIcon::ViewLeft,   "ALT+K",       ImGuiKey_K, false },
		{ "Right",       EEditorIcon::ViewRight,  "ALT+SHIFT+K", ImGuiKey_K, true },
		{ "Front",       EEditorIcon::ViewFront,  "ALT+H",       ImGuiKey_H, false },
		{ "Back",        EEditorIcon::ViewBack,   "ALT+SHIFT+H", ImGuiKey_H, true },
	};
	static_assert(IM_ARRAYSIZE(ViewportTypes) == static_cast<int>(ELevelViewportType::Count));

	// EViewModeIndex 순서
	constexpr const char* ViewModeLabels[] = { "Lit", "Wireframe", "Scene Depth", "World Normal", "Light Culling", "Cascade", "Shadow Mask" };
	constexpr EEditorIcon ViewModeIcons[] = { EEditorIcon::Lit, EEditorIcon::ViewWireframe, EEditorIcon::ViewDepth,
		EEditorIcon::ViewNormal, EEditorIcon::ViewLightCulling, EEditorIcon::ViewCascade, EEditorIcon::ViewShadowMask };
	static_assert(IM_ARRAYSIZE(ViewModeIcons) == IM_ARRAYSIZE(ViewModeLabels), "뷰 모드 아이콘과 이름 수를 맞춘다");

	const FViewportTypeInfo& GetTypeInfo(const FEditorViewportClient* Client)
	{
		const int32 Index = static_cast<int32>(Client->GetViewportType());
		return ViewportTypes[(Index >= 0 && Index < IM_ARRAYSIZE(ViewportTypes)) ? Index : 0];
	}

	int32 GetModeIndex(const FEditorViewportClient* Client)
	{
		const int32 Index = static_cast<int32>(Client->GetViewMode());
		return (Index >= 0 && Index < IM_ARRAYSIZE(ViewModeLabels)) ? Index : 0;
	}

	ImU32 IconColor() { return ImGui::GetColorU32(ImGuiCol_Text, 0.8f); }
}

namespace EditorViewportUI
{
	float GetToolbarHeight()
	{
		return 30.0f * EditorIcons::GetScale();
	}

	float GetButtonHeight()
	{
		return GetToolbarHeight() - 6.0f * EditorIcons::GetScale();
	}

	void DrawToolbarBackground(const ImVec2& ViewMin, const ImVec2& ViewMax)
	{
		ImGui::GetWindowDrawList()->AddRectFilled(ViewMin, ImVec2(ViewMax.x, ViewMin.y + GetToolbarHeight()),
			ImGui::GetColorU32(ImGuiCol_TitleBg, 0.94f));
	}

	void BeginPill()
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->ChannelsSplit(2);
		DrawList->ChannelsSetCurrent(1);
		ImGui::BeginGroup();
	}

	void EndPill()
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		ImGui::EndGroup();
		DrawList->ChannelsSetCurrent(0);
		DrawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_FrameBg),
			4.0f * EditorIcons::GetScale());
		DrawList->ChannelsMerge();
	}

	void OpenPopupBelow(const char* PopupId, bool bAlignRight)
	{
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		ImGui::SetNextWindowPos(ImVec2(bAlignRight ? Max.x : Min.x, Max.y + 2.0f * EditorIcons::GetScale()), ImGuiCond_Appearing,
			ImVec2(bAlignRight ? 1.0f : 0.0f, 0.0f));
		ImGui::OpenPopup(PopupId);
	}

	float CalcViewSettingsWidth(const FEditorViewportClient* Client, const float* CameraSpeed, bool bLabels)
	{
		const float S = EditorIcons::GetScale();
		const float H = GetButtonHeight();
		const float Gap = 4.0f * S;
		char SpeedLabel[16];
		std::snprintf(SpeedLabel, sizeof(SpeedLabel), "%.2f", CameraSpeed ? *CameraSpeed : 1.0f);
		return EditorIcons::CalcButtonWidth(true, bLabels ? GetTypeInfo(Client).Label : nullptr, true, H) + Gap
			+ EditorIcons::CalcButtonWidth(true, (bLabels && CameraSpeed) ? SpeedLabel : nullptr, true, H) + Gap
			+ EditorIcons::CalcButtonWidth(true, bLabels ? ViewModeLabels[GetModeIndex(Client)] : nullptr, true, H) + Gap
			+ EditorIcons::CalcButtonWidth(true, nullptr, true, H) + Gap
			+ EditorIcons::CalcButtonWidth(true, nullptr, true, H);
	}

	void DrawViewSettings(FEditorViewportClient* Client, float* CameraSpeed, bool bLabels)
	{
		const float S = EditorIcons::GetScale();
		const float H = GetButtonHeight();
		const FViewportTypeInfo& TypeInfo = GetTypeInfo(Client);
		const int32 TypeIndex = static_cast<int32>(Client->GetViewportType());
		const int32 ModeIndex = GetModeIndex(Client);
		const char* ModeLabel = ViewModeLabels[ModeIndex];
		FEditorSettings* Settings = GEditor ? GEditor->GetMutableEditorSettings() : nullptr;
		char SpeedLabel[16];
		std::snprintf(SpeedLabel, sizeof(SpeedLabel), "%.2f", CameraSpeed ? *CameraSpeed : 1.0f);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 0.0f));

		// 1. 뷰 종류 (Perspective / Orthographic + View FOV + Exposure)
		if (EditorIcons::Button("##ViewType", TypeInfo.Icon, IconColor(), bLabels ? TypeInfo.Label : nullptr, true,
			ImGui::IsPopupOpen("##ViewTypeMenu"), H))
			OpenPopupBelow("##ViewTypeMenu", true);
		ImGui::SetItemTooltip("Viewport Type: %s", TypeInfo.Label);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 1.0f * S));
		if (ImGui::BeginPopup("##ViewTypeMenu"))
		{
			for (int32 Type = 0; Type < IM_ARRAYSIZE(ViewportTypes); ++Type)
			{
				if (Type == 0 || Type == 1)
				{
					if (Type == 1) ImGui::Spacing();
					EditorIcons::MenuSection(Type == 0 ? "Perspective" : "Orthographic");
				}
				const FViewportTypeInfo& Info = ViewportTypes[Type];
				if (EditorIcons::MenuRow(Info.Label, Info.Icon, TypeIndex == Type, true, false, Info.Shortcut, true))
					Client->SetViewportType(static_cast<ELevelViewportType>(Type));
			}

			ImGui::Spacing();
			EditorIcons::MenuSection("View");
			if (Client->IsPerspective())
			{
				float Fov = Client->GetViewFOV();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Field of View");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f * S);
				if (ImGui::SliderFloat("##ViewFov", &Fov, 30.0f, 120.0f, "%.1f"))
					Client->SetViewFOV(Fov);
			}
			else
			{
				EditorIcons::MenuNote("Field of view applies to perspective view only.", 200.0f * S);
			}

			ImGui::Spacing();
			EditorIcons::MenuSection("Exposure");
			FEngineShowFlags& ViewFlags = Client->GetEngineShowFlags();
			if (EditorIcons::MenuRow("Game Settings", EEditorIcon::Settings, ViewFlags.EyeAdaptation, true, true))
				ViewFlags.EyeAdaptation = !ViewFlags.EyeAdaptation;
			if (Settings)
			{
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("EV100");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(140.0f * S);
				ImGui::SliderFloat("##EV100", &Settings->ExposureCompensation, -4.0f, 4.0f, "%+.2f", ImGuiSliderFlags_AlwaysClamp);
			}

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		// 2. 카메라 속도 (Kurisu 방식: 클릭 시 팝업, 좌우 드래그 시 실시간 속도 조절)
		ImGui::SameLine();
		static bool bSpeedButtonDragged = false;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			bSpeedButtonDragged = false;
		float* EffectiveSpeed = CameraSpeed ? CameraSpeed : (Settings ? &Settings->CameraSpeed : nullptr);
		const bool bSpeedClicked = EditorIcons::Button("##CameraSpeed", EEditorIcon::CameraSpeed, IconColor(),
			(bLabels && CameraSpeed) ? SpeedLabel : nullptr, true, ImGui::IsPopupOpen("##CameraMenu"), H);
		if (EffectiveSpeed && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
		{
			*EffectiveSpeed = FMath::Clamp(*EffectiveSpeed + ImGui::GetIO().MouseDelta.x * 0.1f, 0.1f, 100.0f);
			bSpeedButtonDragged = true;
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
		else if (bSpeedClicked && !bSpeedButtonDragged)
		{
			OpenPopupBelow("##CameraMenu", true);
		}
		ImGui::SetItemTooltip("Camera Speed\nDrag horizontally or hold LMB/RMB + scroll wheel.");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 4.0f * S));
		if (ImGui::BeginPopup("##CameraMenu"))
		{
			EditorIcons::MenuSection("Camera Speed");
			if (EffectiveSpeed)
			{
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Speed");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(160.0f * S);
				ImGui::SliderFloat("##Speed", EffectiveSpeed, 0.1f, 100.0f, "%.2f");
			}
			EditorIcons::MenuNote("Hold LMB or RMB and scroll wheel to adjust speed.", 220.0f * S);

			ImGui::Spacing();
			EditorIcons::MenuSection("Camera Position");
			FVector Pos = Client->GetViewLocation();
			float PosArr[3] = { Pos.X, Pos.Y, Pos.Z };
			ImGui::SetNextItemWidth(210.0f * S);
			if (ImGui::DragFloat3("##CamPos", PosArr, 0.5f, 0.0f, 0.0f, "%.2f"))
				Client->SetViewLocation(FVector(PosArr[0], PosArr[1], PosArr[2]));

			if (ImGui::Button("Origin (0,0,0)"))
				Client->SetViewLocation(FVector::ZeroVector);
			ImGui::SameLine();
			if (ImGui::Button("Reset View"))
			{
				Client->SetViewLocation(FVector(-5.0f, -10.0f, 10.0f));
				Client->SetViewRotation(FRotator(30.0f, 45.0f, 0.0f));
			}

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		// 3. 뷰 모드 (버튼은 지금 모드의 아이콘)
		ImGui::SameLine();
		if (EditorIcons::Button("##ViewMode", ViewModeIcons[ModeIndex], IconColor(), bLabels ? ModeLabel : nullptr, true,
			ImGui::IsPopupOpen("##ViewModeMenu"), H))
			OpenPopupBelow("##ViewModeMenu", true);
		ImGui::SetItemTooltip("View Mode: %s", ModeLabel);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 1.0f * S));
		if (ImGui::BeginPopup("##ViewModeMenu"))
		{
			static constexpr const char* ViewModeShortcuts[] = { "ALT+4", "ALT+2", nullptr, nullptr, nullptr, nullptr, nullptr };
			EditorIcons::MenuSection("View Mode");
			for (int32 Mode = 0; Mode < IM_ARRAYSIZE(ViewModeLabels); ++Mode)
			{
				if (Mode == 2)
				{
					ImGui::Spacing();
					EditorIcons::MenuSection("Buffer Visualization");
				}
				if (EditorIcons::MenuRow(ViewModeLabels[Mode], ViewModeIcons[Mode], ModeIndex == Mode, true, false,
					ViewModeShortcuts[Mode], true))
					Client->SetViewMode(static_cast<EViewModeIndex>(Mode));
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		// 4. 쇼 플래그 (UE5 스타일: 검색 + Use Defaults + Common + Submenus)
		ImGui::SameLine();
		if (EditorIcons::Button("##ShowFlags", EEditorIcon::Eye, IconColor(), nullptr, true, ImGui::IsPopupOpen("##ShowMenu"), H))
			OpenPopupBelow("##ShowMenu", true);
		ImGui::SetItemTooltip("Show Flags");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 1.0f * S));
		if (ImGui::BeginPopup("##ShowMenu"))
		{
			static char ShowSearch[64] = "";
			if (ImGui::IsWindowAppearing())
				ShowSearch[0] = '\0';

			FEngineShowFlags& Flags = Client->GetEngineShowFlags();

			EditorIcons::SearchBox("##ShowSearch", ShowSearch, sizeof(ShowSearch), "Search...", 220.0f * S);
			ImGui::Spacing();
			if (EditorIcons::MenuRow("Use Defaults", EEditorIcon::Reset, false, true, true))
				Flags.Init(EShowFlagInitMode::Editor);
			ImGui::Separator();

			struct FFlagRow { const char* Label; EEditorIcon Icon; bool* Value; };
			const FFlagRow CommonRows[] = {
				{ "Grid",            EEditorIcon::Viewport,         &Flags.Grid },
				{ "Decals",          EEditorIcon::ShowDecal,        &Flags.Decals },
				{ "Skeletal Meshes", EEditorIcon::SkeletalMesh,     &Flags.SkeletalMeshes },
				{ "Primitives",      EEditorIcon::ShowPrimitives,   &Flags.Rendering },
				{ "Anti-Aliasing",   EEditorIcon::ShowAntiAliasing, &Flags.AntiAliasing },
				{ "Collision",       EEditorIcon::ViewWireframe,    &Flags.Collision },
				{ "Particles",       EEditorIcon::Particle,         &Flags.Particles },
				{ "Fog",             EEditorIcon::ShowFog,          &Flags.Fog },
			};
			const FFlagRow PostProcessRows[] = {
				{ "Depth of Field",  EEditorIcon::Camera,            &Flags.DepthOfField },
				{ "Lens Flares",     EEditorIcon::VisualEffects,     &Flags.LensFlares },
				{ "Eye Adaptation",  EEditorIcon::Settings,          &Flags.EyeAdaptation },
				{ "Vignette",        EEditorIcon::ShowVignette,      &Flags.Vignette },
				{ "Camera Overlay",  EEditorIcon::ShowCameraOverlay, &Flags.CameraOverlay },
			};
			const FFlagRow PhysicsRows[] = {
				{ "Collision",       EEditorIcon::ViewWireframe, &Flags.Collision },
				{ "Physics Bodies",  EEditorIcon::ViewWireframe, &Flags.PhysicsBodies },
				{ "Constraints",     EEditorIcon::Rotate,        &Flags.Constraints },
			};
			const FFlagRow DeveloperRows[] = {
				{ "Bounds",          EEditorIcon::ViewWireframe, &Flags.Bounds },
				{ "Visualize DOF",   EEditorIcon::Camera,        &Flags.VisualizeDOF },
				{ "Cascade Bounds",  EEditorIcon::ViewCascade,   &Flags.CascadeBounds },
				{ "Object UUID",     EEditorIcon::Info,          &Flags.ObjectUUID },
				{ "Root Bone (PIE)", EEditorIcon::SkeletalMesh,  &Flags.RootBone },
			};

			if (ShowSearch[0] != '\0')
			{
				auto DrawFiltered = [&](const FFlagRow* Rows, size_t Count)
				{
					for (size_t i = 0; i < Count; ++i)
					{
						if (EditorIcons::MatchesFilter(Rows[i].Label, ShowSearch))
						{
							if (EditorIcons::MenuRow(Rows[i].Label, Rows[i].Icon, *Rows[i].Value, true, true))
								*Rows[i].Value = !*Rows[i].Value;
						}
					}
				};
				DrawFiltered(CommonRows, IM_ARRAYSIZE(CommonRows));
				DrawFiltered(PostProcessRows, IM_ARRAYSIZE(PostProcessRows));
				// Collision은 CommonRows에 이미 있으므로 PhysicsBodies, Constraints만 추가 검색
				DrawFiltered(PhysicsRows + 1, IM_ARRAYSIZE(PhysicsRows) - 1);
				DrawFiltered(DeveloperRows, IM_ARRAYSIZE(DeveloperRows));
			}
			else
			{
				EditorIcons::MenuSection("Common Show Flags");
				for (const FFlagRow& Row : CommonRows)
				{
					if (EditorIcons::MenuRow(Row.Label, Row.Icon, *Row.Value, true, true))
						*Row.Value = !*Row.Value;
				}

				ImGui::Spacing();
				EditorIcons::MenuSection("All Show Flags");
				if (ImGui::BeginMenu("Post Processing"))
				{
					for (const FFlagRow& Row : PostProcessRows)
					{
						if (EditorIcons::MenuRow(Row.Label, Row.Icon, *Row.Value, true, true))
							*Row.Value = !*Row.Value;
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Physics"))
				{
					for (const FFlagRow& Row : PhysicsRows)
					{
						if (EditorIcons::MenuRow(Row.Label, Row.Icon, *Row.Value, true, true))
							*Row.Value = !*Row.Value;
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Developer"))
				{
					for (const FFlagRow& Row : DeveloperRows)
					{
						if (EditorIcons::MenuRow(Row.Label, Row.Icon, *Row.Value, true, true))
							*Row.Value = !*Row.Value;
					}
					ImGui::EndMenu();
				}
			}

			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		// 5. 뷰포트 설정 (UE5 톱니바퀴 드롭다운)
		ImGui::SameLine();
		if (EditorIcons::Button("##ViewportSettings", EEditorIcon::Settings, IconColor(), nullptr, true,
			ImGui::IsPopupOpen("##ViewportSettingsMenu"), H))
			OpenPopupBelow("##ViewportSettingsMenu", true);
		ImGui::SetItemTooltip("Viewport Settings");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 4.0f * S));
		if (ImGui::BeginPopup("##ViewportSettingsMenu"))
		{
			if (Settings)
			{
				EditorIcons::MenuSection("Controls");
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Mouse Sensitivity");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f * S);
				ImGui::SliderFloat("##MouseSens", &Settings->MouseSensitivity, 0.01f, 1.0f, "%.2f");

				float* SpeedPtr = CameraSpeed ? CameraSpeed : &Settings->CameraSpeed;
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Camera Speed");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f * S);
				ImGui::SliderFloat("##VpCamSpeed", SpeedPtr, 0.1f, 100.0f, "%.2f");

				ImGui::Spacing();
				EditorIcons::MenuSection("Grid");
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Grid Spacing");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f * S);
				ImGui::SliderInt("##VpGridSpacing", &Settings->GridSpacing, 1, 100);
				ImGui::Checkbox("Draw PS Grid", &Settings->bDrawPSGrid);
			}
			else
			{
				EditorIcons::MenuNote("Viewport settings unavailable.", 200.0f * S);
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();

		ImGui::PopStyleVar();
	}

	void DrawTransformToolButtons(UE::Widget::EWidgetMode Current, const std::function<void(UE::Widget::EWidgetMode)>& SetMode,
		bool bIncludeSelect)
	{
		struct FToolEntry { const char* Id; const char* Label; EEditorIcon Icon; UE::Widget::EWidgetMode Mode; const char* Key; };
		const FToolEntry Tools[] = {
			{ "##Select",    "Select Mode",    EEditorIcon::Select,    UE::Widget::WM_None,      "Q" },
			{ "##Translate", "Translate Mode", EEditorIcon::Translate, UE::Widget::WM_Translate, "W" },
			{ "##Rotate",    "Rotate Mode",    EEditorIcon::Rotate,    UE::Widget::WM_Rotate,    "E" },
			{ "##Scale",     "Scale Mode",     EEditorIcon::Scale,     UE::Widget::WM_Scale,     "R" },
		};
		const float H = GetButtonHeight();
		const ImU32 ActiveColor = ImGui::GetColorU32(ImGuiCol_SliderGrab);
		bool bFirst = true;
		for (const FToolEntry& Tool : Tools)
		{
			if (!bIncludeSelect && Tool.Mode == UE::Widget::WM_None)
				continue;
			if (!bFirst) ImGui::SameLine();
			bFirst = false;
			if (EditorIcons::Button(Tool.Id, Tool.Icon, Current == Tool.Mode ? ActiveColor : IconColor(), nullptr, false, false, H) && SetMode)
				SetMode(Tool.Mode);
			ImGui::SetItemTooltip("%s (%s)", Tool.Label, Tool.Key);
		}
	}

	// 월드 X/Y/Z 축을 카메라 회전으로 뷰 공간에 옮겨 화면에 그린다.
	// 뷰 공간은 X가 앞(깊이), Y가 오른쪽, Z가 위라서 화면 방향은 (Y, -Z)이다.
	void DrawAxisIndicator(ImDrawList* DrawList, const FEditorViewportClient* Client, const ImVec2& ViewMin, const ImVec2& ViewMax)
	{
		const float S = EditorIcons::GetScale();
		const float Length = 24.0f * S;
		const ImVec2 Origin(ViewMin.x + 30.0f * S, ViewMax.y - 30.0f * S);
		if (Origin.y - Length < ViewMin.y + GetToolbarHeight() || Origin.x + Length > ViewMax.x)
			return;   // 뷰가 너무 작으면 그리지 않는다

		struct FAxis { FVector Dir; const char* Label; ImU32 Color; float Depth; ImVec2 Screen; };
		FAxis Axes[3] = {
			{ FVector(1.0f, 0.0f, 0.0f), "X", IM_COL32(235, 64, 52, 255) },
			{ FVector(0.0f, 1.0f, 0.0f), "Y", IM_COL32(120, 200, 40, 255) },
			{ FVector(0.0f, 0.0f, 1.0f), "Z", IM_COL32(50, 120, 240, 255) },
		};
		const FMatrix View = Client->GetViewMatrix();
		for (FAxis& Axis : Axes)
		{
			const FVector V = View.TransformVector(Axis.Dir);
			Axis.Depth = V.X;
			Axis.Screen = ImVec2(V.Y, -V.Z);
		}
		// 먼 축부터 그려 가까운 축이 위에 오게 한다.
		std::sort(std::begin(Axes), std::end(Axes), [](const FAxis& A, const FAxis& B) { return A.Depth > B.Depth; });

		ImFont* Font = ImGui::GetFont();
		const float LabelSize = ImGui::GetFontSize() * 1.25f;
		for (const FAxis& Axis : Axes)
		{
			const ImVec2 Tip(Origin.x + Axis.Screen.x * Length, Origin.y + Axis.Screen.y * Length);
			DrawList->AddLine(Origin, Tip, Axis.Color, 2.0f * S);

			// 글자는 축 끝에서 조금 더 바깥쪽. 축이 화면을 똑바로 향해 짧아지면 흐리게 한다.
			const float ScreenLen = std::sqrt(Axis.Screen.x * Axis.Screen.x + Axis.Screen.y * Axis.Screen.y);
			const ImVec2 LabelCenter(Origin.x + Axis.Screen.x * (Length + 8.0f * S), Origin.y + Axis.Screen.y * (Length + 8.0f * S));
			const ImVec2 TextSize = Font->CalcTextSizeA(LabelSize, FLT_MAX, 0.0f, Axis.Label);
			const ImVec2 TextPos(std::floor(LabelCenter.x - TextSize.x * 0.5f), std::floor(LabelCenter.y - TextSize.y * 0.5f));
			ImVec4 Col = ImGui::ColorConvertU32ToFloat4(Axis.Color);
			Col.w = ImClamp(ScreenLen * 2.5f, 0.0f, 1.0f);
			const ImU32 LabelColor = ImGui::ColorConvertFloat4ToU32(Col);
			// 바탕 위에서도 보이게 1px 그림자를 깔고 두 번 겹쳐 굵게 쓴다.
			DrawList->AddText(Font, LabelSize, ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), IM_COL32(0, 0, 0, static_cast<int>(Col.w * 160)), Axis.Label);
			DrawList->AddText(Font, LabelSize, TextPos, LabelColor, Axis.Label);
			DrawList->AddText(Font, LabelSize, ImVec2(TextPos.x + 0.6f * S, TextPos.y), LabelColor, Axis.Label);
		}
		DrawList->AddCircleFilled(Origin, 2.0f * S, IM_COL32(40, 40, 40, 255));
	}

	void HandleViewTypeShortcuts(FEditorViewportClient* Client)
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (!Client || !IO.KeyAlt || IO.KeyCtrl || IO.WantTextInput)
			return;
		for (int32 Type = 0; Type < IM_ARRAYSIZE(ViewportTypes); ++Type)
		{
			const FViewportTypeInfo& Info = ViewportTypes[Type];
			if (IO.KeyShift == Info.bShift && ImGui::IsKeyPressed(Info.Key, false))
			{
				Client->SetViewportType(static_cast<ELevelViewportType>(Type));
				return;
			}
		}
	}

	bool DrawAssetViewportToolbar(FEditorViewportClient* Client, const ImVec2& ViewMin, const ImVec2& ViewMax,
		UE::Widget::EWidgetMode GizmoMode, const std::function<void(UE::Widget::EWidgetMode)>& SetGizmoMode,
		float* CameraSpeed)
	{
		if (!Client)
			return false;

		const float S = EditorIcons::GetScale();
		const float BarHeight = GetToolbarHeight();
		const float Pad = 6.0f * S;
		const float ButtonY = ViewMin.y + (BarHeight - GetButtonHeight()) * 0.5f;
		DrawToolbarBackground(ViewMin, ViewMax);

		ImGui::PushID("##AssetViewportToolbar");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f * S, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * S, 6.0f * S));

		// 왼쪽: 기즈모 도구 (기즈모가 있는 에디터만)
		float LeftEndX = ViewMin.x + Pad;
		ImGui::SetCursorScreenPos(ImVec2(LeftEndX, ButtonY));
		if (SetGizmoMode)
		{
			BeginPill();
			DrawTransformToolButtons(GizmoMode, SetGizmoMode, false);
			EndPill();
			LeftEndX = ImGui::GetItemRectMax().x;
		}

		// 오른쪽: 보기 설정. 좁으면 글자를 뺀다.
		const bool bLabels = ViewMax.x - Pad - CalcViewSettingsWidth(Client, CameraSpeed, true) > LeftEndX + 12.0f * S;
		const float RightStartX = ImMax(LeftEndX + 12.0f * S, ViewMax.x - Pad - CalcViewSettingsWidth(Client, CameraSpeed, bLabels));
		ImGui::SetCursorScreenPos(ImVec2(RightStartX, ButtonY));
		DrawViewSettings(Client, CameraSpeed, bLabels);

		ImGui::PopStyleVar(2);
		ImGui::PopID();

		DrawAxisIndicator(ImGui::GetWindowDrawList(), Client, ViewMin, ViewMax);

		return ImGui::IsMouseHoveringRect(ViewMin, ImVec2(ViewMax.x, ViewMin.y + BarHeight)) ||
			ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
	}
}
