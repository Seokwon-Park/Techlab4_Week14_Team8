#include "EnginePCH.h"
#include "PhysicsAssetSettingsPanel.h"

void FPhysicsAssetSettingsPanel::DrawContent()
{
	ImGui::SeparatorText("Show");
	ImGui::Checkbox("Bodies", &Editor.bShowBodies);
	if (Editor.bShowBodies)
	{
		const char* DrawModes[] = { "Solid + Wireframe", "Solid", "Wireframe", "None" };
		int ModeIndex = static_cast<int>(Editor.BodyDrawMode);
		if (ImGui::Combo("Body Draw Mode", &ModeIndex, DrawModes, IM_ARRAYSIZE(DrawModes)))
		{
			Editor.BodyDrawMode = static_cast<EPhysicsBodyDrawMode>(ModeIndex);
		}
		if (Editor.BodyDrawMode == EPhysicsBodyDrawMode::Solid || Editor.BodyDrawMode == EPhysicsBodyDrawMode::SolidWireframe)
		{
			ImGui::SliderFloat("Body Opacity", &Editor.BodyOpacity, 0.05f, 1.0f, "%.2f");
		}
	}
	ImGui::Checkbox("Constraints", &Editor.bShowConstraints);
	if (Editor.bShowConstraints)
	{
		ImGui::Checkbox("3D Shaded Constraints", &Editor.bShowShadedConstraints);
	}
	bool bShowBones = Editor.IsBoneVisualizerVisible();
	if (ImGui::Checkbox("Bones", &bShowBones))
	{
		Editor.SetBoneVisualizerVisible(bShowBones);
	}

	ImGui::SeparatorText("Constraints");
	ImGui::SliderFloat("Constraint Draw Size", &Editor.ConstraintDrawSize, 0.0f, 4.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

	ImGui::SeparatorText("Viewport");
	float CameraSpeed = Editor.GetCameraSpeed();
	if (ImGui::SliderFloat("Camera Speed", &CameraSpeed, 0.1f, 20.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
	{
		Editor.SetCameraSpeed(CameraSpeed);
	}
}
