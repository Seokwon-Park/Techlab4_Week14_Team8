#include "EnginePCH.h"
#include "Editor/InputEditor/InputAssetEditorTab.h"
#include "Editor/InputEditor/InputAssetEditor.h"

#include <cctype>
#include <filesystem>

#include "Asset/AssetUtils.h"
#include "Input/InputAction.h"
#include "Input/InputAssetLibrary.h"
#include "Input/InputCoreTypes.h"
#include "Input/InputMappingContext.h"
#include "Input/InputModifiers.h"
#include "Input/InputSystem.h"
#include "Input/InputTriggers.h"

namespace
{
	const char* ValueTypeNames[] = { "Boolean (bool)", "Axis1D (float)", "Axis2D (Vector2D)", "Axis3D (Vector)" };
	const char* SwizzleNames[] = { "YXZ", "ZYX", "XZY", "YZX", "ZXY" };
	const char* ModifierTypeNames[] = { "Negate", "Swizzle Input Axis Values", "Scalar", "Dead Zone" };
	const char* TriggerTypeNames[] = { "Down", "Pressed", "Released", "Hold" };

	const ImVec4 HintColor(0.6f, 0.6f, 0.6f, 1.0f);

	FString FileName(const FString& Path)
	{
		return std::filesystem::path(Path).stem().string();
	}

	bool ContainsNoCase(const FString& Text, const char* Filter)
	{
		if (!Filter || !*Filter) return true;
		FString A = Text, B = Filter;
		for (char& C : A) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
		for (char& C : B) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
		return A.find(B) != FString::npos;
	}

	std::shared_ptr<FInputModifier> MakeModifier(int32 Type)
	{
		switch (Type)
		{
		case 0: return std::make_shared<FInputModifierNegate>();
		case 1: return std::make_shared<FInputModifierSwizzleAxis>();
		case 2: return std::make_shared<FInputModifierScale>();
		case 3: return std::make_shared<FInputModifierDeadZone>();
		}
		return nullptr;
	}

	std::shared_ptr<FInputTrigger> MakeTrigger(int32 Type)
	{
		switch (Type)
		{
		case 0: return std::make_shared<FInputTriggerDown>();
		case 1: return std::make_shared<FInputTriggerPressed>();
		case 2: return std::make_shared<FInputTriggerReleased>();
		case 3: return std::make_shared<FInputTriggerHold>();
		}
		return nullptr;
	}

	int32 GetModifierType(const FInputModifier& Modifier)
	{
		if (dynamic_cast<const FInputModifierNegate*>(&Modifier)) return 0;
		if (dynamic_cast<const FInputModifierSwizzleAxis*>(&Modifier)) return 1;
		if (dynamic_cast<const FInputModifierScale*>(&Modifier)) return 2;
		if (dynamic_cast<const FInputModifierDeadZone*>(&Modifier)) return 3;
		return -1;
	}

	int32 GetTriggerType(const FInputTrigger& Trigger)
	{
		if (dynamic_cast<const FInputTriggerDown*>(&Trigger)) return 0;
		if (dynamic_cast<const FInputTriggerPressed*>(&Trigger)) return 1;
		if (dynamic_cast<const FInputTriggerReleased*>(&Trigger)) return 2;
		if (dynamic_cast<const FInputTriggerHold*>(&Trigger)) return 3;
		return -1;
	}

	// 위/아래/삭제 버튼. 0: 없음, -1: 위로, 1: 아래로, 2: 삭제
	int32 DrawRowButtons(int32 Index, int32 Count)
	{
		int32 Result = 0;
		ImGui::BeginDisabled(Index == 0);
		if (ImGui::ArrowButton("##Up", ImGuiDir_Up)) Result = -1;
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(Index + 1 >= Count);
		if (ImGui::ArrowButton("##Down", ImGuiDir_Down)) Result = 1;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) Result = 2;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove");
		return Result;
	}

	// 순서 바꾸기/지우기를 적용한다. 바꿨으면 true
	template <typename T>
	bool ApplyRowAction(std::vector<T>& Items, int32 Index, int32 Action)
	{
		if (Action == -1 && Index > 0) { std::swap(Items[Index], Items[Index - 1]); return true; }
		if (Action == 1 && Index + 1 < static_cast<int32>(Items.size())) { std::swap(Items[Index], Items[Index + 1]); return true; }
		if (Action == 2) { Items.erase(Items.begin() + Index); return true; }
		return false;
	}
}

FInputAssetEditorTab::FInputAssetEditorTab(FInputAssetEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, InputEditor(InEditor)
{
}

void FInputAssetEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S", false, InputEditor.IsDirty())) InputEditor.Save();
		if (ImGui::MenuItem("Revert to Saved", nullptr, false, InputEditor.IsDirty())) InputEditor.Revert();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor")) InputEditor.Close();
		ImGui::EndMenu();
	}
}

void FInputAssetEditorTab::OnRender(bool bActive)
{
	if (!bActive || InputEditor.IsCloseRequested()) return;

	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && InputEditor.IsDirty())
		InputEditor.Save();

	DrawToolbar();
	ImGui::Separator();

	if (ImGui::BeginChild("##InputAssetBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
	{
		if (UInputAction* Action = InputEditor.GetAction())
			DrawActionEditor(*Action);
		else if (InputEditor.GetContext())
			DrawContextEditor();
	}
	ImGui::EndChild();
}

void FInputAssetEditorTab::DrawToolbar()
{
	ImGui::BeginDisabled(!InputEditor.IsDirty());
	if (ImGui::Button("Save")) InputEditor.Save();
	ImGui::SameLine();
	if (ImGui::Button("Revert")) InputEditor.Revert();
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextColored(HintColor, "%s", InputEditor.GetAssetPath().c_str());
	ImGui::SameLine();
	ImGui::TextColored(HintColor, "  (edits apply to running PIE immediately)");
}

// ------------------------------------------------------------
// Input Action
// ------------------------------------------------------------

void FInputAssetEditorTab::DrawActionEditor(UInputAction& Action)
{
	ImGui::SeparatorText("Action");

	int32 ValueType = static_cast<int32>(Action.ValueType);
	if (ImGui::Combo("Value Type", &ValueType, ValueTypeNames, IM_ARRAYSIZE(ValueTypeNames)))
	{
		Action.ValueType = static_cast<EInputActionValueType>(ValueType);
		InputEditor.NoteEdited();
	}

	if (ImGui::Checkbox("Consume Input", &Action.bConsumeInput)) InputEditor.NoteEdited();
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Keys mapped to this action are not passed to lower priority mapping contexts.");

	ImGui::SeparatorText("Triggers");
	ImGui::TextColored(HintColor, "Evaluated on the combined value of all mappings. Empty = fires while the value is non-zero.");
	if (DrawTriggerList(Action.Triggers)) InputEditor.NoteEdited();

	ImGui::SeparatorText("Modifiers");
	ImGui::TextColored(HintColor, "Applied after all mapping values are added together.");
	if (DrawModifierList(Action.Modifiers)) InputEditor.NoteEdited();
}

// ------------------------------------------------------------
// Input Mapping Context
// ------------------------------------------------------------

void FInputAssetEditorTab::DrawContextEditor()
{
	UInputMappingContext* Context = InputEditor.GetContext();
	std::vector<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();

	UpdateKeyCapture();

	ImGui::SeparatorText("Mappings");
	if (Mappings.empty())
		ImGui::TextColored(HintColor, "No mappings. Add one below.");

	int32 RemoveIndex = -1;
	for (int32 Index = 0; Index < static_cast<int32>(Mappings.size()); ++Index)
	{
		ImGui::PushID(Index);
		if (DrawMapping(Mappings[Index], Index)) RemoveIndex = Index;
		ImGui::PopID();
	}

	if (RemoveIndex >= 0)
	{
		Mappings.erase(Mappings.begin() + RemoveIndex);
		CapturingMapping = -1;
		InputEditor.NoteEdited();
	}

	ImGui::Spacing();
	if (ImGui::Button("+ Add Mapping"))
	{
		Context->MapKey(nullptr, EKeys::Invalid);
		InputEditor.NoteEdited();
	}
}

bool FInputAssetEditorTab::DrawMapping(FEnhancedActionKeyMapping& Mapping, int32 Index)
{
	const FString ActionName = Mapping.Action ? FileName(Mapping.Action->AssetPath.empty() ? Mapping.Action->GetName() : Mapping.Action->AssetPath) : "(no action)";
	const FString KeyName = Mapping.Key.IsValid() ? FInputKeyNames::ToString(Mapping.Key) : "(no key)";
	const FString Header = ActionName + "   <-   " + KeyName + "###Mapping";

	bool bRemove = false;
	const bool bOpen = ImGui::CollapsingHeader(Header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
	if (ImGui::SmallButton("X")) bRemove = true;
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove mapping");

	if (bOpen && !bRemove)
	{
		ImGui::Indent();
		DrawActionPicker(Mapping);
		DrawKeyPicker(Mapping, Index);

		if (ImGui::TreeNodeEx("Triggers", ImGuiTreeNodeFlags_DefaultOpen, "Triggers (%d)", static_cast<int32>(Mapping.Triggers.size())))
		{
			if (DrawTriggerList(Mapping.Triggers)) InputEditor.NoteEdited();
			ImGui::TreePop();
		}
		if (ImGui::TreeNodeEx("Modifiers", ImGuiTreeNodeFlags_DefaultOpen, "Modifiers (%d)", static_cast<int32>(Mapping.Modifiers.size())))
		{
			if (DrawModifierList(Mapping.Modifiers)) InputEditor.NoteEdited();
			ImGui::TreePop();
		}
		ImGui::Unindent();
		ImGui::Spacing();
	}
	return bRemove;
}

void FInputAssetEditorTab::DrawActionPicker(FEnhancedActionKeyMapping& Mapping)
{
	const FString Current = Mapping.Action ? FileName(Mapping.Action->AssetPath) : "None";
	if (ImGui::BeginCombo("Action", Current.c_str()))
	{
		const std::vector<FString>& Paths = InputEditor.GetActionAssetPaths(ImGui::IsWindowAppearing());
		if (ImGui::Selectable("None", Mapping.Action == nullptr))
		{
			Mapping.Action = nullptr;
			InputEditor.NoteEdited();
		}
		for (const FString& Path : Paths)
		{
			const bool bSelected = Mapping.Action && Mapping.Action->AssetPath == FAssetUtils::GetAssetKey(Path);
			if (ImGui::Selectable(FileName(Path).c_str(), bSelected))
			{
				if (UInputAction* Loaded = FInputAssetLibrary::LoadInputAction(Path))
				{
					Mapping.Action = Loaded;
					InputEditor.NoteEdited();
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
		}
		if (Paths.empty()) ImGui::TextColored(HintColor, "No .inputaction files under Assets.");
		ImGui::EndCombo();
	}
	if (Mapping.Action)
	{
		ImGui::SameLine();
		ImGui::TextColored(HintColor, "%s", ValueTypeNames[static_cast<int32>(Mapping.Action->ValueType)]);
	}
}

void FInputAssetEditorTab::DrawKeyPicker(FEnhancedActionKeyMapping& Mapping, int32 Index)
{
	const FString Current = Mapping.Key.IsValid() ? FInputKeyNames::ToString(Mapping.Key) : "None";
	ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - 80.0f);
	if (ImGui::BeginCombo("##Key", Current.c_str(), ImGuiComboFlags_HeightLarge))
	{
		if (ImGui::IsWindowAppearing())
		{
			KeyFilter[0] = '\0';
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputTextWithHint("##KeyFilter", "Search", KeyFilter, sizeof(KeyFilter));
		for (const FKey& Key : FInputKeyNames::GetAllKeys())
		{
			const FString Name = FInputKeyNames::ToString(Key);
			if (!ContainsNoCase(Name, KeyFilter)) continue;
			if (ImGui::Selectable(Name.c_str(), Key == Mapping.Key))
			{
				Mapping.Key = Key;
				InputEditor.NoteEdited();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	const bool bCapturing = CapturingMapping == Index;
	if (ImGui::Button(bCapturing ? "Press a key...###Listen" : "Listen###Listen", ImVec2(72.0f, 0.0f)))
	{
		CapturingMapping = bCapturing ? -1 : Index;
		bCaptureArmed = false;
	}
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press a keyboard key or mouse button to assign it. Esc cancels.");
	ImGui::SameLine();
	ImGui::TextUnformatted("Key");
}

void FInputAssetEditorTab::UpdateKeyCapture()
{
	if (CapturingMapping < 0) return;

	std::vector<FEnhancedActionKeyMapping>& Mappings = InputEditor.GetContext()->GetMappings();
	if (CapturingMapping >= static_cast<int32>(Mappings.size()))
	{
		CapturingMapping = -1;
		return;
	}

	// Listen 버튼을 누른 클릭이 그대로 잡히지 않게 한 프레임 쉰다
	if (!bCaptureArmed)
	{
		bCaptureArmed = true;
		return;
	}

	if (FInputSystem::IsKeyPressed(EKeyCode::Escape))
	{
		CapturingMapping = -1;
		return;
	}

	FKey Captured = EKeys::Invalid;
	for (int32 Code = 1; Code < 256 && !Captured.IsValid(); ++Code)
	{
		if (FInputSystem::IsMouseVirtualKey(Code)) continue;
		if (FInputSystem::IsKeyPressed(static_cast<EKeyCode>(Code))) Captured = FKey(Code);
	}
	constexpr EMouseButton Buttons[] = { EMouseButton::Left, EMouseButton::Right, EMouseButton::Middle, EMouseButton::Side1, EMouseButton::Side2 };
	for (EMouseButton Button : Buttons)
	{
		if (!Captured.IsValid() && FInputSystem::IsMousePressed(Button))
			Captured = FKey(FInputSystem::MouseButtonToVirtualKey(Button));
	}

	if (Captured.IsValid())
	{
		Mappings[CapturingMapping].Key = Captured;
		CapturingMapping = -1;
		InputEditor.NoteEdited();
	}
}

// ------------------------------------------------------------
// Modifier / Trigger 목록
// ------------------------------------------------------------

bool FInputAssetEditorTab::DrawModifierList(std::vector<std::shared_ptr<FInputModifier>>& Modifiers)
{
	bool bChanged = false;
	const int32 Count = static_cast<int32>(Modifiers.size());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ImGui::PushID(Index);
		FInputModifier* Modifier = Modifiers[Index].get();
		const int32 Type = Modifier ? GetModifierType(*Modifier) : -1;

		ImGui::Text("%d.", Index);
		ImGui::SameLine();
		int32 NewType = Type;
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::Combo("##Type", &NewType, ModifierTypeNames, IM_ARRAYSIZE(ModifierTypeNames)) && NewType != Type)
		{
			Modifiers[Index] = MakeModifier(NewType);
			Modifier = Modifiers[Index].get();
			bChanged = true;
		}
		ImGui::SameLine();
		const int32 RowAction = DrawRowButtons(Index, Count);

		ImGui::Indent();
		if (auto* Negate = dynamic_cast<FInputModifierNegate*>(Modifier))
		{
			bChanged |= ImGui::Checkbox("X", &Negate->bX); ImGui::SameLine();
			bChanged |= ImGui::Checkbox("Y", &Negate->bY); ImGui::SameLine();
			bChanged |= ImGui::Checkbox("Z", &Negate->bZ);
		}
		else if (auto* Swizzle = dynamic_cast<FInputModifierSwizzleAxis*>(Modifier))
		{
			int32 Order = static_cast<int32>(Swizzle->Order);
			if (ImGui::Combo("Order", &Order, SwizzleNames, IM_ARRAYSIZE(SwizzleNames)))
			{
				Swizzle->Order = static_cast<EInputAxisSwizzle>(Order);
				bChanged = true;
			}
			ImGui::TextColored(HintColor, "YXZ: sends a 1D key to the Y axis (e.g. W -> forward).");
		}
		else if (auto* Scale = dynamic_cast<FInputModifierScale*>(Modifier))
		{
			bChanged |= ImGui::DragFloat3("Scalar", Scale->Scalar.V, 0.01f);
			bChanged |= ImGui::Checkbox("Scale By Delta Time", &Scale->bScaleByDeltaTime);
		}
		else if (auto* DeadZone = dynamic_cast<FInputModifierDeadZone*>(Modifier))
		{
			bChanged |= ImGui::DragFloat("Lower Threshold", &DeadZone->LowerThreshold, 0.01f, 0.0f, 1.0f);
			bChanged |= ImGui::DragFloat("Upper Threshold", &DeadZone->UpperThreshold, 0.01f, 0.0f, 1.0f);
		}
		ImGui::Unindent();
		ImGui::PopID();

		if (ApplyRowAction(Modifiers, Index, RowAction)) return true;	// 목록이 바뀌었으니 다음 프레임에 다시 그린다
	}

	int32 AddType = -1;
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::Combo("##AddModifier", &AddType, ModifierTypeNames, IM_ARRAYSIZE(ModifierTypeNames)) && AddType >= 0)
	{
		Modifiers.push_back(MakeModifier(AddType));
		bChanged = true;
	}
	ImGui::SameLine();
	ImGui::TextColored(HintColor, "+ Add Modifier");
	return bChanged;
}

bool FInputAssetEditorTab::DrawTriggerList(std::vector<std::shared_ptr<FInputTrigger>>& Triggers)
{
	bool bChanged = false;
	const int32 Count = static_cast<int32>(Triggers.size());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ImGui::PushID(Index);
		FInputTrigger* Trigger = Triggers[Index].get();
		const int32 Type = Trigger ? GetTriggerType(*Trigger) : -1;

		ImGui::Text("%d.", Index);
		ImGui::SameLine();
		int32 NewType = Type;
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::Combo("##Type", &NewType, TriggerTypeNames, IM_ARRAYSIZE(TriggerTypeNames)) && NewType != Type)
		{
			Triggers[Index] = MakeTrigger(NewType);
			Trigger = Triggers[Index].get();
			bChanged = true;
		}
		ImGui::SameLine();
		const int32 RowAction = DrawRowButtons(Index, Count);

		ImGui::Indent();
		if (Trigger)
		{
			bChanged |= ImGui::DragFloat("Actuation Threshold", &Trigger->ActuationThreshold, 0.01f, 0.0f, 1.0f);
			if (auto* Hold = dynamic_cast<FInputTriggerHold*>(Trigger))
			{
				bChanged |= ImGui::DragFloat("Hold Time", &Hold->HoldTimeThreshold, 0.01f, 0.0f, 10.0f, "%.2f s");
				bChanged |= ImGui::Checkbox("One Shot", &Hold->bIsOneShot);
			}
		}
		ImGui::Unindent();
		ImGui::PopID();

		if (ApplyRowAction(Triggers, Index, RowAction)) return true;
	}

	int32 AddType = -1;
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::Combo("##AddTrigger", &AddType, TriggerTypeNames, IM_ARRAYSIZE(TriggerTypeNames)) && AddType >= 0)
	{
		Triggers.push_back(MakeTrigger(AddType));
		bChanged = true;
	}
	ImGui::SameLine();
	ImGui::TextColored(HintColor, "+ Add Trigger");
	return bChanged;
}
