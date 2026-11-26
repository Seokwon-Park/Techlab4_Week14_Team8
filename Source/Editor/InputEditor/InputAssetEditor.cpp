#include "EnginePCH.h"
#include "Editor/InputEditor/InputAssetEditor.h"
#include "Editor/InputEditor/InputAssetEditorTab.h"

#include <algorithm>
#include <filesystem>

#include "Core/EngineLog.h"
#include "Core/Misc/Paths.h"
#include "Input/InputAction.h"
#include "Input/InputAssetLibrary.h"
#include "Input/InputMappingContext.h"

FInputAssetEditor::FInputAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InAssetPath)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, Kind(FInputAssetLibrary::IsMappingContextPath(InAssetPath) ? EInputAssetKind::Context : EInputAssetKind::Action)
{
}

bool FInputAssetEditor::CanOpen(const FString& AssetPath)
{
	return FInputAssetLibrary::IsInputActionPath(AssetPath) || FInputAssetLibrary::IsMappingContextPath(AssetPath);
}

const char* FInputAssetEditor::GetEditorTypeName() const
{
	return Kind == EInputAssetKind::Action ? "InputAction" : "InputMappingContext";
}

bool FInputAssetEditor::InitializeResources()
{
	// 미리보기 장면은 만들지 않는다 (FAssetEditor::InitializeResources를 부르지 않는다)
	if (Kind == EInputAssetKind::Action)
		Action = FInputAssetLibrary::LoadInputAction(GetAssetPath());
	else
		Context = FInputAssetLibrary::LoadMappingContext(GetAssetPath());

	if (!Action && !Context)
	{
		HTR_LOG(Warning, "Input asset editor: cannot open {}", GetAssetPath());
		return false;
	}

	SavedText = Serialize();
	bDirty = false;
	return true;
}

void FInputAssetEditor::ShutdownResources()
{
	// 저장하지 않고 닫았다면 게임이 쓰는 캐시 객체를 디스크 내용으로 되돌린다
	if (bDirty) Revert();
	Action = nullptr;
	Context = nullptr;
}

TUniquePtr<IEditorTab> FInputAssetEditor::CreateTab()
{
	return MakeUnique<FInputAssetEditorTab>(*this);
}

FString FInputAssetEditor::Serialize() const
{
	if (Action) return FInputAssetLibrary::SerializeInputAction(*Action);
	if (Context) return FInputAssetLibrary::SerializeMappingContext(*Context);
	return {};
}

void FInputAssetEditor::Tick(float DeltaTime)
{
	// 미리보기 장면이 없으므로 FAssetEditor::Tick(PreviewScene.Tick)은 부르지 않는다
	if (!bCheckDirty) return;
	bCheckDirty = false;
	bDirty = Serialize() != SavedText;
}

bool FInputAssetEditor::Save()
{
	const bool bSaved = Action
		? FInputAssetLibrary::SaveInputAction(*Action, GetAssetPath())
		: (Context && FInputAssetLibrary::SaveMappingContext(*Context, GetAssetPath()));

	if (!bSaved)
	{
		const std::wstring Message = FPaths::FromUtf8("Failed to save " + GetAssetPath() + ". See Output Log.").wstring();
		MessageBoxW(GetActiveWindow(), Message.c_str(), L"Save Input Asset", MB_OK | MB_ICONERROR);
		return false;
	}

	SavedText = Serialize();
	bDirty = false;
	HTR_LOG(Info, "Input asset saved: {}", GetAssetPath());
	return true;
}

void FInputAssetEditor::Revert()
{
	FInputAssetLibrary::ReloadFromDisk(GetAssetPath());
	SavedText = Serialize();
	bDirty = false;
}

bool FInputAssetEditor::ConfirmClose()
{
	if (!bDirty) return true;

	const FString Name = std::filesystem::path(GetAssetPath()).filename().string();
	const std::wstring Message = FPaths::FromUtf8("Save changes to " + Name + " before closing?").wstring();
	const int Result = MessageBoxW(GetActiveWindow(), Message.c_str(), L"Input Asset", MB_YESNOCANCEL | MB_ICONQUESTION);
	if (Result == IDCANCEL) return false;
	if (Result == IDYES) return Save();
	return true;	// 저장하지 않고 닫는다. ShutdownResources가 되돌린다
}

const std::vector<FString>& FInputAssetEditor::GetActionAssetPaths(bool bRefresh)
{
	if (!bRefresh && !ActionAssetPaths.empty()) return ActionAssetPaths;

	ActionAssetPaths.clear();
	std::error_code Error;
	for (auto It = std::filesystem::recursive_directory_iterator("Assets", Error);
		!Error && It != std::filesystem::recursive_directory_iterator(); It.increment(Error))
	{
		if (!It->is_regular_file(Error)) continue;
		const FString Path = It->path().generic_string();
		if (FInputAssetLibrary::IsInputActionPath(Path)) ActionAssetPaths.push_back(Path);
	}
	std::sort(ActionAssetPaths.begin(), ActionAssetPaths.end());
	return ActionAssetPaths;
}
