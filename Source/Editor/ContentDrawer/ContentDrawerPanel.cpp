#include "EnginePCH.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"

#include "Render/SkeletalMesh.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpaceFile.h"
#include "Input/InputAssetLibrary.h"
#include <cstring>
#include "Animation/AnimBlueprintFile.h"

#include "Particles/ParticleSystemFile.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicalMaterialFile.h"
#include "Render/Material.h"
#include "Render/MaterialFile.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/UObjectHash.h"

#include <imgui_internal.h>
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace
{
	// 에셋 종류. 카드 아이콘, 아래 색 줄, 종류 글자, 필터 메뉴에 쓴다.
	enum class EAssetCategory : uint8
	{
		Folder,
		Texture,
		StaticMesh,
		SkeletalMesh,
		Animation,
		BlendSpace,
		AnimBlueprint,
		Particle,
		PhysicsAsset,
		Level,
		Widget,
		Font,
		Sound,
		Script,
		Blueprint,
		InputAction,
		InputMappingContext,
		Other,

		Count
	};

	struct FCategoryInfo
	{
		const char* Label;
		EEditorIcon Icon;
		ImU32 Color;
	};

	// EAssetCategory 순서. 색은 UE5 에셋 종류 색(카드 아래 줄)과 비슷하게 골랐다.
	const FCategoryInfo CategoryInfos[] = {
		{ "Folder",             EEditorIcon::Folder,       IM_COL32(201, 169, 110, 255) },
		{ "Texture",            EEditorIcon::Texture,      IM_COL32(214, 82, 82, 255) },
		{ "Static Mesh",        EEditorIcon::Local,        IM_COL32(63, 196, 214, 255) },
		{ "Skeletal Mesh",      EEditorIcon::SkeletalMesh, IM_COL32(236, 92, 178, 255) },
		{ "Animation Sequence", EEditorIcon::Animation,    IM_COL32(86, 170, 232, 255) },
		{ "Blend Space",        EEditorIcon::Animation,    IM_COL32(232, 176, 64, 255) },
		{ "Anim Blueprint",     EEditorIcon::Animation,    IM_COL32(230, 120, 50, 255) },
		{ "Particle System",    EEditorIcon::Particle,     IM_COL32(245, 245, 245, 255) },
		{ "Physics Asset",      EEditorIcon::SkeletalMesh, IM_COL32(255, 192, 128, 255) },
		{ "Level",              EEditorIcon::Level,        IM_COL32(255, 160, 40, 255) },
		{ "Widget Blueprint",   EEditorIcon::Widget,       IM_COL32(80, 160, 255, 255) },
		{ "Font",               EEditorIcon::Font,         IM_COL32(190, 190, 190, 255) },
		{ "Sound",              EEditorIcon::Sound,        IM_COL32(130, 130, 255, 255) },
		{ "Script",             EEditorIcon::Script,       IM_COL32(242, 210, 75, 255) },
		{ "Blueprint Class",    EEditorIcon::Script,       IM_COL32(60, 140, 230, 255) },
		{ "Input Action",       EEditorIcon::Settings,     IM_COL32(220, 90, 90, 255) },
		{ "Input Mapping Context", EEditorIcon::Settings,  IM_COL32(120, 200, 90, 255) },
		{ "File",               EEditorIcon::File,         IM_COL32(150, 150, 150, 255) },
	};
	static_assert(IM_ARRAYSIZE(CategoryInfos) == static_cast<int>(EAssetCategory::Count));

	const FCategoryInfo& GetCategoryInfo(EAssetCategory Category)
	{
		return CategoryInfos[static_cast<int32>(Category)];
	}

	FString ToLower(FString Text)
	{
		for (char& C : Text)
			C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
		return Text;
	}

	EAssetCategory GetAssetCategory(const fs::path& Path, const FString& PathString, bool bDirectory)
	{
		if (bDirectory)
			return EAssetCategory::Folder;
		// 같은 확장자를 쓰는 에셋이 있어 전용 판별 함수를 먼저 본다.
		if (AnimSequenceFile::IsAnimSequencePath(PathString)) return EAssetCategory::Animation;
		if (BlendSpaceFile::IsBlendSpacePath(PathString)) return EAssetCategory::BlendSpace;
		if (AnimBlueprintFile::IsAnimBlueprintPath(PathString)) return EAssetCategory::AnimBlueprint;
		if (ParticleSystemFile::IsParticleSystemPath(PathString)) return EAssetCategory::Particle;
		if (FBlueprint::IsBlueprintPath(PathString)) return EAssetCategory::Blueprint;
		if (PhysicsAssetFile::IsPhysicsAssetPath(PathString)) return EAssetCategory::PhysicsAsset;
		if (FInputAssetLibrary::IsInputActionPath(PathString)) return EAssetCategory::InputAction;
		if (FInputAssetLibrary::IsMappingContextPath(PathString)) return EAssetCategory::InputMappingContext;
		if (FFbxMeshBake::IsBakedPath(PathString))
			return PathString.ends_with("_Static.fbx.bin") ? EAssetCategory::StaticMesh : EAssetCategory::SkeletalMesh;

		const FString Ext = ToLower(Path.extension().string());
		if (Ext == ".png" || Ext == ".dds" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".tga") return EAssetCategory::Texture;
		if (Ext == ".obj") return EAssetCategory::StaticMesh;
		if (Ext == ".scene") return EAssetCategory::Level;
		if (Ext == ".widget") return EAssetCategory::Widget;
		if (Ext == ".json") return EAssetCategory::Font;   // 에셋 매니저가 .json을 폰트로 읽는다
		if (Ext == ".wav" || Ext == ".mp3" || Ext == ".ogg" || Ext == ".flac") return EAssetCategory::Sound;
		if (Ext == ".lua") return EAssetCategory::Script;
		return EAssetCategory::Other;
	}

	struct FEntry
	{
		fs::path Path;
		FString PathString;
		FString Name;
		bool bDirectory = false;
		EAssetCategory Category = EAssetCategory::Other;
	};

	bool LessIgnoreCase(const FString& A, const FString& B)
	{
		return ToLower(A) < ToLower(B);
	}

	// Text를 Width 안에서 줄바꿈해 최대 MaxLines 줄만 그린다. bCenter면 한 줄일 때 가운데 맞춘다.
	void DrawClippedText(ImDrawList* DrawList, const ImVec2& Pos, float Width, int32 MaxLines, float FontSize,
		ImU32 Color, const char* Text, bool bCenter)
	{
		ImFont* Font = ImGui::GetFont();
		const ImVec2 OneLine = Font->CalcTextSizeA(FontSize, FLT_MAX, 0.0f, Text);
		ImVec2 TextPos = Pos;
		if (bCenter && OneLine.x <= Width)
			TextPos.x += std::floor((Width - OneLine.x) * 0.5f);
		const ImVec4 Clip(Pos.x, Pos.y, Pos.x + Width, Pos.y + FontSize * MaxLines + 1.0f);
		DrawList->AddText(Font, FontSize, TextPos, Color, Text, nullptr, MaxLines > 1 ? Width : 0.0f, &Clip);
	}
}

bool FContentDrawerPanel::Init()
{
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
}

void FContentDrawerPanel::ToggleDrawer()
{
	if (bDocked)
	{
		// 레이아웃에 붙어 있으면 그 창으로 포커스만 옮긴다.
		ImGui::SetWindowFocus("Content Drawer");
		return;
	}
	bDrawerOpen = !bDrawerOpen;
	bJustOpened = bDrawerOpen;
}

void FContentDrawerPanel::OpenDrawer()
{
	SetOpen(true);
	if (bDocked)
	{
		ImGui::SetWindowFocus("Content Drawer");
		return;
	}
	if (!bDrawerOpen)
	{
		bDrawerOpen = true;
		bJustOpened = true;
	}
}

void FContentDrawerPanel::OpenCreateBlueprintDialog()
{
	OpenDrawer();
	MakeUniqueBlueprintName();
	bOpenCreateBlueprint = true;
}

bool FContentDrawerPanel::BrowseToFile(const fs::path& FilePath)
{
	std::error_code Error;
	if (FilePath.empty() || !fs::is_regular_file(FilePath, Error))
		return false;

	// 드로어는 작업 폴더 기준 상대 경로("Content/Scenes/A.scene")로 다루므로 프로젝트 안이면 상대 경로로 바꾼다.
	fs::path Target = FilePath;
	const fs::path Relative = fs::relative(FilePath, fs::current_path(Error), Error);
	if (!Error && !Relative.empty() && *Relative.begin() != "..")
		Target = Relative;

	NavigateTo(Target.parent_path());
	SelectOnly(Target.generic_string());
	bScrollToHighlight = true;

	SetOpen(true);
	if (bDocked)
	{
		ImGui::SetWindowFocus("Content Drawer");
	}
	else
	{
		bDrawerOpen = true;
		bJustOpened = true;
	}
	return true;
}

void FContentDrawerPanel::SetDrawerRect(const ImVec2& Pos, const ImVec2& Size, ImGuiID ViewportId)
{
	DrawerPos = Pos;
	DrawerSize = Size;
	DrawerViewportId = ViewportId;
}

void FContentDrawerPanel::NavigateTo(const fs::path& Path)
{
	if (Path.empty() || Path == CurrentPath)
		return;
	BackHistory.push_back(CurrentPath);
	ForwardHistory.clear();
	CurrentPath = Path;
	ClearSelection();
	SearchBuffer[0] = '\0';
	bRevealCurrentFolder = true;
}

void FContentDrawerPanel::NavigateBack()
{
	if (BackHistory.empty())
		return;
	ForwardHistory.push_back(CurrentPath);
	CurrentPath = BackHistory.back();
	ClearSelection();
	BackHistory.pop_back();
	bRevealCurrentFolder = true;
}

void FContentDrawerPanel::NavigateForward()
{
	if (ForwardHistory.empty())
		return;
	BackHistory.push_back(CurrentPath);
	CurrentPath = ForwardHistory.back();
	ClearSelection();
	ForwardHistory.pop_back();
	bRevealCurrentFolder = true;
}

bool FContentDrawerPanel::IsFavorite(const fs::path& Path) const
{
	return std::find(FavoritePaths.begin(), FavoritePaths.end(), Path) != FavoritePaths.end();
}

void FContentDrawerPanel::ToggleFavorite(const fs::path& Path)
{
	const auto It = std::find(FavoritePaths.begin(), FavoritePaths.end(), Path);
	if (It != FavoritePaths.end())
		FavoritePaths.erase(It);
	else
		FavoritePaths.push_back(Path);
}

// ---------------- 선택 ----------------

bool FContentDrawerPanel::IsSelected(const FString& Path) const
{
	return std::find(SelectedPaths.begin(), SelectedPaths.end(), Path) != SelectedPaths.end();
}

void FContentDrawerPanel::SelectOnly(const FString& Path)
{
	SelectedPaths.clear();
	SelectedPaths.push_back(Path);
	HighlightPath = Path;
}

void FContentDrawerPanel::ToggleSelected(const FString& Path)
{
	const auto It = std::find(SelectedPaths.begin(), SelectedPaths.end(), Path);
	if (It != SelectedPaths.end())
		SelectedPaths.erase(It);
	else
		SelectedPaths.push_back(Path);
	HighlightPath = Path;
}

void FContentDrawerPanel::ClearSelection()
{
	SelectedPaths.clear();
	HighlightPath.clear();
	PendingSelectOnly.clear();
}

std::vector<fs::path> FContentDrawerPanel::GetSelectedFsPaths() const
{
	std::vector<fs::path> Paths;
	Paths.reserve(SelectedPaths.size());
	for (const FString& Path : SelectedPaths)
		Paths.emplace_back(Path);
	return Paths;
}

// ---------------- 이름 변경 / 삭제 / 옮기기 ----------------

namespace
{
	// 파일 이름에서 처음 '.'부터를 확장자로 본다 ("Mesh_Static.fbx.bin" -> ".fbx.bin"). 폴더는 확장자 없음.
	FString SplitExtension(const FString& FileName, bool bDirectory, FString& OutStem)
	{
		const size_t Dot = bDirectory ? FString::npos : FileName.find('.', 1);
		OutStem = Dot == FString::npos ? FileName : FileName.substr(0, Dot);
		return Dot == FString::npos ? FString() : FileName.substr(Dot);
	}

	bool IsValidFileName(const char* Name)
	{
		if (!Name || Name[0] == '\0')
			return false;
		for (const char* C = Name; *C; ++C)
		{
			if (std::strchr("\\/:*?\"<>|", *C) || static_cast<unsigned char>(*C) < 32)
				return false;
		}
		const size_t Len = std::strlen(Name);
		return Name[Len - 1] != '.' && Name[Len - 1] != ' ';
	}

	// Child가 Parent와 같거나 그 아래 경로인지 (둘 다 같은 형식의 경로).
	bool IsSameOrUnder(const fs::path& Child, const fs::path& Parent)
	{
		const FString C = Child.lexically_normal().generic_string();
		const FString P = Parent.lexically_normal().generic_string();
		return C == P || C.starts_with(P.ends_with("/") ? P : P + "/");
	}

	// 휴지통으로 보낸다 (탐색기에서 되살릴 수 있다).
	bool MoveToRecycleBin(const fs::path& Path)
	{
		std::error_code Error;
		std::wstring From = fs::absolute(Path, Error).make_preferred().wstring();
		if (Error)
			return false;
		From.push_back(L'\0');   // SHFileOperation은 이중 널로 끝나는 목록을 받는다
		SHFILEOPSTRUCTW Op = {};
		Op.wFunc = FO_DELETE;
		Op.pFrom = From.c_str();
		Op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
		return SHFileOperationW(&Op) == 0 && !Op.fAnyOperationsAborted;
	}
}

void FContentDrawerPanel::BeginRename(const fs::path& Path)
{
	std::error_code Error;
	if (Path.empty() || Path == AssetRootPath || !fs::exists(Path, Error))
		return;
	FString Stem;
	RenameExtension = SplitExtension(Path.filename().string(), fs::is_directory(Path, Error), Stem);
	strncpy_s(RenameBuffer, Stem.c_str(), _TRUNCATE);
	RenameTarget = Path;
	bOpenRename = true;
}

void FContentDrawerPanel::BeginDelete(const std::vector<fs::path>& Paths)
{
	DeleteTargets.clear();
	for (const fs::path& Path : Paths)
	{
		if (!Path.empty() && Path != AssetRootPath)
			DeleteTargets.push_back(Path);
	}
	if (!DeleteTargets.empty())
		bOpenDelete = true;
}

void FContentDrawerPanel::OnPathMoved(const fs::path& OldPath, const fs::path& NewPath)
{
	auto Remap = [&OldPath, &NewPath](fs::path& Path) -> bool
	{
		if (!IsSameOrUnder(Path, OldPath))
			return false;
		if (NewPath.empty())
			return true;   // 지워졌다
		const fs::path Rest = Path.lexically_normal().lexically_relative(OldPath.lexically_normal());
		Path = (Rest.empty() || Rest == ".") ? NewPath : NewPath / Rest;
		return false;
	};

	if (Remap(CurrentPath))
		CurrentPath = OldPath.parent_path();
	for (auto It = FavoritePaths.begin(); It != FavoritePaths.end();)
		It = Remap(*It) ? FavoritePaths.erase(It) : It + 1;
	auto RemapHistory = [&](std::vector<fs::path>& History)
	{
		for (auto It = History.begin(); It != History.end();)
			It = Remap(*It) ? History.erase(It) : It + 1;
	};
	RemapHistory(BackHistory);
	RemapHistory(ForwardHistory);

	for (auto It = SelectedPaths.begin(); It != SelectedPaths.end();)
	{
		fs::path P(*It);
		if (Remap(P))
		{
			It = SelectedPaths.erase(It);
			continue;
		}
		*It = P.generic_string();
		++It;
	}
	if (!HighlightPath.empty())
	{
		fs::path P(HighlightPath);
		HighlightPath = Remap(P) ? FString() : P.generic_string();
	}

	UAssetManager::RefreshAssetPaths(OldPath, NewPath);

	if (!NewPath.empty())
	{
		std::error_code Error;
		PendingReferenceFixups.push_back({ OldPath, NewPath, fs::is_directory(NewPath, Error) });
	}
}

namespace
{
	// 경로 문자열을 담을 수 있는 텍스트 에셋/설정 파일만 고친다 (바이너리·메시·소리는 건드리지 않는다).
	bool IsReferenceFile(const fs::path& Path)
	{
		static const char* const Extensions[] = {
			".scene", ".widget", ".bp", ".mat", ".pmat", ".particle", ".inputaction", ".inputcontext",
			".blendspace", ".animblueprint", ".physicsasset", ".lua", ".ini",
		};
		const FString Ext = ToLower(Path.extension().string());
		for (const char* E : Extensions)
			if (Ext == E) return true;
		return false;
	}

	bool IsQuoteOrAssign(char C) { return C == '"' || C == '\'' || C == '='; }

	// Text 안에서 따옴표(또는 ini의 '=') 바로 뒤에 오는 From을 To로 바꾼다.
	// 파일이면 From 뒤가 따옴표/줄 끝이어야 하고, 폴더면 '/'(그 아래 경로)도 허용한다. 바꾼 개수를 돌려준다.
	int32 ReplacePathToken(std::string& Text, const FString& From, const FString& To, bool bDirectory)
	{
		if (From.empty() || From == To)
			return 0;
		int32 Count = 0;
		size_t Pos = 0;
		while ((Pos = Text.find(From, Pos)) != std::string::npos)
		{
			const size_t End = Pos + From.size();
			const bool bPrevOk = Pos > 0 && IsQuoteOrAssign(Text[Pos - 1]);
			const char Next = End < Text.size() ? Text[End] : '\n';
			const bool bNextOk = Next == '"' || Next == '\'' || Next == '\r' || Next == '\n' || (bDirectory && Next == '/');
			if (bPrevOk && bNextOk)
			{
				Text.replace(Pos, From.size(), To);
				Pos += To.size();
				++Count;
			}
			else
			{
				Pos = End;
			}
		}
		return Count;
	}
}

void FContentDrawerPanel::ProcessReferenceFixups()
{
	if (PendingReferenceFixups.empty())
		return;
	const std::vector<FMovedPath> Moves = std::move(PendingReferenceFixups);
	PendingReferenceFixups.clear();

	// 씬에는 "Content/Models/a.png"와 Content 기준 "Models/a.png" 두 표기가 섞여 있어 둘 다 바꾼다.
	struct FToken { FString From; FString To; bool bDirectory; };
	std::vector<FToken> Tokens;
	const FString ContentPrefix = FPaths::ProjectContentPath() + "/";
	for (const FMovedPath& Move : Moves)
	{
		FString OldFull = Move.Old.lexically_normal().generic_string();
		FString NewFull = Move.New.lexically_normal().generic_string();
		Tokens.push_back({ OldFull, NewFull, Move.bDirectory });
		if (OldFull.starts_with(ContentPrefix) && NewFull.starts_with(ContentPrefix))
			Tokens.push_back({ OldFull.substr(ContentPrefix.size()), NewFull.substr(ContentPrefix.size()), Move.bDirectory });
	}

	std::vector<fs::path> Files;
	auto Collect = [&Files](const fs::path& Root)
	{
		std::error_code Error;
		for (fs::recursive_directory_iterator It(Root, Error), End; !Error && It != End; It.increment(Error))
		{
			std::error_code TypeError;
			if (It->is_regular_file(TypeError) && IsReferenceFile(It->path()))
				Files.push_back(It->path());
		}
	};
	Collect(AssetRootPath);
	Collect("Config");   // 프로젝트 설정 (GameDefaultMap=Content/Scenes/... 등)

	int32 ChangedFiles = 0;
	int32 ChangedRefs = 0;
	for (const fs::path& File : Files)
	{
		std::string Text;
		{
			std::ifstream In(File, std::ios::binary);
			if (!In) continue;
			Text.assign(std::istreambuf_iterator<char>(In), std::istreambuf_iterator<char>());
		}
		int32 Count = 0;
		for (const FToken& Token : Tokens)
			Count += ReplacePathToken(Text, Token.From, Token.To, Token.bDirectory);
		if (Count == 0)
			continue;
		std::ofstream Out(File, std::ios::binary | std::ios::trunc);
		if (!Out)
		{
			HTR_LOG(Warning, "Failed to update references in '{}'", File.generic_string());
			continue;
		}
		Out.write(Text.data(), static_cast<std::streamsize>(Text.size()));
		++ChangedFiles;
		ChangedRefs += Count;
	}
	if (ChangedFiles > 0)
		HTR_LOG(Info, "Updated {} reference(s) in {} file(s) after move/rename", ChangedRefs, ChangedFiles);
}

bool FContentDrawerPanel::CanMoveInto(const fs::path& Source, const fs::path& TargetFolder) const
{
	std::error_code Error;
	if (Source.empty() || Source == AssetRootPath || !fs::exists(Source, Error))
		return false;
	if (Source.parent_path().lexically_normal() == TargetFolder.lexically_normal())
		return false;   // 이미 그 폴더에 있다
	if (fs::is_directory(Source, Error) && IsSameOrUnder(TargetFolder, Source))
		return false;   // 폴더를 자기 안으로는 못 옮긴다
	return true;
}

void FContentDrawerPanel::AcceptMoveDrop(const fs::path& TargetFolder)
{
	if (DragPaths.empty() || !ImGui::BeginDragDropTarget())
		return;
	bool bAny = false;
	for (const fs::path& Path : DragPaths)
		bAny |= CanMoveInto(Path, TargetFolder);
	DragHoverTargetName = TargetFolder.filename().string();
	bDragHoverValid = bAny;
	if (bAny)
	{
		// 끌기 원본에 따라 페이로드 종류가 다르다 (텍스처/폰트/Blueprint는 다른 패널에도 놓을 수 있어 그 종류를 유지한다).
		const char* Types[] = { EditorDragDrop::ContentMove, EditorDragDrop::Texture, EditorDragDrop::Font, EditorDragDrop::Blueprint };
		for (const char* Type : Types)
		{
			if (ImGui::AcceptDragDropPayload(Type))
			{
				PendingMovePaths = DragPaths;
				PendingMoveTarget = TargetFolder;
				break;
			}
		}
	}
	ImGui::EndDragDropTarget();
}

void FContentDrawerPanel::ProcessPendingMove()
{
	if (PendingMovePaths.empty())
		return;
	const std::vector<fs::path> Paths = std::move(PendingMovePaths);
	const fs::path Target = std::move(PendingMoveTarget);
	PendingMovePaths.clear();
	PendingMoveTarget.clear();
	DragPaths.clear();

	std::vector<FString> Moved;
	for (const fs::path& Source : Paths)
	{
		if (!CanMoveInto(Source, Target))
			continue;
		const fs::path Destination = Target / Source.filename();
		std::error_code Error;
		if (fs::exists(Destination, Error))
		{
			HTR_LOG(Warning, "Move skipped, '{}' already exists in '{}'", Source.filename().string(), Target.generic_string());
			continue;
		}
		fs::rename(Source, Destination, Error);
		if (Error)
		{
			HTR_LOG(Warning, "Failed to move '{}': {}", Source.generic_string(), Error.message());
			continue;
		}
		OnPathMoved(Source, Destination);
		Moved.push_back(Destination.generic_string());
	}
	if (!Moved.empty())
		HTR_LOG(Info, "Moved {} item(s) to {}", Moved.size(), Target.generic_string());
}

void FContentDrawerPanel::DrawItemContextMenu(const fs::path& Path, bool bDirectory, bool bSkeletalMesh, const FString& PathString)
{
	const int32 Count = static_cast<int32>(SelectedPaths.size());
	const bool bMulti = Count > 1 && IsSelected(PathString);

	if (bDirectory && !bMulti)
	{
		if (ImGui::MenuItem(IsFavorite(Path) ? "Remove from Favorites" : "Add to Favorites"))
			ToggleFavorite(Path);
		ImGui::Separator();
	}
	if (bSkeletalMesh && !bMulti)
	{
		BlendSpaceCreator.DrawMenuItems(PathString, CurrentPath);
		AnimBlueprintCreator.DrawMenuItems(PathString, CurrentPath);
		ImGui::Separator();
	}

	if (ImGui::MenuItem("Rename", "F2", false, !bMulti))
		BeginRename(Path);
	char DeleteLabel[48];
	if (bMulti)
		std::snprintf(DeleteLabel, sizeof(DeleteLabel), "Delete %d Items", Count);
	else
		std::snprintf(DeleteLabel, sizeof(DeleteLabel), "Delete");
	if (ImGui::MenuItem(DeleteLabel, "Del"))
		BeginDelete(bMulti ? GetSelectedFsPaths() : std::vector<fs::path>{ Path });
	ImGui::Separator();
	if (ImGui::MenuItem("Show in Explorer"))
	{
		std::error_code Error;
		const std::wstring Abs = fs::absolute(Path, Error).make_preferred().wstring();
		const std::wstring Args = L"/select,\"" + Abs + L"\"";
		ShellExecuteW(nullptr, L"open", L"explorer.exe", Args.c_str(), nullptr, SW_SHOWNORMAL);
	}
}

void FContentDrawerPanel::DrawRenameDeletePopups()
{
	if (bOpenRename) { ImGui::OpenPopup("Rename"); bOpenRename = false; }
	if (bOpenDelete) { ImGui::OpenPopup("Delete Assets"); bOpenDelete = false; }

	if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(260.0f * EditorIcons::GetScale());
		const bool bEnter = ImGui::InputText("##RenameName", RenameBuffer, sizeof(RenameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (!RenameExtension.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", RenameExtension.c_str());
		}

		const FString NewName = FString(RenameBuffer) + RenameExtension;
		const fs::path Destination = RenameTarget.parent_path() / NewName;
		const bool bUnchanged = Destination == RenameTarget;
		std::error_code Error;
		// 대소문자만 바꾸는 경우는 같은 파일이 있다고 나오므로 막지 않는다
		const bool bCaseOnly = !bUnchanged && ToLower(Destination.generic_string()) == ToLower(RenameTarget.generic_string());
		const bool bExists = !bUnchanged && !bCaseOnly && fs::exists(Destination, Error);
		const bool bNameOk = IsValidFileName(RenameBuffer);
		if (!bNameOk) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid name.");
		else if (bExists) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "An item with this name already exists.");

		const bool bValid = bNameOk && !bExists;
		ImGui::BeginDisabled(!bValid);
		if (ImGui::Button("Rename") || (bEnter && bValid))
		{
			if (!bUnchanged)
			{
				fs::rename(RenameTarget, Destination, Error);
				if (Error)
					HTR_LOG(Warning, "Failed to rename '{}': {}", RenameTarget.generic_string(), Error.message());
				else
				{
					OnPathMoved(RenameTarget, Destination);
					if (Destination.parent_path() == CurrentPath)
						SelectOnly(Destination.generic_string());
				}
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("Delete Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (DeleteTargets.size() == 1)
			ImGui::Text("Delete '%s'?", DeleteTargets[0].filename().string().c_str());
		else
			ImGui::Text("Delete %d items?", static_cast<int32>(DeleteTargets.size()));
		int32 Shown = 0;
		if (DeleteTargets.size() > 1)
		{
			for (const fs::path& Path : DeleteTargets)
			{
				if (++Shown > 8) { ImGui::TextDisabled("  ... and %d more", static_cast<int32>(DeleteTargets.size()) - 8); break; }
				ImGui::TextDisabled("  %s", Path.filename().string().c_str());
			}
		}
		bool bHasFolder = false;
		for (const fs::path& Path : DeleteTargets)
		{
			std::error_code Error;
			bHasFolder |= fs::is_directory(Path, Error);
		}
		if (bHasFolder)
			ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "Folders are deleted with everything inside them.");
		ImGui::TextDisabled("Items are moved to the Recycle Bin.");

		if (ImGui::Button("Delete") || ImGui::IsKeyPressed(ImGuiKey_Enter))
		{
			const std::vector<fs::path> Targets = std::move(DeleteTargets);
			DeleteTargets.clear();
			for (const fs::path& Path : Targets)
			{
				if (MoveToRecycleBin(Path))
					OnPathMoved(Path, {});
				else
					HTR_LOG(Warning, "Failed to delete '{}'", Path.generic_string());
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			DeleteTargets.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FContentDrawerPanel::OnRender()
{
	if (!BeginWindow())
		return;

	RenderContents();
	ImGui::End();
}

// 드로어 모드면 상태 표시줄 바로 위에 덮어 그리고, 포커스를 잃거나 Esc를 누르면 닫는다.
bool FContentDrawerPanel::BeginWindow()
{
	if (bDocked)
	{
		if (bPendingDock)
		{
			// 처음 레이아웃에 붙일 때는 Output Log가 있는 아래쪽 노드로 보낸다.
			if (ImGuiWindow* LogWindow = ImGui::FindWindowByName("Output Log"); LogWindow && LogWindow->DockId != 0)
				ImGui::SetNextWindowDockID(LogWindow->DockId, ImGuiCond_Always);
			bPendingDock = false;
		}
		// 도킹된 탭의 X를 누르면 패널을 없애지 않고 닫힌 드로어로 돌아간다 (상태 표시줄 버튼으로 다시 연다).
		bool bStayDocked = true;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		EditorPanelUI::BeginPanel("Content Drawer", &bStayDocked);
		ImGui::PopStyleVar();
		if (!bStayDocked)
		{
			bDocked = false;
			bDrawerOpen = false;
		}
		return true;
	}

	if (!bDrawerOpen || DrawerSize.x <= 0.0f || DrawerSize.y <= 0.0f)
		return false;

	ImGui::SetNextWindowPos(DrawerPos);
	ImGui::SetNextWindowSize(DrawerSize);
	if (DrawerViewportId != 0)
		ImGui::SetNextWindowViewport(DrawerViewportId);
	if (bJustOpened)
		ImGui::SetNextWindowFocus();

	const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Content Drawer###ContentDrawerOverlay", nullptr, Flags);
	ImGui::PopStyleVar(2);

	if (!bJustOpened)
	{
		const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const bool bPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
		const bool bDragging = ImGui::GetDragDropPayload() != nullptr;
		// 이번 프레임은 그대로 그리고 다음 프레임부터 닫힌다.
		if ((!bFocused && !bPopupOpen && !bDragging) || (bFocused && ImGui::IsKeyPressed(ImGuiKey_Escape)))
			bDrawerOpen = false;
	}
	bJustOpened = false;
	return true;
}

// 왼쪽 사이드바 | 끌 수 있는 경계 | 오른쪽(툴바, 검색 줄, 그리드, 항목 수)
void FContentDrawerPanel::RenderContents()
{
	std::error_code Error;
	if (!fs::is_directory(CurrentPath, Error))
		CurrentPath = AssetRootPath;

	Error.clear();
	if (!fs::is_directory(AssetRootPath, Error))
	{
		ImGui::TextUnformatted("Asset folder is unavailable.");
		return;
	}

	const float S = EditorIcons::GetScale();

	DrawSidebar();

	// 사이드바 너비 조절 경계
	ImGui::SameLine(0.0f, 0.0f);
	const ImVec2 SplitterMin = ImGui::GetCursorScreenPos();
	const float SplitterWidth = 4.0f * S;
	ImGui::InvisibleButton("##SidebarSplitter", ImVec2(SplitterWidth, ImMax(1.0f, ImGui::GetContentRegionAvail().y)));
	const bool bSplitterActive = ImGui::IsItemActive();
	if (bSplitterActive)
		SidebarWidth = ImClamp(SidebarWidth + ImGui::GetIO().MouseDelta.x / S, 140.0f, 520.0f);
	if (bSplitterActive || ImGui::IsItemHovered())
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	const float LineX = std::floor(SplitterMin.x + SplitterWidth * 0.5f);
	ImGui::GetWindowDrawList()->AddLine(ImVec2(LineX, SplitterMin.y), ImVec2(LineX, ImGui::GetItemRectMax().y),
		ImGui::GetColorU32(bSplitterActive ? ImGuiCol_SeparatorActive : ImGuiCol_Separator), 1.0f);

	ImGui::SameLine(0.0f, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * S, 6.0f * S));
	ImGui::BeginChild("##ContentMain", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
	{
		DrawToolbar();
		DrawSearchRow();

		const float FooterHeight = ImGui::GetTextLineHeight() + 6.0f * S;
		ImGui::BeginChild("##AssetGrid", ImVec2(0.0f, -FooterHeight));
		DrawAssetGrid();
		BlendSpaceCreator.ProcessCreateRequest();
		AnimBlueprintCreator.ProcessCreateRequest();
		ImGui::EndChild();

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f * S);
		ImGui::TextDisabled("%d item%s (%d selected)", VisibleItemCount, VisibleItemCount == 1 ? "" : "s", SelectedVisibleCount);
	}
	ImGui::EndChild();

	DrawCreatePopups();
	DrawRenameDeletePopups();
	ProcessPendingMove();
	ProcessReferenceFixups();
	// 끌기 미리보기는 이 값을 다음 프레임에 읽는다 (원본 카드가 놓기 대상보다 먼저 그려지므로)
	PrevDragHoverTargetName = std::move(DragHoverTargetName);
	bPrevDragHoverValid = bDragHoverValid;
	DragHoverTargetName.clear();
	bDragHoverValid = false;
	// 끌기가 끝났는데 아무 데도 놓지 않았으면 옮길 목록을 비운다
	if (!DragPaths.empty() && ImGui::GetDragDropPayload() == nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		DragPaths.clear();
}

// ---------------- 사이드바 ----------------

// 접고 펴는 섹션 머리줄 (UE5 "Favorites", 프로젝트 이름 줄). 펼쳐져 있으면 true.
bool FContentDrawerPanel::DrawSidebarSection(const char* Label, bool& bOpen, bool* bSearchToggle)
{
	const float S = EditorIcons::GetScale();
	const float H = ImGui::GetFontSize() + 12.0f * S;
	const float Width = ImGui::GetContentRegionAvail().x;
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	ImGui::PushID(Label);
	if (ImGui::InvisibleButton("##Header", ImVec2(ImMax(1.0f, Width - (bSearchToggle ? H : 0.0f)), H)))
		bOpen = !bOpen;
	const bool bHovered = ImGui::IsItemHovered();
	DrawList->AddRectFilled(Min, ImVec2(Min.x + Width, Min.y + H),
		ImGui::GetColorU32(bHovered ? ImGuiCol_ButtonHovered : ImGuiCol_TitleBg));

	const float MidY = Min.y + H * 0.5f;
	EditorIcons::Draw(DrawList, bOpen ? EEditorIcon::ChevronDown : EEditorIcon::ChevronRight,
		ImVec2(Min.x + 12.0f * S, MidY), 8.0f * S, ImGui::GetColorU32(ImGuiCol_Text, 0.8f));
	// 굵은 글꼴이 없어 반 픽셀 옮겨 두 번 써서 굵게 보이게 한다.
	const ImVec2 TextPos(Min.x + 22.0f * S, MidY - ImGui::GetFontSize() * 0.5f);
	DrawList->AddText(TextPos, ImGui::GetColorU32(ImGuiCol_Text), Label);
	DrawList->AddText(ImVec2(TextPos.x + 0.6f * S, TextPos.y), ImGui::GetColorU32(ImGuiCol_Text), Label);

	if (bSearchToggle)
	{
		ImGui::SameLine(0.0f, 0.0f);
		if (EditorIcons::Button("##Search", EEditorIcon::Search, ImGui::GetColorU32(ImGuiCol_Text, 0.75f), nullptr, false, *bSearchToggle, H))
		{
			*bSearchToggle = !*bSearchToggle;
			if (*bSearchToggle)
				bOpen = true;
		}
		ImGui::SetItemTooltip("Search folders");
	}
	ImGui::PopID();
	return bOpen;
}

void FContentDrawerPanel::DrawSidebar()
{
	const float S = EditorIcons::GetScale();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f * S));
	ImGui::BeginChild("##ContentSidebar", ImVec2(SidebarWidth * S, 0.0f));

	const ImU32 FolderColor = GetCategoryInfo(EAssetCategory::Folder).Color;
	const float RowIndent = 14.0f * S;

	if (DrawSidebarSection("Favorites", bFavoritesOpen, nullptr))
	{
		ImGui::Dummy(ImVec2(0.0f, 2.0f * S));
		if (FavoritePaths.empty())
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + RowIndent);
			ImGui::TextDisabled("Right-click a folder to add it");
		}
		for (size_t I = 0; I < FavoritePaths.size(); ++I)
		{
			const fs::path Favorite = FavoritePaths[I];
			ImGui::PushID(static_cast<int>(I));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + RowIndent);
			const ImVec2 RowMin = ImGui::GetCursorScreenPos();
			if (ImGui::Selectable("##Favorite", CurrentPath == Favorite, ImGuiSelectableFlags_SpanAvailWidth,
				ImVec2(0.0f, ImGui::GetFontSize() + 4.0f * S)))
				NavigateTo(Favorite);
			const float MidY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			EditorIcons::Draw(DrawList, EEditorIcon::Folder, ImVec2(RowMin.x + 8.0f * S, MidY), 14.0f * S, FolderColor);
			DrawList->AddText(ImVec2(RowMin.x + 20.0f * S, MidY - ImGui::GetFontSize() * 0.5f),
				ImGui::GetColorU32(ImGuiCol_Text), Favorite.filename().string().c_str());
			if (ImGui::BeginPopupContextItem("##FavoriteContext"))
			{
				if (ImGui::MenuItem("Remove from Favorites"))
					ToggleFavorite(Favorite);
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		ImGui::Dummy(ImVec2(0.0f, 4.0f * S));
	}

	if (DrawSidebarSection("Content", bContentOpen, &bShowFolderSearch))
	{
		if (bShowFolderSearch)
		{
			ImGui::Dummy(ImVec2(0.0f, 2.0f * S));
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f * S);
			EditorIcons::SearchBox("##FolderSearch", FolderSearchBuffer, sizeof(FolderSearchBuffer), "Search Folders",
				ImGui::GetContentRegionAvail().x - 6.0f * S);
		}
		ImGui::Dummy(ImVec2(0.0f, 2.0f * S));

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 2.0f * S));
		if (FolderSearchBuffer[0] != '\0' && bShowFolderSearch)
		{
			// 검색 중에는 트리 대신 이름이 맞는 폴더를 평평하게 보여 준다.
			DrawFolderSearchResults(AssetRootPath);
		}
		else
		{
			DrawFolderTree(AssetRootPath);
			bRevealCurrentFolder = false;
		}
		ImGui::PopStyleVar();
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

// 폴더 하나를 트리 노드로 그린다 (폴더 아이콘 + 이름). Path 아래 폴더를 재귀로 그린다.
void FContentDrawerPanel::DrawFolderTree(const fs::path& Path)
{
	std::error_code Error;
	if (!fs::is_directory(Path, Error))
		return;

	const float S = EditorIcons::GetScale();
	const bool bRoot = Path == AssetRootPath;

	// 하위 폴더가 없으면 화살표 없는 잎 노드로 그린다.
	bool bHasChildren = false;
	for (fs::directory_iterator It(Path, Error), End; !Error && It != End; It.increment(Error))
	{
		std::error_code TypeError;
		if (fs::is_directory(It->path(), TypeError))
		{
			bHasChildren = true;
			break;
		}
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
		ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
	if (bRoot) Flags |= ImGuiTreeNodeFlags_DefaultOpen;
	if (!bHasChildren) Flags |= ImGuiTreeNodeFlags_Leaf;
	if (Path == CurrentPath) Flags |= ImGuiTreeNodeFlags_Selected;

	// BrowseToFile 등으로 들어온 폴더가 보이게 그 조상 노드를 연다.
	const FString PathString = Path.generic_string();
	const FString CurrentString = CurrentPath.generic_string();
	if (bRevealCurrentFolder && CurrentString.size() > PathString.size() && CurrentString.starts_with(PathString + "/"))
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * S, 2.0f * S));
	const bool bOpen = ImGui::TreeNodeEx(PathString.c_str(), Flags, "%s", "");
	ImGui::PopStyleVar();
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		NavigateTo(Path);

	// 아이콘과 이름은 직접 그린다 (트리 노드 글자 자리는 비워 둠).
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const float MidY = (Min.y + Max.y) * 0.5f;
	const float X = Min.x + ImGui::GetTreeNodeToLabelSpacing();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	EditorIcons::Draw(DrawList, EEditorIcon::Folder, ImVec2(X + 7.0f * S, MidY), 14.0f * S, GetCategoryInfo(EAssetCategory::Folder).Color);
	DrawList->AddText(ImVec2(X + 19.0f * S, MidY - ImGui::GetFontSize() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text),
		Path.filename().string().c_str());

	// 같은 단계의 폴더 노드끼리 팝업 ID가 겹치지 않게 경로로 구분한다 (안 그러면 형제 폴더 메뉴가 같이 열린다).
	ImGui::PushID(PathString.c_str());
	if (ImGui::BeginPopupContextItem("##FolderContext"))
	{
		if (ImGui::MenuItem(IsFavorite(Path) ? "Remove from Favorites" : "Add to Favorites"))
			ToggleFavorite(Path);
		if (!bRoot)
		{
			ImGui::Separator();
			if (ImGui::MenuItem("Rename"))
				BeginRename(Path);
			if (ImGui::MenuItem("Delete"))
				BeginDelete({ Path });
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
	// 그리드에서 끌어 온 항목을 이 폴더로 옮긴다
	AcceptMoveDrop(Path);

	if (bOpen)
	{
		for (fs::directory_iterator It(Path, Error), End; !Error && It != End; It.increment(Error))
		{
			std::error_code TypeError;
			if (fs::is_directory(It->path(), TypeError))
				DrawFolderTree(It->path());
		}
		ImGui::TreePop();
	}
}

void FContentDrawerPanel::DrawFolderSearchResults(const fs::path& Path)
{
	std::error_code Error;
	if (!fs::is_directory(Path, Error))
		return;

	const float S = EditorIcons::GetScale();
	if (EditorIcons::MatchesFilter(Path.filename().string().c_str(), FolderSearchBuffer))
	{
		const FString PathString = Path.generic_string();
		ImGui::PushID(PathString.c_str());
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f * S);
		const ImVec2 RowMin = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable("##Result", CurrentPath == Path, ImGuiSelectableFlags_SpanAvailWidth,
			ImVec2(0.0f, ImGui::GetFontSize() + 4.0f * S)))
			NavigateTo(Path);
		ImGui::SetItemTooltip("%s", PathString.c_str());
		const float MidY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		EditorIcons::Draw(DrawList, EEditorIcon::Folder, ImVec2(RowMin.x + 8.0f * S, MidY), 14.0f * S,
			GetCategoryInfo(EAssetCategory::Folder).Color);
		DrawList->AddText(ImVec2(RowMin.x + 20.0f * S, MidY - ImGui::GetFontSize() * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), Path.filename().string().c_str());
		ImGui::PopID();
	}
	for (fs::directory_iterator It(Path, Error), End; !Error && It != End; It.increment(Error))
	{
		std::error_code TypeError;
		if (fs::is_directory(It->path(), TypeError))
			DrawFolderSearchResults(It->path());
	}
}

// ---------------- 툴바와 검색 줄 ----------------

void FContentDrawerPanel::DrawToolbar()
{
	const float S = EditorIcons::GetScale();
	const float H = 26.0f * S;
	const ImU32 IconColor = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 6.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * S, 6.0f * S));

	// 배경이 깔린 버튼 (UE5 Add / Import / Save All)
	auto FramedButton = [&](const char* Id, EEditorIcon Icon, ImU32 Color, const char* Label, bool bDropdown)
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->ChannelsSplit(2);
		DrawList->ChannelsSetCurrent(1);
		const bool bPressed = EditorIcons::Button(Id, Icon, Color, Label, bDropdown, false, H);
		DrawList->ChannelsSetCurrent(0);
		DrawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Button), 3.0f * S);
		DrawList->ChannelsMerge();
		return bPressed;
	};
	auto OpenBelow = [S](const char* PopupId)
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f * S), ImGuiCond_Appearing);
		ImGui::OpenPopup(PopupId);
	};

	// Add
	if (FramedButton("##Add", EEditorIcon::Plus, EditorIcons::PlayGreen, "Add", false))
		OpenBelow("##AddMenu");
	ImGui::SetItemTooltip("Create a new asset or folder in this folder");
	if (ImGui::BeginPopup("##AddMenu"))
	{
		DrawCreateMenu();
		ImGui::EndPopup();
	}

	// Import
	ImGui::SameLine();
	if (FramedButton("##Import", EEditorIcon::Import, IconColor, "Import", false))
		OpenBelow("##ImportMenu");
	ImGui::SetItemTooltip("Import FBX");
	if (ImGui::BeginPopup("##ImportMenu"))
	{
		EditorIcons::MenuSection("Import FBX");
		if (EditorIcons::MenuRow("Static Mesh...", EEditorIcon::Local, false, OnImportFbx != nullptr) && OnImportFbx)
			OnImportFbx(EFbxImportMode::Static);
		if (EditorIcons::MenuRow("Skeletal Mesh...", EEditorIcon::SkeletalMesh, false, OnImportFbx != nullptr) && OnImportFbx)
			OnImportFbx(EFbxImportMode::Skeletal);
		ImGui::EndPopup();
	}

	// Save All
	ImGui::SameLine();
	ImGui::BeginDisabled(!OnSaveAll);
	if (FramedButton("##SaveAll", EEditorIcon::Save, IconColor, "Save All", false) && OnSaveAll)
		OnSaveAll();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Save the current level");

	// 뒤로 / 앞으로
	ImGui::SameLine(0.0f, 10.0f * S);
	ImGui::BeginDisabled(BackHistory.empty());
	if (EditorIcons::Button("##Back", EEditorIcon::ArrowLeft, IconColor, nullptr, false, false, H))
		NavigateBack();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Back");
	ImGui::SameLine(0.0f, 0.0f);
	ImGui::BeginDisabled(ForwardHistory.empty());
	if (EditorIcons::Button("##Forward", EEditorIcon::ArrowRight, IconColor, nullptr, false, false, H))
		NavigateForward();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Forward");

	// 오른쪽 버튼 자리를 먼저 계산한다.
	const char* DockLabel = bDocked ? "Undock to Drawer" : "Dock in Layout";
	const float DockWidth = EditorIcons::CalcButtonWidth(true, DockLabel, false, H);
	const float GearWidth = EditorIcons::CalcButtonWidth(true, nullptr, false, H);
	const float RightWidth = DockWidth + GearWidth + 4.0f * S;

	// 경로 (All > Content > Particle). 조각을 누르면 그 폴더로 간다.
	ImGui::SameLine(0.0f, 8.0f * S);
	const ImVec2 CrumbMin = ImGui::GetCursorScreenPos();
	const float CrumbWidth = ImMax(60.0f * S, ImGui::GetContentRegionAvail().x - RightWidth - 8.0f * S);
	const ImVec2 CrumbMax(CrumbMin.x + CrumbWidth, CrumbMin.y + H);
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(CrumbMin, CrumbMax, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f * S);
	DrawList->PushClipRect(CrumbMin, CrumbMax, true);
	ImGui::SetCursorScreenPos(ImVec2(CrumbMin.x + 4.0f * S, CrumbMin.y));
	{
		std::vector<fs::path> Crumbs;
		fs::path Accumulated;
		for (const fs::path& Part : CurrentPath)
		{
			Accumulated /= Part;
			Crumbs.push_back(Accumulated);
		}

		const ImU32 ChevronColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		auto Chevron = [&]()
		{
			ImGui::SameLine(0.0f, 0.0f);
			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(12.0f * S, H));
			EditorIcons::Draw(DrawList, EEditorIcon::ChevronRight, ImVec2(Pos.x + 6.0f * S, Pos.y + H * 0.5f), 8.0f * S, ChevronColor);
			ImGui::SameLine(0.0f, 0.0f);
		};

		if (EditorIcons::Button("##CrumbAll", EEditorIcon::None, 0, "All", false, false, H))
			NavigateTo(AssetRootPath);
		for (size_t I = 0; I < Crumbs.size(); ++I)
		{
			Chevron();
			ImGui::PushID(static_cast<int>(I));
			const FString Label = Crumbs[I].filename().string();
			if (EditorIcons::Button("##Crumb", EEditorIcon::None, 0, Label.c_str(), false, false, H))
				NavigateTo(Crumbs[I]);
			ImGui::PopID();
		}
	}
	DrawList->PopClipRect();

	// Dock in Layout / 보기 설정
	ImGui::SetCursorScreenPos(ImVec2(CrumbMax.x + 8.0f * S, CrumbMin.y));
	if (EditorIcons::Button("##Dock", EEditorIcon::Dock, IconColor, DockLabel, false, false, H))
	{
		bDocked = !bDocked;
		bPendingDock = bDocked;
		bDrawerOpen = !bDocked;
		bJustOpened = bDrawerOpen;
	}
	ImGui::SameLine();
	if (EditorIcons::Button("##ViewOptions", EEditorIcon::Settings, IconColor, nullptr, false,
		ImGui::IsPopupOpen("##ViewOptionsMenu"), H))
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 2.0f * S), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
		ImGui::OpenPopup("##ViewOptionsMenu");
	}
	ImGui::SetItemTooltip("View Options");
	if (ImGui::BeginPopup("##ViewOptionsMenu"))
	{
		EditorIcons::MenuSection("Thumbnails");
		ImGui::SetNextItemWidth(200.0f * S);
		ImGui::SliderFloat("##ThumbnailSize", &ThumbnailSize, 56.0f, 192.0f, "%.0f px");
		ImGui::Spacing();
		EditorIcons::MenuSection("Content");
		if (EditorIcons::MenuRow("Show Folders", EEditorIcon::Folder, bShowFolders, true, true))
			bShowFolders = !bShowFolders;
		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(2);
}

void FContentDrawerPanel::DrawSearchRow()
{
	const float S = EditorIcons::GetScale();
	const float H = 24.0f * S;
	const ImU32 IconColor = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * S, 6.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * S, 6.0f * S));

	// 필터 (종류별로 켜고 끈다)
	const bool bFiltered = TypeFilterMask != 0;
	if (EditorIcons::Button("##Filters", EEditorIcon::Filter, bFiltered ? ImGui::GetColorU32(ImGuiCol_SliderGrab) : IconColor,
		nullptr, true, ImGui::IsPopupOpen("##FilterMenu"), H))
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f * S), ImGuiCond_Appearing);
		ImGui::OpenPopup("##FilterMenu");
	}
	ImGui::SetItemTooltip("Filters");
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 1.0f * S));
	if (ImGui::BeginPopup("##FilterMenu"))
	{
		EditorIcons::MenuSection("Asset Types");
		for (int32 I = 1; I < static_cast<int32>(EAssetCategory::Count); ++I)
		{
			const FCategoryInfo& Info = CategoryInfos[I];
			const uint32 Bit = 1u << I;
			if (EditorIcons::MenuRow(Info.Label, Info.Icon, (TypeFilterMask & Bit) != 0, true, true))
				TypeFilterMask ^= Bit;
		}
		ImGui::Separator();
		if (EditorIcons::MenuRow("Reset Filters", EEditorIcon::Reset, false, bFiltered))
			TypeFilterMask = 0;
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();

	// 검색 (지금 폴더 안 이름)
	ImGui::SameLine();
	char Hint[160];
	std::snprintf(Hint, sizeof(Hint), "Search %s", CurrentPath.filename().string().c_str());
	const float SortWidth = EditorIcons::CalcButtonWidth(true, nullptr, true, H);
	const float SearchWidth = ImMin(ImGui::GetContentRegionAvail().x - SortWidth - 8.0f * S, 520.0f * S);
	EditorIcons::SearchBox("##AssetSearch", SearchBuffer, sizeof(SearchBuffer), Hint, ImMax(80.0f * S, SearchWidth));

	// 정렬
	ImGui::SameLine();
	if (EditorIcons::Button("##Sort", EEditorIcon::Sort, IconColor, nullptr, true, ImGui::IsPopupOpen("##SortMenu"), H))
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f * S), ImGuiCond_Appearing);
		ImGui::OpenPopup("##SortMenu");
	}
	ImGui::SetItemTooltip("Sort");
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * S, 1.0f * S));
	if (ImGui::BeginPopup("##SortMenu"))
	{
		EditorIcons::MenuSection("Sort By");
		if (EditorIcons::MenuRow("Name (A to Z)", EEditorIcon::None, SortMode == ESortMode::NameAscending, true, false, nullptr, true))
			SortMode = ESortMode::NameAscending;
		if (EditorIcons::MenuRow("Name (Z to A)", EEditorIcon::None, SortMode == ESortMode::NameDescending, true, false, nullptr, true))
			SortMode = ESortMode::NameDescending;
		if (EditorIcons::MenuRow("Asset Type", EEditorIcon::None, SortMode == ESortMode::Type, true, false, nullptr, true))
			SortMode = ESortMode::Type;
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();

	ImGui::PopStyleVar(2);
}

// ---------------- 에셋 그리드 ----------------

void FContentDrawerPanel::DrawAssetGrid()
{
	const float S = EditorIcons::GetScale();

	// 빈 곳 우클릭: 만들기 메뉴
	if (ImGui::BeginPopupContextWindow("##DrawerContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		DrawCreateMenu();
		ImGui::EndPopup();
	}

	// 마우스 뒤로/앞으로 버튼
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
	{
		if (ImGui::IsMouseClicked(3)) NavigateBack();
		if (ImGui::IsMouseClicked(4)) NavigateForward();
	}

	// 지금 폴더 항목을 모아 거르고 정렬한다.
	std::vector<FEntry> Entries;
	{
		std::error_code IterateError;
		for (fs::directory_iterator It(CurrentPath, IterateError), End; !IterateError && It != End; It.increment(IterateError))
		{
			std::error_code TypeError;
			FEntry Entry;
			Entry.Path = It->path();
			Entry.bDirectory = fs::is_directory(Entry.Path, TypeError);
			if (TypeError || (Entry.bDirectory && !bShowFolders))
				continue;
			Entry.PathString = Entry.Path.generic_string();
			Entry.Name = Entry.Path.filename().string();
			if (!EditorIcons::MatchesFilter(Entry.Name.c_str(), SearchBuffer))
				continue;
			Entry.Category = GetAssetCategory(Entry.Path, Entry.PathString, Entry.bDirectory);
			if (TypeFilterMask != 0 && !Entry.bDirectory && (TypeFilterMask & (1u << static_cast<uint32>(Entry.Category))) == 0)
				continue;
			Entries.push_back(std::move(Entry));
		}
	}
	std::sort(Entries.begin(), Entries.end(), [this](const FEntry& A, const FEntry& B)
	{
		if (A.bDirectory != B.bDirectory)
			return A.bDirectory;   // 폴더 먼저
		if (SortMode == ESortMode::Type && A.Category != B.Category)
			return A.Category < B.Category;
		return SortMode == ESortMode::NameDescending ? LessIgnoreCase(B.Name, A.Name) : LessIgnoreCase(A.Name, B.Name);
	});
	VisibleItemCount = static_cast<int32>(Entries.size());
	bSelectionVisible = false;
	SelectedVisibleCount = 0;

	// 단축키: Ctrl+A 모두 선택, F2 이름 변경, Delete 삭제 (그리드에 포커스가 있고 글자 입력 중이 아닐 때)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput
		&& !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
	{
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A))
		{
			SelectedPaths.clear();
			for (const FEntry& Entry : Entries)
				SelectedPaths.push_back(Entry.PathString);
		}
		// 화면에 보이는 선택만 대상으로 한다 (검색/필터로 숨은 항목을 모르게 지우지 않게)
		std::vector<fs::path> VisibleSelected;
		for (const FEntry& Entry : Entries)
			if (IsSelected(Entry.PathString))
				VisibleSelected.push_back(Entry.Path);
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && VisibleSelected.size() == 1)
			BeginRename(VisibleSelected[0]);
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !VisibleSelected.empty())
			BeginDelete(VisibleSelected);
	}
	bool bAnyItemHovered = false;

	if (Entries.empty())
	{
		ImGui::Dummy(ImVec2(0.0f, 12.0f * S));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f * S);
		ImGui::TextDisabled(SearchBuffer[0] || TypeFilterMask ? "No items match the current search or filters." : "This folder is empty.");
		return;
	}

	// 카드 크기와 열 수
	const float Thumb = std::floor(ThumbnailSize * S);
	const float FontSize = ImGui::GetFontSize();
	const float TypeFontSize = std::floor(FontSize * 0.85f);
	const float CardWidth = Thumb;
	const float CardHeight = Thumb + 6.0f * S + FontSize * 2.0f + 4.0f * S + TypeFontSize + 6.0f * S;
	const float Gap = 10.0f * S;
	const float Avail = ImGui::GetContentRegionAvail().x - 8.0f * S;
	const int32 Columns = ImMax(1, static_cast<int32>((Avail + Gap) / (CardWidth + Gap)));

	ImGui::Dummy(ImVec2(0.0f, 4.0f * S));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Gap, Gap));
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const float Rounding = 4.0f * S;

	// 폴더를 열 때 멈추지 않도록 썸네일은 프레임당 하나씩 로드한다. 그동안은 종류 아이콘을 보여 준다.
	bool bLoadedThumbnail = false;

	for (size_t Index = 0; Index < Entries.size(); ++Index)
	{
		const FEntry& Entry = Entries[Index];
		const FCategoryInfo& Info = GetCategoryInfo(Entry.Category);
		if (Index % Columns == 0)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f * S);
		else
			ImGui::SameLine();

		ImGui::PushID(Entry.PathString.c_str());

		// 실제 에셋일 때만 채워진다. 끌어다 놓기 원본이 된다.
		UTexture2D* TextureAsset = nullptr;
		UFont* FontAsset = nullptr;
		UTexture2D* Thumbnail = nullptr;
		if (!Entry.bDirectory)
		{
			URenderAsset* Asset = UAssetManager::FindLoadedAsset(Entry.PathString);
			if (!Asset && !bLoadedThumbnail)
			{
				// 메시는 썸네일로 쓰지 않으므로 드로어를 연 것만으로 로드하지 않는다
				const EAssetType UnloadedType = UAssetManager::GetUnloadedAssetType(Entry.PathString);
				if (UnloadedType == EAssetType::Texture || UnloadedType == EAssetType::Font)
				{
					Asset = UAssetManager::GetAssetByPath<URenderAsset>(Entry.PathString);
					bLoadedThumbnail = true;
				}
			}
			if (Asset && Asset->IsA<UTexture2D>())
			{
				TextureAsset = Cast<UTexture2D>(Asset);
				Thumbnail = TextureAsset;
			}
			else if (Asset && Asset->IsA<UFont>())
			{
				FontAsset = Cast<UFont>(Asset);
				Thumbnail = FontAsset->AtlasTexture;   // 폰트는 글리프 아틀라스를 미리보기로 쓴다
			}
		}

		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + CardWidth, Min.y + CardHeight);
		ImGui::InvisibleButton("##Item", ImVec2(CardWidth, CardHeight));
		const bool bHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		bAnyItemHovered |= bHovered;
		{
			// Ctrl: 하나씩 더하고 빼기, Shift: 기준 항목부터 범위, 그냥 클릭: 그것만.
			// 이미 선택된 항목을 그냥 누르면 여러 개를 같이 끌 수 있게 놓을 때까지 미룬다.
			const ImGuiIO& IO = ImGui::GetIO();
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				PendingSelectOnly.clear();
				if (IO.KeyShift && !HighlightPath.empty())
				{
					size_t Anchor = Entries.size();
					for (size_t I = 0; I < Entries.size(); ++I)
						if (Entries[I].PathString == HighlightPath) { Anchor = I; break; }
					if (Anchor == Entries.size())
						SelectOnly(Entry.PathString);
					else
					{
						if (!IO.KeyCtrl)
							SelectedPaths.clear();
						for (size_t I = ImMin(Anchor, Index); I <= ImMax(Anchor, Index); ++I)
							if (!IsSelected(Entries[I].PathString))
								SelectedPaths.push_back(Entries[I].PathString);
					}
				}
				else if (IO.KeyCtrl)
					ToggleSelected(Entry.PathString);
				else if (IsSelected(Entry.PathString))
				{
					PendingSelectOnly = Entry.PathString;
					HighlightPath = Entry.PathString;
				}
				else
					SelectOnly(Entry.PathString);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !IsSelected(Entry.PathString))
				SelectOnly(Entry.PathString);
			if (ImGui::IsItemDeactivated() && PendingSelectOnly == Entry.PathString)
			{
				const float Threshold = IO.MouseDragThreshold;
				if (IO.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < Threshold * Threshold)
					SelectOnly(Entry.PathString);
				PendingSelectOnly.clear();
			}
		}
		const bool bSelected = IsSelected(Entry.PathString);
		bSelectionVisible |= bSelected;
		SelectedVisibleCount += bSelected ? 1 : 0;
		if (bSelected && bScrollToHighlight)
		{
			ImGui::SetScrollHereY(0.5f);
			bScrollToHighlight = false;
		}

		const ImU32 TextColor = ImGui::GetColorU32(ImGuiCol_Text);
		if (Entry.bDirectory)
		{
			// 폴더: 카드 없이 큰 폴더 아이콘 + 이름 (UE5와 같다)
			if (bSelected || bHovered)
				DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(bSelected ? ImGuiCol_Header : ImGuiCol_ButtonHovered), Rounding);
			EditorIcons::Draw(DrawList, EEditorIcon::Folder, ImVec2(Min.x + CardWidth * 0.5f, Min.y + Thumb * 0.5f + 2.0f * S),
				Thumb * 0.8f, Info.Color);
			DrawClippedText(DrawList, ImVec2(Min.x + 4.0f * S, Min.y + Thumb + 6.0f * S), CardWidth - 8.0f * S, 2,
				FontSize, TextColor, Entry.Name.c_str(), true);
		}
		else
		{
			// 파일: 위쪽 썸네일 + 종류 색 줄 + 아래쪽 이름 / 종류
			const float InfoTop = Min.y + Thumb;
			DrawList->AddRectFilled(Min, ImVec2(Max.x, InfoTop), ImGui::GetColorU32(ImGuiCol_FrameBg), Rounding, ImDrawFlags_RoundCornersTop);
			DrawList->AddRectFilled(ImVec2(Min.x, InfoTop), Max,
				ImGui::GetColorU32(bSelected ? ImGuiCol_ButtonActive : bHovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button),
				Rounding, ImDrawFlags_RoundCornersBottom);
			DrawList->AddRectFilled(ImVec2(Min.x, InfoTop - 2.0f * S), ImVec2(Max.x, InfoTop), Info.Color);

			ImTextureID MeshThumbId = ImTextureID_Invalid;
			if (!Thumbnail && FMeshThumbnailRenderer::IsSupportedFile(Entry.Path, Entry.PathString))
			{
				std::error_code TimeEc;
				const fs::file_time_type LastWrite = fs::last_write_time(Entry.Path, TimeEc);
				MeshThumbId = MeshThumbnails.GetOrRequestThumbnail(Entry.Path, Entry.PathString, TimeEc ? fs::file_time_type{} : LastWrite);
			}

			if (Thumbnail && Thumbnail->GetResource() && Thumbnail->GetResource()->GetSRV())
			{
				// 가로세로 비율을 지켜 썸네일 칸에 맞춘다.
				const float Pad = 4.0f * S;
				const float Box = Thumb - Pad * 2.0f - 2.0f * S;
				float W = Box, Hgt = Box;
				const float TexW = static_cast<float>(Thumbnail->GetWidth());
				const float TexH = static_cast<float>(Thumbnail->GetHeight());
				if (TexW > 0.0f && TexH > 0.0f)
				{
					if (TexW >= TexH) Hgt = Box * TexH / TexW;
					else W = Box * TexW / TexH;
				}
				const ImVec2 ImgMin(std::floor(Min.x + (CardWidth - W) * 0.5f), std::floor(Min.y + Pad + (Box - Hgt) * 0.5f));
				DrawList->AddImage((ImTextureID)Thumbnail->GetResource()->GetSRV(), ImgMin, ImVec2(ImgMin.x + W, ImgMin.y + Hgt));
			}
			else if (MeshThumbId != ImTextureID_Invalid)
			{
				const ImVec2 ImgMin(std::floor(Min.x), std::floor(Min.y));
				const ImVec2 ImgMax(std::floor(Max.x), std::floor(InfoTop - 2.0f * S));
				DrawList->AddImageRounded(MeshThumbId, ImgMin, ImgMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
					IM_COL32_WHITE, Rounding, ImDrawFlags_RoundCornersTop);
			}
			else
			{
				EditorIcons::Draw(DrawList, Info.Icon, ImVec2(Min.x + CardWidth * 0.5f, Min.y + Thumb * 0.5f - 1.0f * S),
					std::floor(Thumb * 0.42f), Info.Color);
			}

			DrawClippedText(DrawList, ImVec2(Min.x + 4.0f * S, InfoTop + 6.0f * S), CardWidth - 8.0f * S, 2,
				FontSize, TextColor, Entry.Name.c_str(), false);
			DrawClippedText(DrawList, ImVec2(Min.x + 4.0f * S, Max.y - TypeFontSize - 5.0f * S), CardWidth - 8.0f * S, 1,
				TypeFontSize, ImGui::GetColorU32(bSelected ? ImGuiCol_Text : ImGuiCol_TextDisabled, bSelected ? 0.8f : 1.0f),
				Entry.Category == EAssetCategory::Other ? Entry.Path.extension().string().c_str() : Info.Label, false);
		}
		if (bSelected)
			DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImGuiCol_SeparatorActive), Rounding, 0, ImMax(1.0f, 1.5f * S));
		else if (bHovered)
			DrawList->AddRect(Min, Max, ImGui::GetColorU32(ImGuiCol_Text, 0.25f), Rounding, 0, 1.0f);

		if (bHovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			ImGui::SetTooltip("%s\n%s", Entry.Name.c_str(), Entry.Category == EAssetCategory::Other ? "File" : Info.Label);

		// 우클릭 메뉴: 이름 변경 / 삭제 + 폴더는 즐겨찾기, 스켈레탈 메시는 블렌드 스페이스 / 애님 블루프린트 만들기
		const bool bSkeletalMesh = Entry.Category == EAssetCategory::SkeletalMesh;
		if (ImGui::BeginPopupContextItem("##AssetContext"))
		{
			DrawItemContextMenu(Entry.Path, Entry.bDirectory, bSkeletalMesh, Entry.PathString);
			ImGui::EndPopup();
		}

		// 끌기: 이 항목이 선택돼 있으면 선택된 것 전부, 아니면 이것만 옮길 목록에 담는다.
		// 한 개만 끌 때 텍스처/폰트/Blueprint는 다른 패널에 놓을 수 있게 그 종류의 페이로드를 싣는다.
		// 미리보기 툴팁은 마우스 아래에 작게 뜨도록 여백을 줄인다 (스타일 값은 툴팁 창을 여는 순간에 쓰인다).
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * S, 4.0f * S));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f * S, 2.0f * S));
		const bool bDragSource = ImGui::BeginDragDropSource();
		ImGui::PopStyleVar(2);
		if (bDragSource)
		{
			if (!IsSelected(Entry.PathString))
				SelectOnly(Entry.PathString);
			PendingSelectOnly.clear();
			DragPaths = GetSelectedFsPaths();
			const bool bSingle = DragPaths.size() <= 1;
			if (bSingle && TextureAsset)
				// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
				ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));
			else if (bSingle && FontAsset)
				ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));
			else if (bSingle && Entry.Category == EAssetCategory::Blueprint)
				// Blueprint는 레벨 뷰포트로 끌어다 놓아 배치한다. 경로 문자열을 그대로 싣는다.
				ImGui::SetDragDropPayload(EditorDragDrop::Blueprint, Entry.PathString.c_str(), Entry.PathString.size() + 1);
			else
				ImGui::SetDragDropPayload(EditorDragDrop::ContentMove, nullptr, 0);

			// 작은 아이콘(텍스처면 썸네일) + 이름, 여러 개면 개수 배지. 놓을 폴더 위면 아랫줄에 대상 폴더.
			const float Icon = std::floor(20.0f * S);
			const ImVec2 IconMin = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(Icon, Icon));
			ImDrawList* TipDraw = ImGui::GetWindowDrawList();
			if (Thumbnail && Thumbnail->GetResource() && Thumbnail->GetResource()->GetSRV())
				TipDraw->AddImage((ImTextureID)Thumbnail->GetResource()->GetSRV(), IconMin, ImVec2(IconMin.x + Icon, IconMin.y + Icon));
			else
				EditorIcons::Draw(TipDraw, Entry.bDirectory ? EEditorIcon::Folder : Info.Icon,
					ImVec2(IconMin.x + Icon * 0.5f, IconMin.y + Icon * 0.5f), Icon * (Entry.bDirectory ? 0.95f : 0.8f), Info.Color);
			if (!bSingle)
			{
				// 아이콘 오른쪽 위에 개수 배지
				char Badge[16];
				std::snprintf(Badge, sizeof(Badge), "%d", static_cast<int32>(DragPaths.size()));
				const float BadgeFont = std::floor(FontSize * 0.75f);
				const ImVec2 BadgeText = ImGui::GetFont()->CalcTextSizeA(BadgeFont, FLT_MAX, 0.0f, Badge);
				const float BadgeH = BadgeFont + 2.0f * S;
				const float BadgeW = ImMax(BadgeH, BadgeText.x + 6.0f * S);
				const ImVec2 BMin(IconMin.x + Icon - BadgeW * 0.6f, IconMin.y - BadgeH * 0.35f);
				TipDraw->AddRectFilled(BMin, ImVec2(BMin.x + BadgeW, BMin.y + BadgeH), ImGui::GetColorU32(ImGuiCol_SeparatorActive), BadgeH * 0.5f);
				TipDraw->AddText(ImGui::GetFont(), BadgeFont, ImVec2(BMin.x + (BadgeW - BadgeText.x) * 0.5f, BMin.y + 1.0f * S),
					IM_COL32_WHITE, Badge);
			}
			ImGui::SameLine();
			ImGui::BeginGroup();
			FString Label = bSingle ? Entry.Name : (Entry.Name + " +" + std::to_string(DragPaths.size() - 1));
			if (Label.size() > 32)
				Label = Label.substr(0, 30) + "...";
			ImGui::TextUnformatted(Label.c_str());
			if (!PrevDragHoverTargetName.empty())
			{
				if (bPrevDragHoverValid)
					ImGui::TextDisabled("Move to %s", PrevDragHoverTargetName.c_str());
				else
					ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Can't move here");
			}
			else if (bSingle && Entry.Category == EAssetCategory::Blueprint)
				ImGui::TextDisabled("Drop in viewport to place");
			ImGui::EndGroup();
			ImGui::EndDragDropSource();
		}
		// 폴더 카드 위에 놓으면 그 폴더로 옮긴다
		if (Entry.bDirectory)
			AcceptMoveDrop(Entry.Path);

		// 더블 클릭: 폴더는 들어가고, 에셋은 맞는 에디터로 연다.
		if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			if (Entry.bDirectory)
			{
				NavigateTo(Entry.Path);
			}
			// TODO: UIEditor를 FAssetEditor 계열로 통합
			else if (Entry.Category == EAssetCategory::Widget && OnOpenWidget)
			{
				OnOpenWidget(Entry.PathString);
			}
			else if ((Entry.Path.extension() == ".obj" || FFbxMeshBake::IsBakedPath(Entry.PathString)
				|| MaterialFile::IsMaterialPath(Entry.PathString) || PhysicalMaterialFile::IsPhysicalMaterialPath(Entry.PathString)
				|| Entry.Category == EAssetCategory::Animation || Entry.Category == EAssetCategory::BlendSpace
				|| Entry.Category == EAssetCategory::AnimBlueprint || Entry.Category == EAssetCategory::Particle
				|| Entry.Category == EAssetCategory::Blueprint || Entry.Category == EAssetCategory::PhysicsAsset
				|| Entry.Category == EAssetCategory::InputAction || Entry.Category == EAssetCategory::InputMappingContext) && OnOpenMesh)
			{
				OnOpenMesh(Entry.PathString);
			}
		}

		ImGui::PopID();
	}
	ImGui::PopStyleVar();
	ImGui::Dummy(ImVec2(0.0f, 4.0f * S));

	// 빈 곳을 누르면 선택을 푼다 (Ctrl/Shift를 누르고 있으면 유지)
	if (!bAnyItemHovered && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
		&& !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
		ClearSelection();
}

namespace
{
	enum class ECreateAction : uint8
	{
		BlueprintClass,
		Widget,
		ParticleSystem,
		InputAction,
		InputMappingContext,
		BlendSpace,			// 스켈레탈 메시 우클릭에서만 만든다
		AnimBlueprint,		// 스켈레탈 메시 우클릭에서만 만든다
	};

	struct FCreateItem
	{
		const char* Label;
		ECreateAction Action;
		EAssetCategory Category;	// 아이콘과 색
		const char* Submenu;		// 종류별 하위 메뉴 이름
		bool bFeatured;				// 위쪽 큰 줄에도 보인다
		const char* DisabledHint;	// 있으면 이 메뉴에서는 못 만들고 이유를 툴팁으로 보인다
	};

	// 같은 Submenu끼리 붙여 두고, 하위 메뉴가 UE처럼 이름순으로 보이게 Submenu 이름순으로 둔다
	const FCreateItem CreateItems[] = {
		{ "Animation Blueprint",   ECreateAction::AnimBlueprint,       EAssetCategory::AnimBlueprint,       "Animation",      false, "Right-click a Skeletal Mesh to create an Animation Blueprint for it." },
		{ "Blend Space 1D",        ECreateAction::BlendSpace,          EAssetCategory::BlendSpace,          "Animation",      false, "Right-click a Skeletal Mesh to create a Blend Space for it." },
		{ "Blueprint Class",       ECreateAction::BlueprintClass,      EAssetCategory::Blueprint,           "Blueprint",      true,  nullptr },
		{ "Particle System",       ECreateAction::ParticleSystem,      EAssetCategory::Particle,            "FX",             true,  nullptr },
		{ "Input Action",          ECreateAction::InputAction,         EAssetCategory::InputAction,         "Input",          false, nullptr },
		{ "Input Mapping Context", ECreateAction::InputMappingContext, EAssetCategory::InputMappingContext, "Input",          false, nullptr },
		{ "Widget Blueprint",      ECreateAction::Widget,              EAssetCategory::Widget,              "User Interface", true,  nullptr },
	};

	// 주요 에셋 줄: 어두운 타일 안 아이콘 + 타일 아래 종류 색 띠 + 이름 (UE5 Add 메뉴의 큰 줄)
	bool FeaturedRow(const char* Label, EEditorIcon Icon, ImU32 Color)
	{
		const float S = EditorIcons::GetScale();
		const float H = 34.0f * S;
		const float Tile = 26.0f * S;
		const float TextX = 14.0f * S + Tile;

		ImGui::PushID(Label);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const float Width = ImMax(TextX + ImGui::CalcTextSize(Label).x + 24.0f * S, ImGui::GetContentRegionAvail().x);
		const bool bPressed = ImGui::Selectable("##Featured", false, ImGuiSelectableFlags_None, ImVec2(Width, H));

		ImDrawList* DL = ImGui::GetWindowDrawList();
		const ImVec2 TileMin(Min.x + 4.0f * S, Min.y + (H - Tile) * 0.5f);
		const ImVec2 TileMax(TileMin.x + Tile, TileMin.y + Tile);
		DL->AddRectFilled(TileMin, TileMax, IM_COL32(16, 16, 16, 255), 2.0f * S);
		EditorIcons::Draw(DL, Icon, ImVec2((TileMin.x + TileMax.x) * 0.5f, (TileMin.y + TileMax.y) * 0.5f - 1.0f * S),
			16.0f * S, IM_COL32(235, 235, 235, 255));
		DL->AddRectFilled(ImVec2(TileMin.x, TileMax.y - 3.0f * S), TileMax, Color);
		DL->AddText(ImVec2(Min.x + TextX, Min.y + (H - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), Label);

		ImGui::PopID();
		return bPressed;
	}

	// 종류 아이콘 + 이름 한 줄. 못 만드는 항목은 흐리게 두고 이유를 툴팁으로 보인다
	bool CreateItemRow(const FCreateItem& Item)
	{
		const bool bEnabled = Item.DisabledHint == nullptr;
		const bool bPressed = EditorIcons::MenuRow(Item.Label, GetCategoryInfo(Item.Category).Icon, false, bEnabled);
		if (!bEnabled) ImGui::SetItemTooltip("%s", Item.DisabledHint);
		return bPressed && bEnabled;
	}
}

void FContentDrawerPanel::DrawCreateMenu()
{
	const float S = EditorIcons::GetScale();

	auto Run = [this](ECreateAction Action)
	{
		switch (Action)
		{
		case ECreateAction::BlueprintClass:      MakeUniqueBlueprintName(); bOpenCreateBlueprint = true; break;
		case ECreateAction::Widget:              strcpy_s(NewAssetName, "NewWidget"); bOpenCreateWidget = true; break;
		case ECreateAction::ParticleSystem:      strcpy_s(NewAssetName, "NewParticleSystem"); bOpenCreateParticle = true; break;
		case ECreateAction::InputAction:         strcpy_s(NewAssetName, "IA_NewAction"); bOpenCreateInputAction = true; break;
		case ECreateAction::InputMappingContext: strcpy_s(NewAssetName, "IMC_NewContext"); bOpenCreateInputContext = true; break;
		default: break;
		}
	};
	auto NewFolder = [this]()
	{
		strcpy_s(NewAssetName, "NewFolder");
		bOpenNewFolder = true;
	};

	// 메뉴 폭을 UE 메뉴와 비슷하게 잡는다
	ImGui::Dummy(ImVec2(280.0f * S, 0.0f));

	// 검색: 메뉴가 열릴 때 비우고 바로 입력을 받는다
	if (ImGui::IsWindowAppearing())
	{
		CreateMenuSearch[0] = '\0';
		ImGui::SetKeyboardFocusHere();
	}
	EditorIcons::SearchBox("##CreateMenuSearch", CreateMenuSearch, sizeof(CreateMenuSearch), "Start typing to search");

	// 검색어가 있으면 하위 메뉴를 펼친 목록에서 이름(또는 종류 이름)으로 거른다
	if (CreateMenuSearch[0] != '\0')
	{
		bool bAny = false;
		if (EditorIcons::MatchesFilter("New Folder", CreateMenuSearch))
		{
			bAny = true;
			if (EditorIcons::MenuRow("New Folder", EEditorIcon::Folder)) NewFolder();
		}
		for (const FCreateItem& Item : CreateItems)
		{
			if (!EditorIcons::MatchesFilter(Item.Label, CreateMenuSearch) && !EditorIcons::MatchesFilter(Item.Submenu, CreateMenuSearch))
				continue;
			bAny = true;
			if (CreateItemRow(Item)) Run(Item.Action);
		}
		if (!bAny) ImGui::TextDisabled("  No results");
		return;
	}

	EditorIcons::MenuSection("Get");
	if (EditorIcons::MenuRow("Import Static Mesh...", EEditorIcon::Import, false, OnImportFbx != nullptr) && OnImportFbx)
		OnImportFbx(EFbxImportMode::Static);
	if (EditorIcons::MenuRow("Import Skeletal Mesh...", EEditorIcon::Import, false, OnImportFbx != nullptr) && OnImportFbx)
		OnImportFbx(EFbxImportMode::Skeletal);

	EditorIcons::MenuSection("Create");
	if (EditorIcons::MenuRow("New Folder", EEditorIcon::Folder)) NewFolder();
	if (EditorIcons::MenuRow("Material (.mat)", EEditorIcon::File))
	{
		for (int32 Idx = 0; Idx < 1000; ++Idx)
		{
			const FString Name = Idx == 0 ? "M_NewMaterial.mat" : ("M_NewMaterial" + std::to_string(Idx) + ".mat");
			const fs::path OutPath = CurrentPath / Name;
			std::error_code Ec;
			if (!fs::exists(OutPath, Ec))
			{
				UMaterial* Mat = NewObject<UMaterial>();
				FString Err;
				if (Mat && UAssetManager::SaveMaterial(*Mat, OutPath.generic_string(), Err) && OnOpenMesh)
					OnOpenMesh(OutPath.generic_string());
				break;
			}
		}
		ImGui::CloseCurrentPopup();
	}
	if (EditorIcons::MenuRow("Physical Material (.pmat)", EEditorIcon::File))
	{
		for (int32 Idx = 0; Idx < 1000; ++Idx)
		{
			const FString Name = Idx == 0 ? "PM_NewPhysicalMaterial.pmat" : ("PM_NewPhysicalMaterial" + std::to_string(Idx) + ".pmat");
			const fs::path OutPath = CurrentPath / Name;
			std::error_code Ec;
			if (!fs::exists(OutPath, Ec))
			{
				UPhysicalMaterial* PM = NewObject<UPhysicalMaterial>();
				FString Err;
				if (PhysicalMaterialFile::Write(OutPath, *PM, Err) && OnOpenMesh)
					OnOpenMesh(OutPath.generic_string());
				break;
			}
		}
		ImGui::CloseCurrentPopup();
	}

	ImGui::Separator();
	for (const FCreateItem& Item : CreateItems)
	{
		if (!Item.bFeatured) continue;
		const FCategoryInfo& Info = GetCategoryInfo(Item.Category);
		if (FeaturedRow(Item.Label, Info.Icon, Info.Color)) Run(Item.Action);
	}

	ImGui::Separator();
	// 종류별 하위 메뉴. 같은 Submenu가 이어져 있으므로 이름이 바뀔 때마다 하나씩 연다
	const ImVec2 RowSpacing(ImGui::GetStyle().ItemSpacing.x, 9.0f * S);
	const char* PrevSubmenu = nullptr;
	for (const FCreateItem& Head : CreateItems)
	{
		if (PrevSubmenu && std::strcmp(PrevSubmenu, Head.Submenu) == 0) continue;
		PrevSubmenu = Head.Submenu;

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, RowSpacing);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f * S);
		const bool bOpen = ImGui::BeginMenu(Head.Submenu);
		ImGui::PopStyleVar();
		if (!bOpen) continue;

		for (const FCreateItem& Item : CreateItems)
		{
			if (std::strcmp(Item.Submenu, Head.Submenu) != 0) continue;
			if (CreateItemRow(Item)) Run(Item.Action);
		}
		ImGui::EndMenu();
	}
}

// 만들기 창. 메뉴 안에서 바로 열 수 없어 플래그만 세우고 여기(창 기준)에서 연다.
void FContentDrawerPanel::DrawCreatePopups()
{
	if (bOpenCreateWidget) { ImGui::OpenPopup("Create Widget"); bOpenCreateWidget = false; }
	if (bOpenCreateParticle) { ImGui::OpenPopup("Create Particle System"); bOpenCreateParticle = false; }
	if (bOpenNewFolder) { ImGui::OpenPopup("New Folder"); bOpenNewFolder = false; }
	if (bOpenCreateInputAction) { ImGui::OpenPopup("Create Input Action"); bOpenCreateInputAction = false; }
	if (bOpenCreateInputContext) { ImGui::OpenPopup("Create Input Mapping Context"); bOpenCreateInputContext = false; }
	if (bOpenCreateBlueprint) { ImGui::OpenPopup("Pick Parent Class"); bOpenCreateBlueprint = false; BlueprintParentClass = "AActor"; BlueprintClassSearch[0] = '\0'; }
	DrawCreateBlueprintPopup();

	// 이름 입력 + 같은 이름 검사 + Create/Cancel. Create를 누르면 OnCreate(만들 경로)를 부른다.
	auto NameDialog = [this](const char* Title, const char* Extension, const std::function<void(const fs::path&)>& OnCreate)
	{
		if (!ImGui::BeginPopupModal(Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		const bool bEnter = ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName), ImGuiInputTextFlags_EnterReturnsTrue);
		const fs::path Target = CurrentPath / (FString(NewAssetName) + Extension);
		std::error_code Error;
		const bool bExists = fs::exists(Target, Error);
		if (bExists) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "A file with this name already exists.");

		const bool bValid = !bExists && NewAssetName[0] != '\0';
		ImGui::BeginDisabled(!bValid);
		if ((ImGui::Button("Create") || (bEnter && bValid)))
		{
			OnCreate(Target);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	};

	NameDialog("Create Widget", ".widget", [this](const fs::path& Target)
	{
		if (OnCreateWidget)
			OnCreateWidget(Target.generic_string());   // "Content/UI/GameOver.widget" 형식
	});

	NameDialog("Create Particle System", ".particle", [this](const fs::path& Target)
	{
		FString Error;
		if (ParticleSystemFile::CreateDefault(Target, Error))
		{
			SelectOnly(Target.generic_string());
			if (OnOpenMesh) OnOpenMesh(Target.generic_string());   // 만들자마자 에디터로 연다
		}
		else
		{
			HTR_LOG(Warning, "Failed to create particle system: {}", Error);
		}
	});

	// 입력 에셋은 빈 파일을 만들고 바로 에디터로 연다
	auto CreateInputAsset = [this](const fs::path& Target, bool bContext)
	{
		FString Error;
		const FString Path = Target.generic_string();
		const bool bCreated = bContext
			? FInputAssetLibrary::CreateDefaultMappingContext(Path, Error)
			: FInputAssetLibrary::CreateDefaultInputAction(Path, Error);
		if (!bCreated)
		{
			HTR_LOG(Warning, "Failed to create input asset: {}", Error);
			return;
		}
		SelectOnly(Path);
		if (OnOpenMesh) OnOpenMesh(Path);
	};
	NameDialog("Create Input Action", FInputAssetLibrary::ActionExtension,
		[CreateInputAsset](const fs::path& Target) { CreateInputAsset(Target, false); });
	NameDialog("Create Input Mapping Context", FInputAssetLibrary::ContextExtension,
		[CreateInputAsset](const fs::path& Target) { CreateInputAsset(Target, true); });

	NameDialog("New Folder", "", [this](const fs::path& Target)
	{
		std::error_code Error;
		if (fs::create_directory(Target, Error))
			SelectOnly(Target.generic_string());
		else
			HTR_LOG(Warning, "Failed to create folder: {}", Target.generic_string());
	});
}

// ---------------- Blueprint Class 만들기 (UE5 "Pick Parent Class") ----------------

namespace
{
	struct FCommonParent
	{
		const char* ClassName;
		const char* Label;
		EEditorIcon Icon;
		const char* Description;
	};

	// UE5 창의 COMMON 목록. 엔진에 없는 클래스는 건너뛴다.
	const FCommonParent CommonParents[] = {
		{ "AActor",            "Actor",             EEditorIcon::ClassActor,          "An Actor is an object that can be placed or spawned in the world." },
		{ "APawn",             "Pawn",              EEditorIcon::ClassPawn,           "A Pawn is an actor that can be 'possessed' and receive input from a controller." },
		{ "ACharacter",        "Character",         EEditorIcon::ClassCharacter,      "A character is a type of Pawn that includes the ability to walk around." },
		{ "APlayerController", "Player Controller", EEditorIcon::ClassController,     "A Player Controller is an actor responsible for controlling a Pawn used by the player." },
		{ "AGameModeBase",     "Game Mode Base",    EEditorIcon::ClassGameMode,       "Game Mode Base defines the game being played, its rules, scoring, and other facets of the game type." },
		{ "UActorComponent",   "Actor Component",   EEditorIcon::ClassComponent,      "An ActorComponent is a reusable component that can be added to any actor." },
		{ "USceneComponent",   "Scene Component",   EEditorIcon::ClassSceneComponent, "A Scene Component is a component that has a scene transform and can be attached to other scene components." },
	};

	// Blueprint 부모가 될 수 있나. 지금 데이터 Blueprint는 액터만 지원하고, 추상 클래스는 만들 수 없다.
	bool CanBeBlueprintParent(const UClass* Class)
	{
		return FBlueprint::IsValidParentClass(Class);
	}

	// 클래스 아이콘. 가장 가까운 COMMON 부모의 아이콘을 쓴다.
	EEditorIcon GetClassIcon(const UClass* Class)
	{
		for (const UClass* It = Class; It; It = It->Super)
		{
			for (const FCommonParent& Parent : CommonParents)
			{
				if (It->Name == Parent.ClassName)
					return Parent.Icon;
			}
		}
		return EEditorIcon::ClassActor;
	}

	// 동그라미 안의 "?" (UE5 도움말 아이콘). 마우스를 올리면 Tooltip을 보인다.
	void HelpIcon(const char* Id, const char* Tooltip)
	{
		const float S = EditorIcons::GetScale();
		const float Size = 18.0f * S;
		ImGui::InvisibleButton(Id, ImVec2(Size, Size));
		const bool bHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Center(Min.x + Size * 0.5f, Min.y + Size * 0.5f);
		const ImU32 Color = bHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddCircle(Center, Size * 0.45f, Color, 0, 1.5f * S);
		const ImVec2 TextSize = ImGui::CalcTextSize("?");
		DrawList->AddText(ImVec2(Center.x - TextSize.x * 0.5f, Center.y - TextSize.y * 0.5f), Color, "?");
		if (bHovered && Tooltip)
			ImGui::SetTooltip("%s", Tooltip);
	}

	// 아이콘 + 이름이 왼쪽 정렬된 넓은 버튼 (UE5 Pick Parent Class 행의 버튼).
	bool ClassButton(const char* Id, EEditorIcon Icon, const char* Label, const ImVec2& Size)
	{
		const float S = EditorIcons::GetScale();
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * S);
		const bool bPressed = ImGui::Button(Id, Size);
		ImGui::PopStyleVar();

		const ImVec2 Min = ImGui::GetItemRectMin();
		const float IconSize = 16.0f * S;
		const float CenterY = Min.y + Size.y * 0.5f;
		const ImU32 TextColor = ImGui::GetColorU32(ImGuiCol_Text);
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		EditorIcons::Draw(DrawList, Icon, ImVec2(Min.x + 12.0f * S + IconSize * 0.5f, CenterY), IconSize, TextColor);
		DrawList->AddText(ImVec2(Min.x + 20.0f * S + IconSize, CenterY - ImGui::GetTextLineHeight() * 0.5f), TextColor, Label);
		return bPressed;
	}

	// UE5처럼 배경 없는 "▼ COMMON" 접기 제목.
	bool SectionHeader(const char* Label, bool bDefaultOpen)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.06f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.10f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
		if (ImGui::IsWindowAppearing())
			ImGui::SetNextItemOpen(bDefaultOpen);
		const bool bOpen = ImGui::CollapsingHeader(Label, ImGuiTreeNodeFlags_SpanAvailWidth);
		ImGui::PopStyleColor(4);
		return bOpen;
	}
}

void FContentDrawerPanel::MakeUniqueBlueprintName()
{
	for (int32 Index = 0; ; ++Index)
	{
		const FString Name = Index == 0 ? FString("BP_NewBlueprint") : "BP_NewBlueprint" + std::to_string(Index);
		std::error_code Error;
		if (!fs::exists(CurrentPath / (Name + FBlueprint::Extension), Error) && !FindClass(Name))
		{
			strcpy_s(NewAssetName, Name.c_str());
			return;
		}
	}
}

void FContentDrawerPanel::CreateBlueprintAsset(UClass* ParentClass)
{
	const fs::path Target = CurrentPath / (FString(NewAssetName) + FBlueprint::Extension);
	FString CreateError;
	if (FBlueprint::SaveBlueprint(Target, ParentClass, nullptr, CreateError))
	{
		SelectOnly(Target.generic_string());
		if (OnOpenMesh) OnOpenMesh(Target.generic_string());   // 만들자마자 Blueprint 에디터로 연다
	}
	else
	{
		HTR_LOG(Warning, "Failed to create blueprint: {}", CreateError);
	}
}

void FContentDrawerPanel::DrawBlueprintClassTree(UClass* Class, const TArray<UClass*>& Classes, UClass*& OutPicked)
{
	TArray<UClass*> Children;
	for (UClass* Other : Classes)
	{
		if (Other->Super == Class)
			Children.Add(Other);
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (Children.IsEmpty()) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	if (BlueprintParentClass == Class->Name) Flags |= ImGuiTreeNodeFlags_Selected;
	if (Class == AActor::StaticClass() || Class == UActorComponent::StaticClass()) Flags |= ImGuiTreeNodeFlags_DefaultOpen;

	const bool bCanPick = CanBeBlueprintParent(Class);
	if (!bCanPick) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	const FString Label = Class->Name + (FBlueprint::IsBlueprintClass(Class) ? "  (Blueprint)" : "");
	const bool bOpen = ImGui::TreeNodeEx(Class, Flags, "%s", Label.c_str());
	if (!bCanPick) ImGui::PopStyleColor();

	if (bCanPick && ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		BlueprintParentClass = Class->Name;
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OutPicked = Class;
	}
	if (!bCanPick)
		ImGui::SetItemTooltip("%s is abstract and can't be used as a parent.", Class->Name.c_str());

	if (bOpen && !Children.IsEmpty())
	{
		for (UClass* Child : Children)
			DrawBlueprintClassTree(Child, Classes, OutPicked);
		ImGui::TreePop();
	}
}

// UE5의 "Pick Parent Class" 창. COMMON 버튼을 누르면 그 부모로 바로 만들고,
// ALL CLASSES에서는 모든 액터 클래스를 트리로 보여 주고 검색한다 (고르고 Select, 또는 더블클릭).
void FContentDrawerPanel::DrawCreateBlueprintPopup()
{
	const float S = EditorIcons::GetScale();
	ImGui::SetNextWindowSize(ImVec2(620.0f * S, 0.0f), ImGuiCond_Appearing);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.5f, 0.5f));
	bool bKeepOpen = true;
	const bool bVisible = ImGui::BeginPopupModal("Pick Parent Class", &bKeepOpen, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::PopStyleVar();
	if (!bVisible)
		return;

	// 파일 이름이 곧 클래스 이름이라 같은 이름의 파일이나 클래스(C++ 또는 다른 BP)가 있으면 만들 수 없다.
	const fs::path Target = CurrentPath / (FString(NewAssetName) + FBlueprint::Extension);
	std::error_code FsError;
	const char* NameError = nullptr;
	if (NewAssetName[0] == '\0') NameError = "Enter a name.";
	else if (fs::exists(Target, FsError)) NameError = "A file with this name already exists.";
	else if (FindClass(NewAssetName)) NameError = "A class with this name already exists.";

	UClass* Picked = nullptr;
	const float WindowWidth = 620.0f * S - ImGui::GetStyle().WindowPadding.x * 2.0f;

	// ---- COMMON ----
	if (SectionHeader("COMMON", true))
	{
		const float ButtonWidth = 280.0f * S;
		const float ButtonHeight = 28.0f * S;
		const float HelpWidth = 18.0f * S;
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		const float DescWidth = WindowWidth - ImGui::GetStyle().WindowPadding.x * 2.0f - ButtonWidth - HelpWidth - Spacing * 2.0f;
		const float RowHeight = std::max(ButtonHeight, ImGui::GetTextLineHeightWithSpacing() * 2.0f);

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.09f, 0.09f, 1.0f));
		ImGui::BeginChild("##CommonClasses", ImVec2(WindowWidth, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleColor();
		for (const FCommonParent& Parent : CommonParents)
		{
			UClass* Class = FindClass(Parent.ClassName);
			if (!Class)
				continue;   // 이 엔진에 없는 클래스 (UCLASS로 등록되지 않음)
			const bool bSupported = CanBeBlueprintParent(Class);

			ImGui::PushID(Parent.ClassName);
			const float RowY = ImGui::GetCursorPosY();

			ImGui::SetCursorPosY(RowY + (RowHeight - ButtonHeight) * 0.5f);
			ImGui::BeginDisabled(!bSupported || NameError != nullptr);
			if (ClassButton("##Pick", Parent.Icon, Parent.Label, ImVec2(ButtonWidth, ButtonHeight)))
				Picked = Class;
			ImGui::EndDisabled();
			if (!bSupported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s is abstract and can't be used as a parent.", Parent.ClassName);

			ImGui::SameLine();
			ImGui::SetCursorPosY(RowY);
			ImGui::BeginGroup();
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + DescWidth);
			ImGui::TextUnformatted(Parent.Description);
			ImGui::PopTextWrapPos();
			ImGui::EndGroup();

			ImGui::SameLine(WindowWidth - HelpWidth - ImGui::GetStyle().WindowPadding.x);
			ImGui::SetCursorPosY(RowY + (RowHeight - HelpWidth) * 0.5f);
			const FString Help = FString(Parent.ClassName) + "\n" + Parent.Description;
			HelpIcon("##Help", Help.c_str());

			ImGui::SetCursorPosY(RowY + RowHeight + ImGui::GetStyle().ItemSpacing.y);
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	// ---- ALL CLASSES ----
	const bool bAllClassesOpen = SectionHeader("ALL CLASSES", false);
	if (bAllClassesOpen)
	{
		EditorIcons::SearchBox("##BlueprintParentSearch", BlueprintClassSearch, sizeof(BlueprintClassSearch), "Search", WindowWidth);

		// 액터 계열과 컴포넌트 계열 두 갈래
		TArray<UClass*> Classes;
		Classes.Add(AActor::StaticClass());
		GetDerivedClasses(AActor::StaticClass(), Classes);
		Classes.Add(UActorComponent::StaticClass());
		GetDerivedClasses(UActorComponent::StaticClass(), Classes);
		std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });

		ImGui::BeginChild("##ParentClassList", ImVec2(WindowWidth, 260.0f * S), ImGuiChildFlags_Borders);
		if (BlueprintClassSearch[0] == '\0')
		{
			DrawBlueprintClassTree(AActor::StaticClass(), Classes, Picked);
			DrawBlueprintClassTree(UActorComponent::StaticClass(), Classes, Picked);
		}
		else
		{
			// 검색 중에는 트리 대신 맞는 클래스만 평평하게 보여 준다.
			for (UClass* Class : Classes)
			{
				if (!CanBeBlueprintParent(Class) || !EditorIcons::MatchesFilter(Class->Name.c_str(), BlueprintClassSearch))
					continue;
				ImGui::PushID(Class);
				const FString Label = Class->Name + (FBlueprint::IsBlueprintClass(Class) ? "  (Blueprint)" : "");
				if (ImGui::Selectable(Label.c_str(), BlueprintParentClass == Class->Name, ImGuiSelectableFlags_AllowDoubleClick))
				{
					BlueprintParentClass = Class->Name;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						Picked = Class;
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::TextDisabled("Selected: %s", BlueprintParentClass.c_str());
	}

	// ---- 이름 + Select / Cancel ----
	ImGui::Spacing();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Name");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(220.0f * S);
	ImGui::InputText("##BlueprintName", NewAssetName, sizeof(NewAssetName));
	if (NameError)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", NameError);
	}

	const ImGuiStyle& Style = ImGui::GetStyle();
	const ImVec2 ButtonSize(96.0f * S, 0.0f);
	const float ButtonsWidth = bAllClassesOpen ? ButtonSize.x * 2.0f + Style.ItemSpacing.x : ButtonSize.x;
	ImGui::SameLine(Style.WindowPadding.x + WindowWidth - ButtonsWidth);
	if (bAllClassesOpen)
	{
		UClass* Selected = FindClass(BlueprintParentClass);
		ImGui::BeginDisabled(!CanBeBlueprintParent(Selected) || NameError != nullptr);
		if (ImGui::Button("Select", ButtonSize))
			Picked = Selected;
		ImGui::EndDisabled();
		ImGui::SameLine();
	}
	const bool bCancel = ImGui::Button("Cancel", ButtonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape);

	if (Picked && !NameError && CanBeBlueprintParent(Picked))
	{
		CreateBlueprintAsset(Picked);
		ImGui::CloseCurrentPopup();
	}
	else if (bCancel || !bKeepOpen)
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
