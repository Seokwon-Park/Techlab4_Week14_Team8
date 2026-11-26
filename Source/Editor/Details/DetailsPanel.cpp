#include "EnginePCH.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/Details/PropertyEditor.h"
#include "Core/Misc/Paths.h"

#include <filesystem>

#include "imgui_internal.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"
#include "Editor/HitoriEd/EditorActorUtils.h"
#include "Editor/EditorUI/ComponentTypeInfo.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/SpotLightComponent.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"

#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/AnimBlueprintFile.h"
#include <algorithm>
#include <vector>

#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemFile.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Render/SkeletalMesh.h"
#include "Text/Font.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"
#include "Core/EngineLog.h"

#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Script/LuaScriptManager.h"
#include <unordered_map>


namespace
{
	struct FDragSettings
	{
		float Speed;
		float Min;
		float Max;
	};

	FDragSettings GetDragSettings(const FPropertyMeta& Meta, float DefaultSpeed)
	{
		const float Min = (std::max)(Meta.UIMin, Meta.ClampMin);
		const float Max = (std::min)(Meta.UIMax, Meta.ClampMax);
		const bool bHasRange = Min > -FLT_MAX || Max < FLT_MAX;
		return { Meta.Delta > 0.0f ? Meta.Delta : DefaultSpeed, bHasRange ? Min : 0.0f, bHasRange ? Max : 0.0f };
	}

	int32 ToIntBound(float Value)
	{
		if (Value <= static_cast<float>(INT_MIN)) return INT_MIN;
		if (Value >= static_cast<float>(INT_MAX)) return INT_MAX;
		return static_cast<int32>(Value);
	}

	// 드래그 범위와 별개로, Ctrl+클릭으로 직접 입력한 값도 Clamp 범위로 자른다.
	void ClampToMeta(float* Values, int32 Count, const FPropertyMeta& Meta)
	{
		if (!Meta.HasClamp())
		{
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Values[Index] = std::clamp(Values[Index], Meta.ClampMin, Meta.ClampMax);
		}
	}

	void DrawScriptSection(AActor* Actor)
	{
		ImGui::SeparatorText("Lua Script");

		if (Actor->ScriptPath.empty())
		{
			if (ImGui::Button("Create Script"))
			{
				const FString& ScenePath = FEditorFileUtils::CurrentScenePath;
				if (ScenePath.empty())
				{
					HTR_LOG(Warning, "[Lua] Save the scene first. The scene name will be included in the script name.");
				}
				else
				{
					const FString SceneName = FLuaScriptManager::FromPath(FLuaScriptManager::ToPath(ScenePath).stem());
					const FString NewPath = FLuaScriptManager::MakeScriptPath(SceneName, Actor->GetName());
					if (FLuaScriptManager::Get().CreateScriptFromTemplate(NewPath))
					{
						Actor->ScriptPath = NewPath;   // 리플렉션 프로퍼티라 씬 저장 때 같이 저장된다
					}
				}
			}
			return;
		}

		ImGui::TextWrapped("%s", Actor->ScriptPath.c_str());
		if (ImGui::Button("Edit Script"))
		{
			FLuaScriptManager::Get().OpenScriptInEditor(Actor->ScriptPath);
		}
		ImGui::SameLine();
		if (ImGui::Button("Detach"))
		{
			Actor->ScriptPath.clear();   // 연결만 끊고 파일은 지우지 않는다
		}
	}

	// UE5 Details의 축 색 (X 빨강, Y 초록, Z 파랑, W 회색)
	ImU32 AxisColor(int32 Axis)
	{
		switch (Axis)
		{
		case 0:  return IM_COL32(219, 66, 66, 255);
		case 1:  return IM_COL32(115, 191, 64, 255);
		case 2:  return IM_COL32(64, 128, 230, 255);
		default: return IM_COL32(150, 150, 150, 255);
		}
	}

	// 검색 칸에 쓴 글자. 비어 있지 않으면 이름에 이 글자가 들어간 프로퍼티만 그린다.
	// DrawProperties를 부르는 동안에만 FDetailsPanel::OnRender가 정한다.
	const char* GPropertyFilter = nullptr;

	// UE5 Details 카테고리 머리줄. 선택 색(분홍)과 구분되게 회색으로 칠한다.
	bool CategoryHeader(const char* Label, ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_DefaultOpen)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		const bool bOpen = ImGui::CollapsingHeader(Label, Flags);
		ImGui::PopStyleColor(3);
		return bOpen;
	}

	// 입력 칸 Count개를 한 줄에 나눠 그리고, 각 칸 왼쪽에 축 색 막대를 붙인다 (UE5 벡터 입력).
	// 전체 폭은 SetNextItemWidth로 정한 값(없으면 기본 아이템 폭)을 따른다.
	bool DrawAxisFields(const char* Id, float* Values, int32 Count, float Speed, float Min, float Max, const char* Format = "%.3f")
	{
		const float S = EditorIcons::GetScale();
		const float Spacing = 4.0f * S;
		const float BarWidth = 3.0f * S;
		const float TotalWidth = ImGui::CalcItemWidth();
		const float FieldWidth = ImMax(1.0f, (TotalWidth - Spacing * (Count - 1)) / Count);
		const ImGuiStyle& Style = ImGui::GetStyle();

		bool bChanged = false;
		ImGui::PushID(Id);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Spacing, Style.ItemSpacing.y));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Style.FramePadding.x + BarWidth, Style.FramePadding.y));
		for (int32 Axis = 0; Axis < Count; ++Axis)
		{
			if (Axis > 0)
				ImGui::SameLine();
			ImGui::PushID(Axis);
			ImGui::SetNextItemWidth(FieldWidth);
			bChanged |= ImGui::DragFloat("##Axis", &Values[Axis], Speed, Min, Max, Format);
			const ImVec2 ItemMin = ImGui::GetItemRectMin();
			const ImVec2 ItemMax = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRectFilled(ItemMin, ImVec2(ItemMin.x + BarWidth, ItemMax.y),
				AxisColor(Axis), ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);
			ImGui::PopID();
		}
		ImGui::PopStyleVar(2);
		ImGui::PopID();
		return bChanged;
	}

	// Transform 한 줄: 라벨 | X Y Z | 리셋. 리셋 버튼은 값이 기본값과 다를 때만 보이고, 세 값을 _resetValue로 되돌린다.
	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue, float _columnWidth)
	{
		const float S = EditorIcons::GetScale();
		const float StartX = ImGui::GetCursorPosX();
		const float ResetSize = ImGui::GetFrameHeight();
		const bool bIsDefault = _values[0] == _resetValue && _values[1] == _resetValue && _values[2] == _resetValue;

		bool bChanged = false;
		ImGui::PushID(_label.c_str());

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(_label.c_str());
		ImGui::SameLine();
		ImGui::SetCursorPosX(StartX + _columnWidth * S);

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ResetSize - ImGui::GetStyle().ItemSpacing.x);
		bChanged |= DrawAxisFields("##Fields", _values, 3, 0.1f, 0.0f, 0.0f, "%.2f");

		ImGui::SameLine();
		if (bIsDefault)
		{
			ImGui::Dummy(ImVec2(ResetSize, ResetSize));
		}
		else
		{
			if (EditorIcons::Button("##Reset", EEditorIcon::Reset, ImGui::GetColorU32(ImGuiCol_Text, 0.7f),
				nullptr, false, false, ResetSize))
			{
				_values[0] = _values[1] = _values[2] = _resetValue;
				bChanged = true;
			}
			ImGui::SetItemTooltip("Reset to Default");
		}

		ImGui::PopID();
		return bChanged;
	}

	// FRotator의 V는 (Pitch, Yaw, Roll) 순서지만 엔진은 Roll=X, Pitch=Y, Yaw=Z축이므로
	// X/Y/Z 칸이 실제 회전축과 맞도록 재배열해 편집한다.
	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f, 70.0f);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}

	void DrawTextureSlot(UMaterial** MaterialPtr, int32 SlotIndex)
	{
		const float ThumbnailSize = 64.0f;

		UMaterial* Material = *MaterialPtr;
		UTexture2D* Texture = nullptr;
		if (Material && SlotIndex < static_cast<int32>(Material->Textures.Num()))
		{
			Texture = Material->Textures[SlotIndex];
		}

		ImGui::PushID(SlotIndex);

		if (Texture && Texture->GetResource())
		{
			ImGui::Image(
				(ImTextureID)Texture->GetResource()->GetSRV(),
				{ ThumbnailSize, ThumbnailSize });
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		// 바로 위 위젯이 드롭을 받는다
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* DroppedTexture = *static_cast<UTexture2D**>(Payload->Data);

				// 머티리얼이 없으면 기본 머티리얼에서 시작한다
				if (!Material)
				{
					Material = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
					*MaterialPtr = Material;
				}
				// 공유 에셋이면 지금 복제한다.
				// 이 시점까지 미루면 손대지 않은 오브젝트는 계속 공유 에셋을 쓴다.
				else if (Material->bIsInstance == false)
				{
					Material = UMaterial::CreateInstance(Material);
					*MaterialPtr = Material;
				}

				if (Material)
				{
					// 슬롯이 배열 범위 밖이면 그 자리까지 늘려 준다
					while (static_cast<int32>(Material->Textures.Num()) <= SlotIndex)
					{
						Material->Textures.Add(nullptr);
					}

					Material->Textures[SlotIndex] = DroppedTexture;
					Texture = DroppedTexture;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();

		ImGui::BeginGroup();
		ImGui::Text("Slot %d", SlotIndex);

		if (Texture)
		{
			ImGui::TextWrapped("%s", Texture->GetPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("(비어 있음)");
		}
		ImGui::EndGroup();

		ImGui::PopID();
	}

	FString GetAssetDisplayName(const FString& Path)
	{
		fs::path AssetPath(Path);
		if (AssetPath.stem() == "Atlas")
		{
			return AssetPath.parent_path().filename().string();
		}
		return AssetPath.stem().string();
	}

	void DrawFontSlot(UObject* Owner, UFont** FontPtr)
	{
		UFont* Current = *FontPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##Font", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UFont> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UTextRenderComponent* TextComponent = Cast<UTextRenderComponent>(Owner))
						TextComponent->SetFont(*Itr);
					else
						*FontPtr = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}

			// 아직 로드되지 않은 폰트는 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Font))
			{
				ImGui::PushID(ItemPath.c_str());
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
				{
					if (UFont* Font = UAssetManager::GetAssetByPath<UFont>(ItemPath))
					{
						if (UTextRenderComponent* TextComponent = Cast<UTextRenderComponent>(Owner))
							TextComponent->SetFont(Font);
						else
							*FontPtr = Font;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	void DrawSkeletalMeshSlot(USkinnedMeshComponent* Component)
	{
		USkeletalMesh* Current = Component->GetSkeletalMesh();
		const FString CurrentPath = Current ? Current->GetPath() : "";
		const FString CurrentName = Current ? (CurrentPath.empty() ? Current->GetName() : GetAssetDisplayName(CurrentPath)) : "None";
		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen = ImGui::BeginCombo("##SkeletalMesh", CurrentName.c_str());
		if (ImGui::IsItemHovered() && Current) ImGui::SetTooltip("%s", CurrentPath.c_str());
		if (!bOpen) return;
		if (ImGui::Selectable("None", Current == nullptr)) Component->SetSkeletalMesh(nullptr);
		for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
		{
			const FString Path = Itr->GetPath();
			if (Path.empty()) continue;
			const bool bSelected = *Itr == Current;
			ImGui::PushID(*Itr);
			if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected)) Component->SetSkeletalMesh(*Itr);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
			if (bSelected) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}

		// 아직 로드되지 않은 메시는 경로만 보여 주고 고를 때 로드한다
		for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::SkeletalMesh))
		{
			ImGui::PushID(ItemPath.c_str());
			if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
			{
				if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(ItemPath))
				{
					Component->SetSkeletalMesh(Mesh);
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

    void DrawAnimBlueprintSlot(USkeletalMeshComponent* Component)
    {
        static std::vector<FString> Paths;
        static FString ScanMessage;
        const FString& CurrentPath = Component->GetAnimBlueprintPath();
        const FString Name = CurrentPath.empty() ? "None" : GetAssetDisplayName(CurrentPath);
        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::BeginCombo("##AnimBlueprintAsset", Name.c_str()))
        {
            if (ImGui::IsWindowAppearing())
            {
                Paths.clear();
                ScanMessage.clear();
                std::error_code Error;
                std::filesystem::recursive_directory_iterator It("Assets", Error);
                const std::filesystem::recursive_directory_iterator End;

                while (!Error && It != End)
                {
                    if (It->is_regular_file(Error))
                    {
                        const FString Path = FPaths::ToUtf8(It->path());

                        if (AnimBlueprintFile::IsAnimBlueprintPath(Path))
                        {
                            Paths.push_back(FAssetUtils::GetAssetKey(Path));
                        }
                    }

                    if (!Error)
                    {
                        It.increment(Error);
                    }
                }

                std::sort(Paths.begin(), Paths.end());

                if (Error)
                {
                    ScanMessage = "Cannot read the asset folder: " + Error.message();
                }
            }

            if (ImGui::Selectable("None", CurrentPath.empty()))
            {
                Component->SetAnimBlueprint(FString());
            }

            for (const FString& Path : Paths)
            {
                ImGui::PushID(Path.c_str());

                if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), CurrentPath == Path))
                {
                    Component->SetAnimBlueprint(Path);
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", Path.c_str());
                }

                ImGui::PopID();
            }

            if (!ScanMessage.empty())
            {
                ImGui::TextWrapped("%s", ScanMessage.c_str());
            }
            else if (Paths.empty())
            {
                ImGui::TextUnformatted("No .animblueprint files in Assets.");
            }

            ImGui::EndCombo();
        }

        if (!Component->GetAnimBlueprintPath().empty() && ImGui::SmallButton("Reload Animation Blueprint"))
        {
            Component->ReloadAnimBlueprint();
        }

        const FString& Error = Component->GetAnimBlueprintError();

        if (!Error.empty())
        {
            ImGui::TextWrapped("%s", Error.c_str());
        }
    }

	void DrawAnimationSlot(USkeletalMeshComponent* Component)
	{
		auto* Instance = Component->GetSingleNodeInstance();
		UAnimationAsset* Current = Instance ? Instance->GetCurrentAsset() : nullptr;

		const FString Name = Current ? GetAssetDisplayName(Current->GetAssetPath()) : "None";

		ImGui::SetNextItemWidth(-1.0f);
		if (!ImGui::BeginCombo("##SingleAnimAsset", Name.c_str()))
		{
			return;
		}

		if (ImGui::Selectable("None", Current == nullptr))
		{
			Component->SetAnimation(nullptr);
			Component->ResetPose();
			Component->RefreshBoneTransformsNow();
		}

		USkeletalMesh* Mesh = Component->GetSkeletalMesh();
		if (Mesh)
		{
			FString FilePath = FAssetUtils::ResolveAssetFilePath(Mesh->GetPath());

			if (FilePath.empty())
			{
				FilePath = Mesh->GetPath();
			}

			const std::filesystem::path Directory = std::filesystem::path(FilePath).parent_path();
			if (!Directory.empty() && std::filesystem::is_directory(Directory))
			{
				for (const auto& Entry : std::filesystem::directory_iterator(Directory))
				{
					if (!Entry.is_regular_file())
					{
						continue;
					}

					const FString Path = Entry.path().generic_string();
					if (!AnimSequenceFile::IsAnimSequencePath(Path))
					{
						continue;
					}

					ImGui::PushID(Path.c_str());

					if (ImGui::Selectable(GetAssetDisplayName(Path).c_str()))
					{
						if (UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Path))
						{
							Component->PlayAnimation(Sequence, true);
						}
					}
					ImGui::PopID();
				}
			}
		}
		ImGui::EndCombo();
	}

	// UParticleSystem* 프로퍼티 (PSC::Template 등). 바꾸면 true를 돌려 PostEditChangeProperty로 재초기화를 맡긴다.
	bool DrawParticleSystemSlot(UParticleSystem** SystemPtr)
	{
		static std::vector<FString> Paths;   // .particle 파일 키. 콤보를 열 때마다 다시 훑는다
		UParticleSystem* Current = *SystemPtr;
		const FString CurrentPath = UAssetManager::FindParticleSystemPath(Current);
		const FString CurrentName = !Current ? "None" : (CurrentPath.empty() ? Current->GetName() : GetAssetDisplayName(CurrentPath));

		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen = ImGui::BeginCombo("##ParticleSystem", CurrentName.c_str());
		if (ImGui::IsItemHovered() && !CurrentPath.empty()) ImGui::SetTooltip("%s", CurrentPath.c_str());
		if (!bOpen) return false;

		if (ImGui::IsWindowAppearing())
		{
			Paths.clear();
			std::error_code Error;
			std::filesystem::recursive_directory_iterator It("Assets", Error);
			const std::filesystem::recursive_directory_iterator End;
			while (!Error && It != End)
			{
				if (It->is_regular_file(Error))
				{
					const FString Path = FPaths::ToUtf8(It->path());
					if (ParticleSystemFile::IsParticleSystemPath(Path))
						Paths.push_back(FAssetUtils::GetAssetKey(Path));
				}
				if (!Error) It.increment(Error);
			}
			std::sort(Paths.begin(), Paths.end());
		}

		bool bChanged = false;
		if (ImGui::Selectable("None", Current == nullptr) && Current)
		{
			*SystemPtr = nullptr;
			bChanged = true;
		}
		for (const FString& Path : Paths)
		{
			ImGui::PushID(Path.c_str());
			const bool bSelected = Path == CurrentPath;
			if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected) && !bSelected)
			{
				if (UParticleSystem* Picked = UAssetManager::LoadParticleSystem(Path))   // 이미 로드됐으면 같은 객체
				{
					*SystemPtr = Picked;
					bChanged = true;
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
			if (bSelected) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}
		ImGui::EndCombo();
		return bChanged;
	}

	void DrawMeshSlot(UObject* Owner, UStaticMesh** StaticMeshPtr)
	{
		UStaticMesh* Current = *StaticMeshPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##StaticMesh", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UStaticMesh> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Owner))
						MeshComponent->SetStaticMesh(*Itr);
					else
						*StaticMeshPtr = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}

			// 아직 로드되지 않은 메시는 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::StaticMesh))
			{
				ImGui::PushID(ItemPath.c_str());
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
				{
					if (UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>(ItemPath))
					{
						if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Owner))
							MeshComponent->SetStaticMesh(Mesh);
						else
							*StaticMeshPtr = Mesh;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	UMaterial* EnsureMaterialOverride(UMeshComponent* MeshComponent, int32 Slot, UMaterial* Effective, UMaterial* Override)
	{
		// 이 컴포넌트 전용 인스턴스가 아니면 지금 적용 중인 머티리얼을 복제해서 덮어쓰기로 만든다.
		// 메시의 머티리얼은 같은 메시를 쓰는 모든 액터가 공유하므로 직접 고치면 안 된다.
		if (Override && Override->bIsInstance)	return Override;

		UMaterial* Source = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

		Override = UMaterial::CreateInstance(Source);
		MeshComponent->SetMaterial(Slot, Override);

		return Override;
	}

	bool SetMaterialTexture(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, UTexture2D* Texture, UMaterial*& Effective, UMaterial*& Override) {
		UTexture2D* Current{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };
		if (Current == Texture) {
			return false;
		}

		Override = EnsureMaterialOverride(MeshComponent, MaterialSlot, Effective, Override);
		if (!Override) {
			return false;
		}

		while (!Override->Textures.IsValidIndex(TextureSlot)) {
			Override->Textures.Add(nullptr);
		}
		Override->Textures[TextureSlot] = Texture;
		Effective = Override;
		return true;
	}

	bool DrawTexturePicker(UTexture2D* Current, const char* Label,
		const std::function<void(UTexture2D*)>& OnPick)
	{
		const float ThumbnailSize{ 64.0f };
		const float ItemThumbnailSize{ 24.0f };

		bool bChanged{ false };

		if (Current && Current->GetResource())
		{
			ImGui::Image((ImTextureID)Current->GetResource()->GetSRV(), { ThumbnailSize, ThumbnailSize });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Current->GetPath().c_str());
			}
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload * Payload{ ImGui::AcceptDragDropPayload(EditorDragDrop::Texture) })
			{
				UTexture2D* DroppedTexture{ *static_cast<UTexture2D**>(Payload->Data) };
				if (DroppedTexture != Current)
				{
					OnPick(DroppedTexture);
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		const FString CurrentPath{ Current ? Current->GetPath() : "" };
		const FString CurrentFileName{ Current ? GetAssetDisplayName(CurrentPath) : "None" };

		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen{ ImGui::BeginCombo(Label, CurrentFileName.c_str()) };
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			if (ImGui::Selectable("None", Current == nullptr) && Current != nullptr)
			{
				OnPick(nullptr);
				bChanged = true;
			}

			// 경로 없는 런타임 텍스처(렌더타겟 등)는 목록에서 뺀다.
			for (TObjectIterator<UTexture2D> Iterator{}; Iterator; ++Iterator)
			{
				UTexture2D* ItemTexture{ *Iterator };
				if (!ItemTexture->GetResource() || ItemTexture->GetPath().empty())
				{
					continue;
				}

				const bool bSelected{ ItemTexture == Current };
				const FString ItemPath{ ItemTexture->GetPath() };
				const FString ItemFileName{ GetAssetDisplayName(ItemPath) };

				ImGui::PushID(ItemTexture);
				ImGui::Image((ImTextureID)ItemTexture->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(ItemFileName.c_str(), bSelected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize })
					&& !bSelected)
				{
					OnPick(ItemTexture);
					bChanged = true;
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}

			// 아직 로드되지 않은 텍스처는 썸네일 없이 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Texture))
			{
				ImGui::PushID(ItemPath.c_str());
				ImGui::Dummy({ ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize }))
				{
					if (UTexture2D* ItemTexture{ UAssetManager::GetAssetByPath<UTexture2D>(ItemPath) })
					{
						OnPick(ItemTexture);
						bChanged = true;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		return bChanged;
	}

	void DrawMaterialTextureSlot(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, const char* Label, UMaterial*& Effective, UMaterial*& Override) {
		const float ThumbnailSize{ 64.0f };
		const float ItemThumbnailSize{ 24.0f };
		UTexture2D* Texture{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::PushID(TextureSlot);

		DrawTexturePicker(Texture, "##Texture",
			[&](UTexture2D* Picked)
			{
				SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, Picked, Effective, Override);
			});

		ImGui::PopID();
	}

	// 메시 컴포넌트의 머티리얼 슬롯을 한 줄씩 그린다.
	// 텍스처를 드롭하면 그 슬롯의 덮어쓰기에 반영하고, 메시 에셋의 머티리얼은 건드리지 않는다.
	void DrawMaterialSlots(UMeshComponent* MeshComponent)
	{
		const int32 NumSlots = MeshComponent->GetNumMaterials();
		if (NumSlots <= 0)
		{
			return;
		}

		if (!CategoryHeader("Materials"))
		{
			return;
		}

		// 메쉬 내의 매터리얼 별 Slot 순회
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			ImGui::PushID(Slot);

			FString SlotLabel = std::format("[{}] {}", Slot, MeshComponent->GetMaterialSlotName(Slot));
			bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

			UMaterial* Override = MeshComponent->GetOverrideMaterial(Slot);
			UMaterial* Effective = MeshComponent->GetMaterial(Slot);

			ImGui::SameLine();

			if (Override)
			{
				ImGui::TextDisabled("Override");
				ImGui::SameLine();

				if (ImGui::SmallButton("Reset"))
				{
					MeshComponent->SetMaterial(Slot, nullptr);
					Override = nullptr;
					Effective = MeshComponent->GetMaterial(Slot);
				}
			}
			else
			{
				ImGui::TextDisabled("Default");
			}

			if (bOpen)
			{
				if (ImGui::BeginTable("MaterialProperties", 2))
				{
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					DrawMaterialTextureSlot(MeshComponent, Slot, 0, "Texture", Effective, Override);
					DrawMaterialTextureSlot(MeshComponent, Slot, 1, "Normal Map", Effective, Override);

					ImGui::TableNextRow();

					// 3: Base Color
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Base Color");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector4 TempColor = Effective ? Effective->BaseColor : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					if (ImGui::ColorEdit4("##BaseColor", &TempColor.X))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->BaseColor = TempColor;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Shininess");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					float TempShininess = Effective ? Effective->Shininess : 32.0f;
					if (ImGui::DragFloat("##Shininess", &TempShininess, 0.5f, 1.0f, 256.0f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->Shininess = TempShininess;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 4: UV Scroll Speed
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("UV Scroll");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector2 TempUVScrollSpeed = Effective ? Effective->UVScrollSpeed : FVector2();
					if (ImGui::DragFloat2("##UVScrollSpeed", &TempUVScrollSpeed.X, 0.01f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->UVScrollSpeed = TempUVScrollSpeed;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 5: Sampler State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Sampler");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
					int SamplerIndex = Effective ? static_cast<int>(Effective->SamplerState) : static_cast<int>(ESamplerState::LinearClamp);
					if (ImGui::BeginCombo("##SamplerState", SamplerItems[SamplerIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(SamplerItems); ++i)
						{
							if (ImGui::Selectable(SamplerItems[i], SamplerIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SamplerState = static_cast<ESamplerState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::TableNextRow();

					// 6: Blend State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Blend Mode");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* BlendItems[] = { "Opaque", "Alpha Blend" };
					int BlendIndex = Effective ? static_cast<int>(Effective->BlendState) : static_cast<int>(EBlendState::Opaque);
					if (ImGui::BeginCombo("##BlendState", BlendItems[BlendIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
						{
							if (ImGui::Selectable(BlendItems[i], BlendIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->BlendState = static_cast<EBlendState>(i);
									if (Override->PSOType == EPSOType::StaticMesh_Opaque || Override->PSOType == EPSOType::StaticMesh_Translucent)
									{
										const EPSOType MeshPSO = Override->BlendState == EBlendState::AlphaBlend
											? EPSOType::StaticMesh_Translucent : EPSOType::StaticMesh_Opaque;
										if (Override->PSOType != MeshPSO)
										{
											Override->PSOType = MeshPSO;
											MeshComponent->SetMaterial(Slot, Override);
										}
									}
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					if (Effective && Effective->PSOType == EPSOType::StaticMesh_Translucent)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted("Before DOF");
						ImGui::TableSetColumnIndex(1);
						bool bBeforeDOF = Effective->bRenderBeforeDOF;
						if (ImGui::Checkbox("##BeforeDOF", &bBeforeDOF))
						{
							Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
							if (Override)
							{
								Override->bRenderBeforeDOF = bBeforeDOF;
								Effective = Override;
							}
						}
					}

					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();

			ImGui::Separator();
		}
	}

	uint32 ToUIntBound(float Value)
	{
		if (Value <= 0.0f) return 0;
		if (Value >= static_cast<float>(UINT_MAX)) return UINT_MAX;
		return static_cast<uint32>(Value);
	}

	bool DrawPropertyRow(UObject* Object, void* Container, const FProperty& Property, ImFont* CustomFont, const char* DisplayNameOverride = nullptr);

	// FPropertyEditor::RegisterStructMemberRule로 등록한 구조체별 멤버 표시 규칙
	std::unordered_map<const UStruct*, FPropertyEditor::FStructMemberRule>& GetStructMemberRules()
	{
		static std::unordered_map<const UStruct*, FPropertyEditor::FStructMemberRule> Rules;
		return Rules;
	}

	// FPropertyEditor::RegisterClassPicker로 등록한 "클래스 이름" 프로퍼티와 고를 수 있는 부모 클래스
	std::unordered_map<const FProperty*, UClass*>& GetClassPickers()
	{
		static std::unordered_map<const FProperty*, UClass*> Pickers;
		return Pickers;
	}

	// "ADefaultPawn" -> "DefaultPawn". UE 클래스 고르기처럼 접두사를 뗀다.
	const char* ShortClassName(const FString& Name)
	{
		if (Name.size() > 1 && (Name[0] == 'A' || Name[0] == 'U') && std::isupper(static_cast<unsigned char>(Name[1])))
			return Name.c_str() + 1;
		return Name.c_str();
	}

	// BaseClass와 그 자식 중에서 고르는 콤보. None은 빈 문자열이다. 바뀌면 true.
	bool DrawClassPickerCombo(const char* Label, UClass* BaseClass, FString& InOutClassName)
	{
		TArray<UClass*> Classes;
		Classes.Add(BaseClass);
		GetDerivedClasses(BaseClass, Classes);
		std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });

		FString Preview = "None";
		if (!InOutClassName.empty())
			Preview = FString(ShortClassName(InOutClassName)) + (FindClass(InOutClassName) ? "" : " (Missing)");

		bool bChanged = false;
		if (ImGui::BeginCombo(Label, Preview.c_str(), ImGuiComboFlags_HeightLarge))
		{
			if (ImGui::Selectable("None", InOutClassName.empty()))
			{
				InOutClassName.clear();
				bChanged = true;
			}
			ImGui::Separator();
			for (UClass* Class : Classes)
			{
				// 추상 클래스는 만들 수 없으므로 고를 수 없다.
				if (!Class || !Class->Constructor)
					continue;
				ImGui::PushID(Class);
				if (ImGui::Selectable(ShortClassName(Class->Name), InOutClassName == Class->Name))
				{
					InOutClassName = Class->Name;
					bChanged = true;
				}
				ImGui::SetItemTooltip("%s", Class->Name.c_str());
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	// 프로퍼티 타입에 맞는 위젯 하나를 그린다. 값이 바뀌면 true
	// UE도 FProperty는 위젯을 모르고, 에디터(PropertyEditor 모듈)가 CastField로 타입을 보고 위젯을 고른다.
	bool DrawPropertyValue(UObject* Object, const FProperty& Property, void* ValuePtr, const char* Label, ImFont* CustomFont)
	{
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(&Property))
		{
			float* Value = &FloatProperty->GetPropertyValue(ValuePtr);
			const FDragSettings Drag = GetDragSettings(FloatProperty->Meta, 0.1f);
			const bool bChanged = ImGui::DragFloat(Label, Value, Drag.Speed, Drag.Min, Drag.Max);
			if (bChanged) ClampToMeta(Value, 1, FloatProperty->Meta);
			return bChanged;
		}
		if (const FIntProperty* IntProperty = CastField<FIntProperty>(&Property))
		{
			int32* Value = &IntProperty->GetPropertyValue(ValuePtr);
			const FPropertyMeta& Meta = IntProperty->Meta;
			const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
			const bool bChanged = ImGui::DragInt(Label, Value, Drag.Speed, ToIntBound(Drag.Min), ToIntBound(Drag.Max));
			if (bChanged && Meta.HasClamp())
			{
				*Value = ToIntBound(std::clamp(static_cast<float>(*Value), Meta.ClampMin, Meta.ClampMax));
			}
			return bChanged;
		}
		if (const FUInt32Property* UIntProperty = CastField<FUInt32Property>(&Property))
		{
			uint32* Value = &UIntProperty->GetPropertyValue(ValuePtr);
			const FPropertyMeta& Meta = UIntProperty->Meta;
			const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
			const uint32 Min = ToUIntBound(Drag.Min);
			const uint32 Max = Drag.Min < Drag.Max ? ToUIntBound(Drag.Max) : UINT_MAX;
			const bool bChanged = ImGui::DragScalar(Label, ImGuiDataType_U32, Value, Drag.Speed, &Min, &Max);
			if (bChanged && Meta.HasClamp())
			{
				*Value = ToUIntBound(std::clamp(static_cast<float>(*Value), Meta.ClampMin, Meta.ClampMax));
			}
			return bChanged;
		}
		if (const FVectorProperty* VectorProperty = CastField<FVectorProperty>(&Property))
		{
			FVector& Value = VectorProperty->GetPropertyValue(ValuePtr);
			const FDragSettings Drag = GetDragSettings(VectorProperty->Meta, 0.1f);
			const bool bChanged = DrawAxisFields(Label, Value.V, 3, Drag.Speed, Drag.Min, Drag.Max);
			if (bChanged) ClampToMeta(Value.V, 3, VectorProperty->Meta);
			return bChanged;
		}
		if (const FRotatorProperty* RotatorProperty = CastField<FRotatorProperty>(&Property))
		{
			FRotator& Value = RotatorProperty->GetPropertyValue(ValuePtr);
			// Transform의 Rotation과 같은 X/Y/Z 축 순서로 보여준다.
			float Euler[3] = { Value.Roll, Value.Pitch, Value.Yaw };
			const FDragSettings Drag = GetDragSettings(RotatorProperty->Meta, 0.1f);
			if (DrawAxisFields(Label, Euler, 3, Drag.Speed, Drag.Min, Drag.Max))
			{
				ClampToMeta(Euler, 3, RotatorProperty->Meta);
				Value.Roll = Euler[0];
				Value.Pitch = Euler[1];
				Value.Yaw = Euler[2];
			}
			return false;
		}
		if (const FVector4Property* Vector4Property = CastField<FVector4Property>(&Property))
		{
			FVector4& Value = Vector4Property->GetPropertyValue(ValuePtr);
			const FDragSettings Drag = GetDragSettings(Vector4Property->Meta, 0.1f);
			const bool bChanged = DrawAxisFields(Label, &Value.X, 4, Drag.Speed, Drag.Min, Drag.Max);
			if (bChanged) ClampToMeta(&Value.X, 4, Vector4Property->Meta);
			return bChanged;
		}
		if (const FVector2Property* Vector2Property = CastField<FVector2Property>(&Property))
		{
			FVector2& Value = Vector2Property->GetPropertyValue(ValuePtr);
			const FDragSettings Drag = GetDragSettings(Vector2Property->Meta, 0.1f);
			const bool bChanged = ImGui::DragFloat2(Label, &Value.X, Drag.Speed, Drag.Min, Drag.Max);
			if (bChanged) ClampToMeta(&Value.X, 2, Vector2Property->Meta);
			return bChanged;
		}
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(&Property))
		{
			return ImGui::Checkbox(Label, &BoolProperty->GetPropertyValue(ValuePtr));
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
		{
			// UENUM: 이름 목록으로 콤보박스를 그린다. UMETA(Hidden) 항목은 목록에서 뺀다.
			const FEnumInfo* EnumInfo = EnumProperty->Enum;
			if (!EnumInfo)
			{
				ImGui::TextDisabled("(No enum info)");
				return false;
			}
			bool bChanged = false;
			const int64 Current = EnumProperty->GetValue(ValuePtr);
			const FString Preview = EnumInfo->ToName(Current);
			if (ImGui::BeginCombo(Label, Preview.c_str()))
			{
				for (const FEnumEntry& Entry : EnumInfo->Entries)
				{
					if (Entry.bHidden)
					{
						continue;
					}
					const bool bSelected = Entry.Value == Current;
					if (ImGui::Selectable(Entry.Name.c_str(), bSelected) && !bSelected)
					{
						EnumProperty->SetValue(ValuePtr, Entry.Value);
						bChanged = true;
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			return bChanged;
		}
		if (const FColorProperty* ColorProperty = CastField<FColorProperty>(&Property))
		{
			FColor& C = ColorProperty->GetPropertyValue(ValuePtr);
			float V[4] = { C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f };
			if (ImGui::ColorEdit4(Label, V))
			{
				C = FColor(uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
					uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f));
				return true;
			}
			return false;
		}
		if (const FLinearColorProperty* LinearColorProperty = CastField<FLinearColorProperty>(&Property))
		{
			return ImGui::ColorEdit4(Label, LinearColorProperty->GetPropertyValue(ValuePtr).RGBA);
		}
		if (const FStrProperty* StrProperty = CastField<FStrProperty>(&Property))
		{
			FString& Value = StrProperty->GetPropertyValue(ValuePtr);

			// 클래스 이름 칸이면 문자열 입력 대신 클래스 고르기 콤보를 쓴다.
			if (auto Picker = GetClassPickers().find(&Property); Picker != GetClassPickers().end())
				return DrawClassPickerCombo(Label, Picker->second, Value);

			char Buffer[256] = {};
			strncpy_s(Buffer, Value.c_str(), sizeof(Buffer) - 1);
			if (CustomFont) ImGui::PushFont(CustomFont);
			if (ImGui::InputText(Label, Buffer, sizeof(Buffer)))
			{
				Value = Buffer;
			}
			if (CustomFont) ImGui::PopFont();
			return false;
		}
		if (const FNameProperty* NameProperty = CastField<FNameProperty>(&Property))
		{
			FName& Value = NameProperty->GetPropertyValue(ValuePtr);

			// FName은 입력할 때마다 이름 테이블에 등록되므로 편집이 끝났을 때만 바꾼다.
			char Buffer[256] = {};
			strncpy_s(Buffer, Value.IsNone() ? "" : Value.ToString().c_str(), sizeof(Buffer) - 1);
			if (CustomFont) ImGui::PushFont(CustomFont);
			const bool bChanged = ImGui::InputText(Label, Buffer, sizeof(Buffer), ImGuiInputTextFlags_EnterReturnsTrue)
				|| (ImGui::IsItemDeactivatedAfterEdit());
			if (CustomFont) ImGui::PopFont();
			if (bChanged)
			{
				Value = Buffer[0] ? FName(Buffer) : NAME_None;
				return true;
			}
			return false;
		}
		if (const FTransformProperty* TransformProperty = CastField<FTransformProperty>(&Property))
		{
			FTransform& Value = TransformProperty->GetPropertyValue(ValuePtr);

			ImGui::NewLine();

			bool bChanged = false;
			bChanged |= DrawVector3Controller("Location", Value.Location.V, 0.0f, 70.0f);
			// 드래그 중 값이 튀지 않도록 프로퍼티마다 마지막 오일러 값을 기억한다
			static TMap<const void*, FRotationConversionCache> RotationCaches;
			FRotationConversionCache& RotationCache = RotationCaches[ValuePtr];
			FRotator Euler = RotationCache.QuatToRotator(Value.Rotation);
			if (DrawRotatorAsXYZ("Rotation", Euler))
			{
				Value.Rotation = RotationCache.RotatorToQuat(Euler);
				bChanged = true;
			}
			bChanged |= DrawVector3Controller("Scale", Value.Scale.V, 1.0f, 70.0f);
			return bChanged;
		}
		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(&Property))
		{
			UObject** ObjPtr = &ObjectProperty->GetPropertyValue(ValuePtr);
			const UClass* PropertyClass = ObjectProperty->PropertyClass;

			if (PropertyClass == UMaterial::StaticClass())
			{
				DrawTextureSlot(reinterpret_cast<UMaterial**>(ObjPtr), 0);
			}
			else if (PropertyClass == UFont::StaticClass())
			{
				DrawFontSlot(Object, reinterpret_cast<UFont**>(ObjPtr));
			}
			else if (PropertyClass == UStaticMesh::StaticClass())
			{
				DrawMeshSlot(Object, reinterpret_cast<UStaticMesh**>(ObjPtr));
			}
			else if (PropertyClass == UParticleSystem::StaticClass())
			{
				return DrawParticleSystemSlot(reinterpret_cast<UParticleSystem**>(ObjPtr));
			}
			else if (PropertyClass == UTexture2D::StaticClass())
			{
				UTexture2D** TexturePtr = reinterpret_cast<UTexture2D**>(ObjPtr);
				return DrawTexturePicker(*TexturePtr, Label,
					[TexturePtr](UTexture2D* Picked) { *TexturePtr = Picked; });
			}
			return false;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			// USTRUCT: 구조체의 프로퍼티를 한 단계 들여 그린다. 컨테이너가 객체가 아니라 이 멤버의 주소다.
			ImGui::NewLine();
			ImGui::Indent();
			ImGui::PushID(Label);

			TArray<const UStruct*> StructChain;
			for (const UStruct* Struct = StructProperty->Struct; Struct; Struct = Struct->GetSuperStruct())
			{
				StructChain.Add(Struct);
			}

			const auto& Rules = GetStructMemberRules();
			const auto RuleIt = Rules.find(StructProperty->Struct);
			const FPropertyEditor::FStructMemberRule* Rule = RuleIt != Rules.end() ? &RuleIt->second : nullptr;

			bool bChanged = false;
			for (auto It = StructChain.rbegin(); It != StructChain.rend(); ++It)
			{
				for (const FProperty* Member : (*It)->GetProperties())
				{
					if (!Member->HasAnyFlags(CPF_Edit))
					{
						continue;
					}
					FPropertyEditor::FStructMemberDisplay Display;
					if (Rule) Display = (*Rule)(ValuePtr, *Member);
					if (!Display.bVisible)
					{
						continue;
					}
					const bool bReadOnly = Member->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					bChanged |= DrawPropertyRow(Object, ValuePtr, *Member, CustomFont, Display.Label);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}

			ImGui::PopID();
			ImGui::Unindent();
			return bChanged;
		}

		ImGui::TextDisabled("(Unsupported %s)", Property.GetClass()->GetName());
		return false;
	}

	// 프로퍼티 한 줄: 라벨과 값 위젯. 값이 바뀌면 true
	// Container는 프로퍼티를 가진 메모리 (UObject 멤버면 객체, USTRUCT 멤버면 그 구조체 값)
	bool DrawPropertyRow(UObject* Object, void* Container, const FProperty& Property, ImFont* CustomFont, const char* DisplayNameOverride)
	{
		void* ValuePtr = Property.ContainerPtrToValuePtr(Container);
		const FString Label = "##" + Property.Name;

		const char* DisplayName = DisplayNameOverride ? DisplayNameOverride : Property.Name.c_str();

		// 패널이 넓으면 라벨 | 값을 한 줄에, 좁거나 라벨이 길면 라벨 아래 줄에 값을 놓는다.
		const float StartX = ImGui::GetCursorPosX();
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		const float LabelWidth = AvailableWidth * 0.45f;

		const bool bInline = !Property.IsA<FTransformProperty>() && !Property.IsA<FStructProperty>() && AvailableWidth >= 260.0f && ImGui::CalcTextSize(DisplayName).x < LabelWidth;

		ImGui::AlignTextToFramePadding();
		ImGui::TextWrapped("%s", DisplayName);

		// SameLine(x)는 테이블 칸 안에서 칸 시작 위치를 한 번 더 더해 값 위젯이 칸 밖으로 밀려난다.
		// StartX는 이미 칸 시작을 포함한 창 기준 좌표이므로 SetCursorPosX로 직접 옮긴다.
		if (bInline)
		{
			ImGui::SameLine();
			ImGui::SetCursorPosX(StartX + LabelWidth);
		}

		ImGui::SetNextItemWidth(-1.0f);

		const bool bChanged = DrawPropertyValue(Object, Property, ValuePtr, Label.c_str(), CustomFont);

		// UE5처럼 라벨 칸과 값 칸 사이에 세로 구분선을 긋는다.
		if (bInline)
		{
			ImGuiWindow* Window = ImGui::GetCurrentWindow();
			const float LineX = Window->Pos.x - Window->Scroll.x + StartX + LabelWidth - ImGui::GetStyle().ItemSpacing.x * 0.5f - 2.0f;
			const float HalfGap = ImGui::GetStyle().ItemSpacing.y * 0.5f;
			Window->DrawList->AddLine(ImVec2(LineX, ImGui::GetItemRectMin().y - HalfGap),
				ImVec2(LineX, ImGui::GetItemRectMax().y + HalfGap), ImGui::GetColorU32(ImGuiCol_Border));
		}
		return bChanged;
	}

	// UClass에 등록된 프로퍼티 한 줄. 구조체 멤버 하나가 바뀌어도 객체에는 바깥 프로퍼티(구조체)가 바뀌었다고 알린다.
	// (UE는 FPropertyChangedEvent에 MemberProperty와 안쪽 Property를 따로 담는다)
	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		if (DrawPropertyRow(Object, Object, Property, CustomFont))
		{
			Object->PostEditChangeProperty(Property);
		}
	}

	// 클래스 계층을 따라 올라가며 각 단계의 프로퍼티를 표시
	void DrawProperties(UObject* Object, ImFont* CustomFont)
	{
		if (!Object)
		{
			return;
		}

		TArray<UClass*> ClassChain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			ClassChain.Add(Class);
		}

		// 기반 클래스부터 표시
		for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
		{
			UClass* Class = *It;

			// 노출할 프로퍼티가 하나도 없으면 헤더도 그리지 않는다 (검색 중이면 검색어에 맞는 것만 센다)
			auto IsShown = [](const FProperty* Property)
			{
				return Property->HasAnyFlags(CPF_Edit) && EditorIcons::MatchesFilter(Property->Name.c_str(), GPropertyFilter);
			};
			bool bHasVisibleProperty = false;
			for (const FProperty* Property : Class->GetProperties())
			{
				if (IsShown(Property))
				{
					bHasVisibleProperty = true;
					break;
				}
			}
			if (!bHasVisibleProperty)
			{
				continue;
			}

			ImGui::PushID(Class->Name.c_str());
			// 검색 중에는 접어 둔 카테고리도 펼쳐서 결과를 보여 준다.
			const bool bFiltering = GPropertyFilter && GPropertyFilter[0];
			if (bFiltering)
				ImGui::SetNextItemOpen(true);
			if (CategoryHeader(Class->Name.c_str()))
			{
				for (const FProperty* Property : Class->GetProperties())
				{
					if (!IsShown(Property))
					{
						continue;
					}

					const bool bReadOnly = Property->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					DrawProperty(Object, *Property, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}
			ImGui::PopID();
		}
	}

	// ---------------- 컴포넌트 아이콘과 Add 메뉴 ----------------
	// 아이콘, 보기 좋은 이름, 분류는 Outliner와 함께 쓰도록 ComponentTypeInfo에 있다.
	using ComponentTypeInfo::GetComponentIcon;
	using ComponentTypeInfo::GetComponentDisplayName;

	// 아이콘 + 글자 한 줄짜리 선택 항목. 눌리면 true. Suffix는 이름 뒤에 흐리게 붙는다.
	bool IconSelectable(const char* Id, EEditorIcon Icon, ImU32 IconColor, const char* Label, const char* Suffix, bool bSelected)
	{
		const float S = EditorIcons::GetScale();
		const float H = ImGui::GetTextLineHeight() + 6.0f * S;
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const bool bPressed = ImGui::Selectable(Id, bSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, H));

		ImDrawList* DL = ImGui::GetWindowDrawList();
		const float IconSize = 14.0f * S;
		const float MidY = Min.y + H * 0.5f;
		float X = Min.x + 4.0f * S;
		EditorIcons::Draw(DL, Icon, ImVec2(X + IconSize * 0.5f, MidY), IconSize, IconColor);
		X += IconSize + 6.0f * S;

		const float TextY = MidY - ImGui::GetFontSize() * 0.5f;
		DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_Text), Label);
		if (Suffix && Suffix[0])
		{
			X += ImGui::CalcTextSize(Label).x + 5.0f * S;
			DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_TextDisabled), Suffix);
		}
		return bPressed;
	}

	// 컴포넌트 트리 노드 줄에 아이콘, 이름, 흐린 종류 이름을 그린다. 이름 편집 중이면 아이콘만 그린다.
	// 이름 칸이 시작할 곳(라벨 시작에서 떨어진 거리)을 돌려준다.
	float DrawComponentRowLabel(const UActorComponent* Component, const ImVec2& Min, const ImVec2& Max, bool bDrawName)
	{
		const float S = EditorIcons::GetScale();
		const float IconSize = 14.0f * S;
		const float MidY = (Min.y + Max.y) * 0.5f;
		float X = Min.x + ImGui::GetTreeNodeToLabelSpacing();

		EEditorIcon Icon;
		ImU32 Color;
		GetComponentIcon(Component->GetClass(), Icon, Color);
		ImDrawList* DL = ImGui::GetWindowDrawList();
		EditorIcons::Draw(DL, Icon, ImVec2(X + IconSize * 0.5f, MidY), IconSize, Color);
		const float NameOffset = IconSize + 6.0f * S;
		X += NameOffset;

		if (bDrawName)
		{
			const float TextY = MidY - ImGui::GetFontSize() * 0.5f;
			const FString Name = Component->GetName();
			DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_Text), Name.c_str());
			X += ImGui::CalcTextSize(Name.c_str()).x + 5.0f * S;
			const FString Kind = "(" + GetComponentDisplayName(Component->GetClass()->Name) + ")";
			DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_TextDisabled), Kind.c_str());
		}
		return NameOffset;
	}

	// Add 메뉴의 분류 제목. UE5처럼 흐린 글자 아래에 얇은 선을 긋는다.
	void ComponentMenuSection(const char* Title)
	{
		const float S = EditorIcons::GetScale();
		ImGui::Dummy(ImVec2(0.0f, 2.0f * S));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2.0f * S);
		ImGui::TextDisabled("%s", Title);
		ImGui::Separator();
	}

	// Add 메뉴 팝업. 검색 칸에 쓴 글자가 이름(띄어 쓴 이름이나 클래스 이름)에 들어간 컴포넌트만 보여 준다.
	// Enter를 누르면 맨 위 항목을 붙인다. 붙인 컴포넌트를 돌려준다.
	UActorComponent* DrawAddComponentPopup(AActor* Actor, char* Search, size_t SearchSize)
	{
		const float S = EditorIcons::GetScale();
		UActorComponent* Added = nullptr;

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		EditorIcons::SearchBox("##ComponentSearch", Search, SearchSize, "Search Components");
		const bool bSearchEnter = (ImGui::IsItemActive() || ImGui::IsItemDeactivated()) &&
			(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

		// 메뉴에 내놓을 클래스를 분류별로 모은다. 목록에 있는 것은 목록 순서, 나머지(Other)는 이름순이다.
		TArray<UClass*> AllClasses;
		GetDerivedClasses(UActorComponent::StaticClass(), AllClasses);

		auto Matches = [Search](const FString& ClassName)
		{
			return EditorIcons::MatchesFilter(GetComponentDisplayName(ClassName).c_str(), Search) ||
				EditorIcons::MatchesFilter(ClassName.c_str(), Search);
		};

		UClass* Picked = nullptr;
		UClass* FirstMatch = nullptr;

		ImGui::BeginChild("##ComponentList", ImVec2(0.0f, 380.0f * S));
		int32 EntryCount = 0;
		int32 CategoryCount = 0;
		const ComponentTypeInfo::FEntry* Entries = ComponentTypeInfo::GetEntries(EntryCount);
		const char* const* Categories = ComponentTypeInfo::GetCategories(CategoryCount);
		for (int32 CategoryIndex = 0; CategoryIndex < CategoryCount; ++CategoryIndex)
		{
			const char* Category = Categories[CategoryIndex];
			TArray<UClass*> Classes;
			if (std::strcmp(Category, "Other") != 0)
			{
				for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
				{
					const ComponentTypeInfo::FEntry& Entry = Entries[EntryIndex];
					if (std::strcmp(Entry.Category, Category) != 0) continue;
					UClass* Class = FindClass(Entry.ClassName);
					if (Class && Class->IsChildOf(UActorComponent::StaticClass()) && Matches(Class->Name))
						Classes.Add(Class);
				}
			}
			else
			{
				for (UClass* Class : AllClasses)
				{
					if (!Class || ComponentTypeInfo::IsAbstractComponentClass(Class->Name) || ComponentTypeInfo::FindExactEntry(Class->Name))
						continue;
					if (Matches(Class->Name))
						Classes.Add(Class);
				}
				std::sort(Classes.begin(), Classes.end(),
					[](const UClass* A, const UClass* B) { return A->Name < B->Name; });
			}

			if (Classes.IsEmpty())
				continue;

			ComponentMenuSection(Category);
			for (UClass* Class : Classes)
			{
				if (!FirstMatch)
					FirstMatch = Class;

				EEditorIcon Icon;
				ImU32 Color;
				GetComponentIcon(Class, Icon, Color);
				const FString Label = GetComponentDisplayName(Class->Name);
				ImGui::PushID(Class);
				if (IconSelectable("##AddComponentItem", Icon, Color, Label.c_str(), nullptr, false))
					Picked = Class;
				ImGui::SetItemTooltip("%s", Class->Name.c_str());
				ImGui::PopID();
			}
		}
		if (!FirstMatch)
			ImGui::TextDisabled("No components match \"%s\"", Search);
		ImGui::EndChild();

		if (!Picked && bSearchEnter)
			Picked = FirstMatch;
		if (Picked)
		{
			// 이름은 비워 두어 겹치지 않는 이름을 받는다. 붙인 뒤 트리에서 바로 이름을 고칠 수 있다.
			Added = Actor->AddComponent(Picked, NAME_None);
			if (Added)
				FLevelUndoHistory::MarkDirty();
			ImGui::CloseCurrentPopup();
		}
		return Added;
	}

	void DrawActorHeader(AActor* Actor, UActorComponent*& OutSelectedComponent, UActorComponent*& OutAddedComponent)
	{
		if (!Actor) { return; }

		static char ActorNameBuf[256] = {};
		static char ComponentSearch[128] = {};

		// UE5 Details 맨 위: [액터 아이콘] [이름 입력 칸 ............] [+ Add]
		const float S = EditorIcons::GetScale();
		const float RowH = ImGui::GetFrameHeight() + 4.0f * S;
		const float LineY = ImGui::GetCursorPosY();
		const float AddWidth = EditorIcons::CalcButtonWidth(true, "Add", false, RowH);
		ImDrawList* DL = ImGui::GetWindowDrawList();

		// 액터 아이콘은 루트 컴포넌트 종류를 따른다.
		{
			EEditorIcon Icon;
			ImU32 Color;
			USceneComponent* Root = Actor->GetRootComponent();
			GetComponentIcon(Root ? Root->GetClass() : USceneComponent::StaticClass(), Icon, Color);
			const float IconSize = 18.0f * S;
			const ImVec2 P = ImGui::GetCursorScreenPos();
			EditorIcons::Draw(DL, Icon, ImVec2(P.x + IconSize * 0.5f, P.y + RowH * 0.5f), IconSize, Color);
			ImGui::Dummy(ImVec2(IconSize, RowH));
		}

		// 액터 이름은 입력 칸에서 바로 고친다. Enter나 다른 곳 클릭으로 확정, Esc로 되돌린다.
		// 입력 중이 아닐 때는 매 프레임 지금 이름으로 채워, 다른 곳(Outliner, 실행 취소)에서 바뀐 이름을 따라간다.
		ImGui::SameLine(0.0f, 6.0f * S);
		ImGui::SetCursorPosY(LineY + (RowH - ImGui::GetFrameHeight()) * 0.5f);
		const ImGuiID NameFieldId = ImGui::GetID("##ActorName");
		if (ImGui::GetActiveID() != NameFieldId)
			strncpy_s(ActorNameBuf, sizeof(ActorNameBuf), Actor->GetName().c_str(), _TRUNCATE);

		const bool bEditorActor = Actor->GetWorld() && Actor->GetWorld()->GetWorldType() == EWorldType::Editor;
		ImGui::SetNextItemWidth(ImMax(60.0f * S, ImGui::GetContentRegionAvail().x - AddWidth - 8.0f * S));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * S);
		ImGui::BeginDisabled(!bEditorActor);
		const bool bNameEntered = ImGui::InputText("##ActorName", ActorNameBuf, sizeof(ActorNameBuf),
			ImGuiInputTextFlags_EnterReturnsTrue);
		const bool bNameCommitted = bNameEntered || ImGui::IsItemDeactivatedAfterEdit();
		ImGui::EndDisabled();
		ImGui::PopStyleVar();
		if (bEditorActor)
			ImGui::SetItemTooltip("Actor name (UUID %u)", Actor->GetUUID());
		else
			ImGui::SetItemTooltip("Actors can be renamed only outside Play (UUID %u)", Actor->GetUUID());
		// 실패하면(빈 이름, 겹치는 이름) 다음 프레임에 원래 이름으로 돌아간다.
		if (bNameCommitted && bEditorActor)
			FEditorActorUtils::RenameActor(Actor, ActorNameBuf);

		// [+ Add] 버튼. UE5처럼 테두리 있는 버튼에 초록 + 를 단다.
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - AddWidth));
		ImGui::SetCursorPosY(LineY);
		const ImVec2 AddMin = ImGui::GetCursorScreenPos();
		const ImVec2 AddMax(AddMin.x + AddWidth, AddMin.y + RowH);
		DL->AddRectFilled(AddMin, AddMax, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f * S);
		DL->AddRect(AddMin, AddMax, ImGui::GetColorU32(ImGuiCol_Border), 4.0f * S);
		const bool bAddClicked = EditorIcons::Button("##AddComponent", EEditorIcon::Plus, EditorIcons::PlayGreen, "Add",
			false, false, RowH);
		ImGui::SetItemTooltip("Add a new component to this actor");
		ImGui::Dummy(ImVec2(0.0f, 2.0f * S));

		if (bAddClicked)
		{
			ComponentSearch[0] = '\0';
			ImGui::OpenPopup("AddComponentPopup");
		}

		// 버튼 바로 아래, 오른쪽 끝을 맞춰 연다.
		ImGui::SetNextWindowPos(ImVec2(AddMax.x, AddMax.y + 2.0f * S), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(300.0f * S, 0.0f), ImGuiCond_Appearing);
		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			if (UActorComponent* Added = DrawAddComponentPopup(Actor, ComponentSearch, sizeof(ComponentSearch)))
			{
				OutSelectedComponent = Added;
				OutAddedComponent = Added;
			}
			ImGui::EndPopup();
		}
		DrawScriptSection(Actor);
	}
}

// UI 에디터처럼 다른 패널도 같은 속성 편집 UI를 쓰도록 위 함수들을 공개한다.
// 보조 함수들이 이 파일의 익명 네임스페이스에 있어 구현은 옮기지 않고 넘겨 준다.
namespace FPropertyEditor
{
	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		::DrawProperty(Object, Property, CustomFont);
	}

	void DrawObjectProperties(UObject* Object, ImFont* CustomFont, const char* Filter)
	{
		const char* PrevFilter = GPropertyFilter;
		if (Filter)
			GPropertyFilter = Filter;
		DrawProperties(Object, CustomFont);
		GPropertyFilter = PrevFilter;
	}

	void DrawActorExtras(AActor* Actor)
	{
		if (Actor)
			DrawScriptSection(Actor);
	}

	void DrawComponentExtras(UActorComponent* Component)
	{
		if (auto* Skinned = Cast<USkinnedMeshComponent>(Component))
		{
			ImGui::TextUnformatted("Skeletal Mesh");
			DrawSkeletalMeshSlot(Skinned);
		}
		if (auto* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			ImGui::TextUnformatted("Animation Blueprint");
			DrawAnimBlueprintSlot(Skeletal);
			const bool bUseBlueprint = !Skeletal->GetAnimBlueprintPath().empty();
			ImGui::BeginDisabled(bUseBlueprint);
			ImGui::TextUnformatted("Single Anim Asset");
			DrawAnimationSlot(Skeletal);
			ImGui::EndDisabled();
		}
		if (auto* Mesh = Cast<UMeshComponent>(Component))
			DrawMaterialSlots(Mesh);
	}

	UActorComponent* DrawAddComponentMenu(AActor* Actor, char* Search, size_t SearchSize)
	{
		return Actor ? DrawAddComponentPopup(Actor, Search, SearchSize) : nullptr;
	}

	void RegisterStructMemberRule(const UStruct* Struct, FStructMemberRule Rule)
	{
		if (Struct) GetStructMemberRules()[Struct] = std::move(Rule);
	}

	void RegisterClassPicker(UClass* OwnerClass, const char* PropertyName, UClass* BaseClass)
	{
		const FProperty* Property = OwnerClass ? OwnerClass->FindPropertyByName(PropertyName) : nullptr;
		if (!Property || !CastField<FStrProperty>(Property) || !BaseClass)
		{
			HTR_LOG(Warning, "RegisterClassPicker: {}.{} is not a string property", OwnerClass ? OwnerClass->Name : FString("?"), PropertyName);
			return;
		}
		GetClassPickers()[Property] = BaseClass;
	}
}

bool FDetailsPanel::Init()
{
	ImGuiIO& io = ImGui::GetIO();

	io.Fonts->AddFontDefault();

	// 폰트 파일은 엔진 폴더 루트에 있다. 작업 디렉터리는 프로젝트 폴더이므로 절대 경로로 연다.
	const FString fontPath = FPaths::EngineFileUtf8("NanumSquareR.TTF");
	float fontSize = 15.0f;
	const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

	CustomFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize, nullptr, koreanRanges);
	assert(CustomFont != nullptr);

	return true;
}

void FDetailsPanel::Tick(float DeltaTime)
{
}

const ULightComponent* FDetailsPanel::GetSelectedLight() const
{
	UActorComponent* Component = TargetComponent;
	while (Component && Component->IsHiddenInDetails())
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		Component = SceneComponent ? SceneComponent->GetAttachParent() : nullptr;
	}
	return Cast<ULightComponent>(Component);
}

void FDetailsPanel::DrawShadowPreview()
{
	const ULightComponent* Light = GetSelectedLight();
	if (!Light || !QueryShadowPreview || !ImGuiRenderer) return;
	if (!CategoryHeader("Shadow Maps")) return;
	FTexture2DArray* Atlas = nullptr;
	TArray<FLightShadowRenderData> Shadows;
	if (!QueryShadowPreview(Light, Atlas, Shadows) || !Atlas || !Atlas->GetWidth() || !Atlas->GetHeight())
	{
		ImGui::TextDisabled("No shadow map available for this light in the active view.");
		return;
	}
	ImGui::SliderFloat("Preview Contrast", &ShadowPreviewContrast, 1.0f, 256.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	const float Width = float(Atlas->GetWidth());
	const float Height = float(Atlas->GetHeight());
	const float DisplayWidth = ImGui::GetContentRegionAvail().x;
	if (DisplayWidth <= 0) return;
	static const char* PointFaces[] = { "+X", "-X", "+Z", "-Z", "+Y", "-Y" };
	for (int32 i = 0; i < Shadows.Num(); ++i)
	{
		const auto& Shadow = Shadows[i];
		const FRect& Rect = Shadow.AtlasRect;
		if (Rect.Width <= 0 || Rect.Height <= 0) continue;
		ImGui::PushID(i);
		if (Light->GetLightType() == ELightType::Point)
			ImGui::Text("Face %s", PointFaces[i % 6]);
		else if (Light->GetLightType() == ELightType::Directional)
			ImGui::Text("Cascade %d", i + 1);
		ImGui::TextDisabled("Slice %u | %.0f x %.0f", Shadow.AtlasIndex, Rect.Width, Rect.Height);

		const ImVec2 UV0(Rect.X / Width, Rect.Y / Height);
		const ImVec2 UV1((Rect.X + Rect.Width) / Width, (Rect.Y + Rect.Height) / Height);

		if (!ImGuiRenderer->DrawShadowMap(Atlas->GetSRV(), Shadow.AtlasIndex,
			ImVec2(DisplayWidth, DisplayWidth * Rect.Height / Rect.Width), UV0, UV1, ShadowPreviewContrast))
			ImGui::TextDisabled("Shadow preview shader is unavailable.");
		ImGui::PopID();
	}
}


void FDetailsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);

	EditorPanelUI::BeginPanel("Details", GetOpenFlag());

	if (TargetActor)
	{
		// 빌보드처럼 디테일에 숨겨진 컴포넌트가 선택되면 노출되는 부모로 올린다.
		// (예: 라이트는 빌보드를 클릭해서 고르지만 수치는 SpotLight 쪽에 있다)
		while (TargetComponent && TargetComponent->IsHiddenInDetails())
		{
			USceneComponent* HiddenComponent = Cast<USceneComponent>(TargetComponent);
			TargetComponent = HiddenComponent ? HiddenComponent->GetAttachParent() : nullptr;
		}

		UActorComponent* SelectCandidateComponent = TargetComponent;

		UActorComponent* AddedComponent = nullptr;
		DrawActorHeader(TargetActor, TargetComponent, AddedComponent);
		// UE5처럼 방금 붙인 컴포넌트는 바로 이름을 고칠 수 있게 한다. 이름 편집은 트리에 있는 씬 컴포넌트만 된다.
		if (AddedComponent && Cast<USceneComponent>(AddedComponent))
		{
			EditingNameComponent = AddedComponent;
			strncpy_s(ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), AddedComponent->GetName().c_str(), _TRUNCATE);
			bFocusNameEdit = true;
		}
		DrawCompoenetList(TargetActor, TargetComponent);
		ImGui::Separator();

		// UE5처럼 프로퍼티 위에 검색 칸을 두고, 이름에 검색어가 들어간 프로퍼티만 보여 준다.
		EditorIcons::SearchBox("##DetailsSearch", PropertySearch, sizeof(PropertySearch));
		GPropertyFilter = PropertySearch;

		// 액터 -> 컴포넌트 순으로, 클래스별 프로퍼티 표시
		ImGui::PushID(TargetActor);
		DrawProperties(TargetActor, CustomFont);
		ImGui::PopID();

		if (TargetComponent)
		{
			ImGui::PushID(TargetComponent);
			ImGui::SeparatorText("Component Properties");
			DrawProperties(TargetComponent, CustomFont);
			if (auto* Component = Cast<USkinnedMeshComponent>(TargetComponent))
			{
				ImGui::TextUnformatted("Skeletal Mesh");
				DrawSkeletalMeshSlot(Component);
			}
			if (auto* Component = Cast<USkeletalMeshComponent>(TargetComponent))
			{
                ImGui::TextUnformatted("Animation Blueprint");
                DrawAnimBlueprintSlot(Component);
                const bool bUseBlueprint = !Component->GetAnimBlueprintPath().empty();
                ImGui::BeginDisabled(bUseBlueprint);
                ImGui::TextUnformatted("Single Anim Asset");
                DrawAnimationSlot(Component);
                ImGui::EndDisabled();

                if (bUseBlueprint)
                {
                    ImGui::TextWrapped("Choose None in Animation Blueprint to use a single animation.");
                }
			}
			DrawShadowPreview();

			if (SelectCandidateComponent != TargetComponent)
			{
				SelectComponent(TargetComponent);
			}
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TargetComponent))
			{
				DrawMaterialSlots(MeshComponent);
			}
			ImGui::PopID();

			if (PendingDeleteComponent)
			{
				if (DeleteCallback)
				{
					DeleteCallback(PendingDeleteComponent);
				}

				PendingDeleteComponent = nullptr;
			}
		}
		GPropertyFilter = nullptr;
	}
	// 프로퍼티 값, 이름, 에셋 슬롯, 부모 바꾸기 등 이 패널의 편집은 모두 여기서 잡는다.
	// 실제로 바뀌었는지는 실행 취소 기록이 확정할 때 월드를 비교해 가린다.
	FLevelUndoHistory::NotePanelInteraction();
	ImGui::End();
}


void FDetailsPanel::SelectComponent(UActorComponent* Component)
{
	if (!Component)
	{
		TargetComponent = nullptr;
		EditingNameComponent = nullptr;
		bFocusNameEdit = false;

		if (Callback)
			Callback(nullptr);

		return;
	}

	TargetComponent = Component;

	if (Callback)
		Callback(TargetComponent);
}

void FDetailsPanel::DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent)
{
	if (!SelectedActor) { return; }

	if (PendingDragComponent && PendingDroppedComponent)
	{
		TryReparent(PendingDroppedComponent, PendingDragComponent);
		PendingDragComponent = nullptr;
		PendingDroppedComponent = nullptr;
		FLevelUndoHistory::MarkDirty();
	}

	USceneComponent* Root = SelectedActor->GetRootComponent();
	const float S = EditorIcons::GetScale();

	// UE5처럼 테두리 있는 상자 안에 [액터 (Instance)] 줄과 컴포넌트 트리를 그린다.
	// 내용만큼 늘어나다가 너무 길어지면 상자 안에서 스크롤한다.
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * S, 4.0f * S));
	ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, 260.0f * S));
	ImGui::BeginChild("##ComponentTree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

	// 맨 위 줄은 액터 자신이다. 누르면 컴포넌트 선택을 풀고 액터 프로퍼티만 본다.
	bool bInstanceClicked = false;
	{
		EEditorIcon Icon;
		ImU32 Color;
		GetComponentIcon(Root ? Root->GetClass() : USceneComponent::StaticClass(), Icon, Color);
		const FString Label = SelectedActor->GetName();
		bInstanceClicked = IconSelectable("##ActorInstance", Icon, Color, Label.c_str(), "(Instance)", OutSelectedComponent == nullptr);
	}

	ImGui::Indent(14.0f * S);
	if (Root)
	{
		DrawSceneComponentNode(Root, Root, OutSelectedComponent);
	}

	// 위치가 없는 컴포넌트(이동, 스크립트 등)는 트리 아래에 따로 늘어놓는다.
	for (UActorComponent* ActorComp : SelectedActor->GetComponents())
	{
		if (!ActorComp || Cast<USceneComponent>(ActorComp))
		{
			continue;
		}

		ImGui::PushID(ActorComp);
		EEditorIcon Icon;
		ImU32 Color;
		GetComponentIcon(ActorComp->GetClass(), Icon, Color);
		const FString Name = ActorComp->GetName();
		const FString Kind = "(" + GetComponentDisplayName(ActorComp->GetClass()->Name) + ")";
		if (IconSelectable("##ActorComponentNode", Icon, Color, Name.c_str(), Kind.c_str(), ActorComp == OutSelectedComponent))
		{
			OutSelectedComponent = ActorComp;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete"))
				PendingDeleteComponent = ActorComp;
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::Unindent(14.0f * S);

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	if (bInstanceClicked)
	{
		// 선택을 비우면 에디터가 디테일 대상을 액터의 루트로 되돌리므로, 그 뒤에 컴포넌트 대상을 다시 비운다.
		SelectComponent(nullptr);
		TargetComponent = nullptr;
		OutSelectedComponent = nullptr;
	}
	ImGui::Dummy(ImVec2(0.0f, 2.0f * S));
}

void FDetailsPanel::DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent)
{
	// 빌보드 같은 에디터 표시용 컴포넌트는 트리에 노출하지 않는다
	if (!Component || Component->IsHiddenInDetails())
		return;

	// 숨겨진 자식만 있는 노드가 펼침 화살표를 갖지 않도록 노출되는 자식만 센다
	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (Child && !Child->IsHiddenInDetails())
		{
			Children.Add(Child);
		}
	}

	const bool bIsLeaf = Children.IsEmpty();
	bIsEditingName = (EditingNameComponent == Component);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (Component == OutSelectedComponent)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	ImGui::PushID(Component);
	// 글자는 비워 두고 아래에서 아이콘과 함께 직접 그린다. 펼침 화살표와 선택 영역은 트리 노드가 맡는다.
	const bool bOpen = ImGui::TreeNodeEx("ComponentNode", Flags, "%s", "");
	const ImVec2 NodeMin = ImGui::GetItemRectMin();
	const ImVec2 NodeMax = ImGui::GetItemRectMax();

	USceneComponent* DragComponent = Component;

	// source: Root 제외, 현재 Component를 payload에 담음
	if (Component != Root && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Component, &Component, sizeof(USceneComponent*));
		ImGui::EndDragDropSource();
	}
	// target: 모든 행이 가능, payload를 받아 현재 Component를 부모로 사용
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
		{
			USceneComponent* DroppedComponent = *static_cast<USceneComponent**>(Payload->Data);
			PendingDroppedComponent = DroppedComponent;
			PendingDragComponent = DragComponent;
		}
		ImGui::EndDragDropTarget();
	}

	if (!bIsEditingName)
	{
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			OutSelectedComponent = Component;

		if (Component != Root && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			EditingNameComponent = Component;
			strncpy_s(ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), Component->GetName().c_str(), _TRUNCATE);
			bFocusNameEdit = true;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, Component != Root))
				PendingDeleteComponent = Component;
			ImGui::EndPopup();
		}

		DrawComponentRowLabel(Component, NodeMin, NodeMax, true);
	}
	else
	{
		const float NameOffset = DrawComponentRowLabel(Component, NodeMin, NodeMax, false);
		ImGui::SameLine(0.0f, NameOffset);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (bFocusNameEdit)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusNameEdit = false;
		}

		const bool bConfirmed = ImGui::InputText("##Rename", ComponentNameEditBuffer,
			sizeof(ComponentNameEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		const bool bCancelled = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
		const bool bFinished = bConfirmed || ImGui::IsItemDeactivatedAfterEdit();
		if (bCancelled || bFinished)
		{
			if (!bCancelled && ComponentNameEditBuffer[0] != '\0')
			{
				FName NewName(ComponentNameEditBuffer);
				AActor* Owner = Component->GetOwner();

				UActorComponent* Existing = Owner->FindComponentByName(NewName);
				if (!Existing)
				{
					Component->SetName(FName(ComponentNameEditBuffer));
				}
				else
				{
					HTR_LOG(Warning, "A component with this name already exists in this Actor.");
				}
			}
			EditingNameComponent = nullptr;
		}
	}

	if (bOpen && !bIsLeaf)
	{
		for (USceneComponent* Child : Children)
		{
			DrawSceneComponentNode(Child, Root, OutSelectedComponent);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FDetailsPanel::TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent)
{
	if (DroppedComponent != nullptr
		&& DroppedComponent->GetAttachParent() != DragComponent
		&& DroppedComponent != DragComponent)
	{
		DroppedComponent->SetupAttachment(DragComponent, EAttachmentRule::KeepWorld);
	}
}


FDetailsPanel::~FDetailsPanel()
{
	//delete transform;
}
