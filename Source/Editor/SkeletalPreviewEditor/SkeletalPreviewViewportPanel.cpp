#include "EnginePCH.h"
#include "Editor/Viewports/EditorViewportToolbar.h"
#include "SkeletalPreviewViewportPanel.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"

void FSkeletalPreviewViewportPanel::DrawContent()
{
    FEditorViewportClient* Client = Editor.GetViewportClient();
    const ImVec2 Size = ImGui::GetContentRegionAvail();
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

    // UE5식 뷰포트 툴바(기즈모 도구, 뷰 종류, 카메라, 뷰 모드, 쇼 플래그)와 왼쪽 아래 축 표시.
    // 입력 영역보다 먼저 그려야 툴바 버튼이 마우스를 먼저 받는다. 툴바 위에서는 뷰포트 입력을 막는다.
    std::function<void(UE::Widget::EWidgetMode)> SetGizmoMode;
    if (Editor.GetGizmoMode() != UE::Widget::WM_None)
    {
        SetGizmoMode = [this](UE::Widget::EWidgetMode Mode) { Editor.SetGizmoMode(Mode); };
    }
    const bool bToolbarBusy = EditorViewportUI::DrawAssetViewportToolbar(Client, Origin, ViewMax, Editor.GetGizmoMode(), SetGizmoMode);

    // 이미지 영역을 입력 항목으로 등록하여 드래그 중에도 카메라 조작을 유지한다.
    ImGui::SetCursorScreenPos(Origin);
    ImGui::InvisibleButton("##AnimViewport", Size,
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

    const ImGuiIO& IO = ImGui::GetIO();
    const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const FViewportPointerInput PointerInput
    {
        .MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
        .bHovered = bHovered,
        .bFocused = bFocused && !IO.AppFocusLost,
        .bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
        .bMouseDown = bMouseDown,
        .bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
        .bRightMousePressed = bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right),
        .bRightMouseDown = bCameraDragging,
        .bCtrlDown = IO.KeyCtrl,
        .bShiftDown = IO.KeyShift,
    };

    const FViewportCameraInput CameraInput
    {
        .bCaptured = bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !Editor.IsViewportInteractionBusy(),
        .bAllowWheel = bHovered && !bMouseDown && !Editor.IsViewportInteractionBusy(),
        .bForward = ImGui::IsKeyDown(ImGuiKey_W),
        .bBackward = ImGui::IsKeyDown(ImGuiKey_S),
        .bRight = ImGui::IsKeyDown(ImGuiKey_D),
        .bLeft = ImGui::IsKeyDown(ImGuiKey_A),
        .bUp = ImGui::IsKeyDown(ImGuiKey_E),
        .bDown = ImGui::IsKeyDown(ImGuiKey_Q),
        .MouseX = IO.MouseDelta.x,
        .MouseY = IO.MouseDelta.y,
        // ImGui의 휠 단위를 카메라 이동 함수가 쓰는 Windows 휠 단위로 바꾼다
        .Wheel = IO.MouseWheel * 120.0f,
    };

    if (SetGizmoMode && bFocused && bHovered && !bCameraDragging && !IO.WantTextInput && !Editor.IsGizmoDragging())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            SetGizmoMode(UE::Widget::WM_Translate);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        {
            SetGizmoMode(UE::Widget::WM_Rotate);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            SetGizmoMode(UE::Widget::WM_Scale);
        }
    }

    Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), CameraInput, PointerInput);
}
