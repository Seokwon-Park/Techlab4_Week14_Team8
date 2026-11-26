#include "EnginePCH.h"
#include "Editor/ProjectBrowser/ProjectBrowserView.h"

#include <algorithm>
#include <cmath>

#include <ShlObj.h>
#include <shellapi.h>
#include <wrl/client.h>

#include "Core/Misc/Paths.h"
#include "Editor/ProjectBrowser/ProjectCode.h"
#include "Editor/ProjectBrowser/ProjectThumbnail.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace
{
	// ---- 색 (UE 런처와 비슷한 어두운 회색 + 하늘색 강조) ----
	constexpr ImU32 SidebarBg     = IM_COL32(38, 38, 40, 255);
	constexpr ImU32 ContentBg     = IM_COL32(21, 21, 23, 255);
	constexpr ImU32 PanelBg       = IM_COL32(28, 28, 32, 255);
	constexpr ImU32 PanelBorder   = IM_COL32(62, 62, 68, 255);
	constexpr ImU32 Accent        = IM_COL32(38, 187, 255, 255);
	constexpr ImU32 AccentHovered = IM_COL32(96, 208, 255, 255);
	constexpr ImU32 AccentText    = IM_COL32(12, 12, 14, 255);
	constexpr ImU32 Selection     = IM_COL32(0, 112, 224, 255);
	constexpr ImU32 GreyButton    = IM_COL32(72, 72, 76, 255);
	constexpr ImU32 GreyHovered   = IM_COL32(92, 92, 98, 255);
	constexpr ImU32 NavActive     = IM_COL32(68, 68, 72, 255);
	constexpr ImU32 NavHovered    = IM_COL32(56, 56, 60, 255);
	constexpr ImU32 TextColor     = IM_COL32(235, 235, 238, 255);
	constexpr ImU32 MutedText     = IM_COL32(160, 160, 166, 255);
	constexpr ImU32 ErrorColor    = IM_COL32(255, 110, 110, 255);

	// 레이아웃 배율. 브라우저 창과 에디터의 글자 크기가 달라도 같은 비율로 보이게 글자 크기에 맞춘다.
	float Scale() { return ImGui::GetFontSize() / 16.0f; }
	// PushFont(nullptr, 크기)에 넘길 크기 (글자 배율이 곱해지기 전 크기)
	float FontSize(float Ratio)
	{
		const float Base = ImGui::GetStyle().FontSizeBase;
		return (Base > 0.0f ? Base : ImGui::GetFontSize()) * Ratio;
	}

	ImVec2 Add(const ImVec2& A, const ImVec2& B) { return ImVec2(A.x + B.x, A.y + B.y); }

	// ---- 아이콘 (엔진 아이콘 텍스처가 없는 브라우저 창에서도 그려야 해서 선으로 그린다) ----
	using FIconFn = void(*)(ImDrawList*, ImVec2, float, ImU32);

	void IconPlusCircle(ImDrawList* DL, ImVec2 C, float S, ImU32 Col)
	{
		DL->AddCircle(C, S * 0.45f, Col, 0, 1.6f);
		DL->AddLine(ImVec2(C.x - S * 0.22f, C.y), ImVec2(C.x + S * 0.22f, C.y), Col, 1.6f);
		DL->AddLine(ImVec2(C.x, C.y - S * 0.22f), ImVec2(C.x, C.y + S * 0.22f), Col, 1.6f);
	}

	void IconFolder(ImDrawList* DL, ImVec2 C, float S, ImU32 Col)
	{
		const float L = C.x - S * 0.45f, R = C.x + S * 0.45f, T = C.y - S * 0.32f, B = C.y + S * 0.34f;
		const ImVec2 Points[6] = { ImVec2(L, B), ImVec2(L, T), ImVec2(L + S * 0.32f, T), ImVec2(L + S * 0.42f, T + S * 0.12f),
			ImVec2(R, T + S * 0.12f), ImVec2(R, B) };
		DL->AddPolyline(Points, 6, Col, ImDrawFlags_Closed, 1.6f);
	}

	void IconHouse(ImDrawList* DL, ImVec2 C, float S, ImU32 Col)
	{
		const float L = C.x - S * 0.36f, R = C.x + S * 0.36f, B = C.y + S * 0.4f, Mid = C.y - S * 0.02f;
		const ImVec2 Points[6] = { ImVec2(L, Mid), ImVec2(C.x, C.y - S * 0.42f), ImVec2(R, Mid), ImVec2(R, B), ImVec2(L, B), ImVec2(L, Mid) };
		DL->AddPolyline(Points, 6, Col, 0, 1.6f);
		DL->AddRect(ImVec2(C.x - S * 0.1f, C.y + S * 0.12f), ImVec2(C.x + S * 0.1f, B), Col, 0.0f, 0, 1.4f);
	}

	void IconList(ImDrawList* DL, ImVec2 C, float S, ImU32 Col)
	{
		DL->AddRect(ImVec2(C.x - S * 0.4f, C.y - S * 0.36f), ImVec2(C.x + S * 0.4f, C.y + S * 0.36f), Col, 2.0f, 0, 1.4f);
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const float Y = C.y - S * 0.16f + Row * S * 0.16f;
			DL->AddLine(ImVec2(C.x - S * 0.24f, Y), ImVec2(C.x + S * 0.24f, Y), Col, 1.4f);
		}
	}

	// 아이콘 + 글자를 가운데 놓은 채운 버튼
	bool FilledButton(const char* Id, const char* Label, FIconFn Icon, ImU32 Bg, ImU32 BgHovered, ImU32 TextCol, ImVec2 Size)
	{
		const float S = Scale();
		const ImVec2 P = ImGui::GetCursorScreenPos();
		const bool bPressed = ImGui::InvisibleButton(Id, Size);
		const bool bHovered = ImGui::IsItemHovered();
		ImDrawList* DL = ImGui::GetWindowDrawList();
		DL->AddRectFilled(P, Add(P, Size), bHovered ? BgHovered : Bg, 4.0f * S);
		const float IconSize = Icon ? ImGui::GetFontSize() * 1.15f : 0.0f;
		const float Gap = Icon ? 8.0f * S : 0.0f;
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		const float X = P.x + (Size.x - (IconSize + Gap + TextSize.x)) * 0.5f;
		const float MidY = P.y + Size.y * 0.5f;
		if (Icon)
			Icon(DL, ImVec2(X + IconSize * 0.5f, MidY), IconSize, TextCol);
		DL->AddText(ImVec2(X + IconSize + Gap, MidY - TextSize.y * 0.5f), TextCol, Label);
		return bPressed;
	}

	// 테두리만 있는 버튼 ("See all projects")
	bool OutlineButton(const char* Id, const char* Label, ImVec2 Size)
	{
		const float S = Scale();
		const ImVec2 P = ImGui::GetCursorScreenPos();
		const bool bPressed = ImGui::InvisibleButton(Id, Size);
		const bool bHovered = ImGui::IsItemHovered();
		ImDrawList* DL = ImGui::GetWindowDrawList();
		if (bHovered)
			DL->AddRectFilled(P, Add(P, Size), IM_COL32(255, 255, 255, 18), 6.0f * S);
		DL->AddRect(P, Add(P, Size), IM_COL32(200, 200, 205, bHovered ? 255 : 170), 6.0f * S, 0, 1.2f * S);
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		DL->AddText(ImVec2(P.x + (Size.x - TextSize.x) * 0.5f, P.y + (Size.y - TextSize.y) * 0.5f), TextColor, Label);
		return bPressed;
	}

	float OutlineButtonWidth(const char* Label) { return ImGui::CalcTextSize(Label).x + 40.0f * Scale(); }

	// 왼쪽 메뉴 한 줄 (Home 등)
	bool NavItem(const char* Id, const char* Label, FIconFn Icon, bool bActive, float Width)
	{
		const float S = Scale();
		const ImVec2 Size(Width, 38.0f * S);
		const ImVec2 P = ImGui::GetCursorScreenPos();
		const bool bPressed = ImGui::InvisibleButton(Id, Size);
		const bool bHovered = ImGui::IsItemHovered();
		ImDrawList* DL = ImGui::GetWindowDrawList();
		if (bActive || bHovered)
			DL->AddRectFilled(P, Add(P, Size), bActive ? NavActive : NavHovered, 4.0f * S);
		const float IconSize = ImGui::GetFontSize() * 1.2f;
		const float MidY = P.y + Size.y * 0.5f;
		Icon(DL, ImVec2(P.x + 14.0f * S + IconSize * 0.5f, MidY), IconSize, bActive ? TextColor : MutedText);
		DL->AddText(ImVec2(P.x + 26.0f * S + IconSize, MidY - ImGui::GetFontSize() * 0.5f), bActive ? TextColor : IM_COL32(210, 210, 214, 255), Label);
		return bPressed;
	}

	void HeadingText(const char* Text, float Ratio = 1.85f)
	{
		ImGui::PushFont(nullptr, FontSize(Ratio));
		ImGui::PushStyleColor(ImGuiCol_Text, TextColor);
		ImGui::TextUnformatted(Text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	uint32 HashName(const FString& Text)
	{
		uint32 Hash = 2166136261u;
		for (const char C : Text)
			Hash = (Hash ^ static_cast<uint8>(C)) * 16777619u;
		return Hash;
	}

	// 프로젝트 썸네일 자리. 프로젝트마다 색이 다른 바닥 격자와 첫 글자를 그린다.
	void DrawThumbnail(ImDrawList* DL, ImVec2 Min, ImVec2 Max, const FString& Name)
	{
		const float Hue = (HashName(Name) % 360) / 360.0f;
		const ImU32 Top = ImColor::HSV(Hue, 0.30f, 0.42f);
		const ImU32 Bottom = ImColor::HSV(Hue, 0.22f, 0.20f);
		DL->AddRectFilledMultiColor(Min, Max, Top, Top, Bottom, Bottom);

		// 소실점으로 모이는 바닥 격자 (레벨 썸네일 느낌)
		const float W = Max.x - Min.x, H = Max.y - Min.y;
		const float Horizon = Min.y + H * 0.42f;
		const ImU32 Line = IM_COL32(255, 255, 255, 28);
		DL->PushClipRect(Min, Max, true);
		for (int32 Index = -6; Index <= 6; ++Index)
			DL->AddLine(ImVec2(Min.x + W * 0.5f, Horizon), ImVec2(Min.x + W * 0.5f + Index * W * 0.28f, Max.y), Line, 1.0f);
		for (int32 Row = 1; Row <= 5; ++Row)
		{
			const float T = Row / 5.0f;
			const float Y = Horizon + (Max.y - Horizon) * T * T;
			DL->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), Line, 1.0f);
		}
		DL->PopClipRect();

		const char Initial[2] = { Name.empty() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(Name[0]))), 0 };
		ImGui::PushFont(nullptr, FontSize(3.2f));
		const ImVec2 Size = ImGui::CalcTextSize(Initial);
		DL->AddText(ImVec2(Min.x + (W - Size.x) * 0.5f, Min.y + (H - Size.y) * 0.45f), IM_COL32(255, 255, 255, 200), Initial);
		ImGui::PopFont();
	}

	bool SamePath(const fs::path& A, const fs::path& B)
	{
		if (A.empty() || B.empty())
			return false;
		std::error_code Ec;
		if (fs::equivalent(A, B, Ec))
			return true;
		return _wcsicmp(A.lexically_normal().c_str(), B.lexically_normal().c_str()) == 0;
	}

	void ShowInExplorer(const fs::path& File)
	{
		const std::wstring Args = L"/select,\"" + fs::path(File).make_preferred().wstring() + L"\"";
		ShellExecuteW(nullptr, L"open", L"explorer.exe", Args.c_str(), nullptr, SW_SHOWNORMAL);
	}

	// IFileOpenDialog로 폴더나 .hproject 파일을 고른다. 취소하면 빈 경로.
	fs::path ShowOpenDialog(HWND Owner, bool bPickFolder, const fs::path& InitialDir)
	{
		ComPtr<IFileOpenDialog> Dialog;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))))
			return {};

		DWORD Options = 0;
		Dialog->GetOptions(&Options);
		Options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
		if (bPickFolder)
		{
			Options |= FOS_PICKFOLDERS;
			Dialog->SetTitle(L"Select Project Location");
		}
		else
		{
			Options |= FOS_FILEMUSTEXIST;
			const COMDLG_FILTERSPEC Filter[] = { { L"Hitori Project (*.hproject)", L"*.hproject" } };
			Dialog->SetFileTypes(1, Filter);
			Dialog->SetTitle(L"Open Project");
		}
		Dialog->SetOptions(Options);

		std::error_code Ec;
		if (!InitialDir.empty() && fs::is_directory(InitialDir, Ec))
		{
			ComPtr<IShellItem> Folder;
			if (SUCCEEDED(SHCreateItemFromParsingName(InitialDir.c_str(), nullptr, IID_PPV_ARGS(&Folder))))
				Dialog->SetFolder(Folder.Get());
		}

		if (FAILED(Dialog->Show(Owner))) return {};

		ComPtr<IShellItem> Result;
		if (FAILED(Dialog->GetResult(&Result))) return {};
		PWSTR Path = nullptr;
		if (FAILED(Result->GetDisplayName(SIGDN_FILESYSPATH, &Path))) return {};
		fs::path Out(Path);
		CoTaskMemFree(Path);
		return Out;
	}

	fs::path DefaultProjectLocation()
	{
		PWSTR Documents = nullptr;
		fs::path Result;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &Documents)))
			Result = fs::path(Documents) / L"HitoriProjects";
		CoTaskMemFree(Documents);
		return Result;
	}

	FString NextFreeProjectName(const fs::path& Location)
	{
		std::error_code Ec;
		for (int32 i = 1; i < 1000; ++i)
		{
			const FString Name = "MyProject" + std::to_string(i);
			if (!fs::exists(Location / FPaths::FromUtf8(Name), Ec)) return Name;
		}
		return "MyProject";
	}

	void CopyToBuffer(const FString& Text, char* Buffer, size_t Size)
	{
		strncpy_s(Buffer, Size, Text.c_str(), _TRUNCATE);
	}

	// 타일 아래 두 번째 줄: 지금 연 프로젝트면 "Current", 아니면 C++ / Blank
	FString ProjectKind(const fs::path& ProjectFile, const fs::path& CurrentProject)
	{
		if (SamePath(ProjectFile, CurrentProject))
			return "Current";
		std::error_code Ec;
		return fs::is_directory(ProjectFile.parent_path() / L"Source", Ec) ? "C++" : "Blank";
	}
}

ImTextureID FProjectBrowserView::GetThumbnail(const fs::path& ProjectFile)
{
	if (!Device)
		return ImTextureID_Invalid;
	const std::wstring Key = ProjectFile.lexically_normal().wstring();
	auto It = Thumbnails.find(Key);
	if (It == Thumbnails.end())
		It = Thumbnails.emplace(Key, ProjectThumbnail::LoadTexture(Device, ProjectThumbnail::Find(ProjectFile))).first;
	return It->second ? (ImTextureID)It->second.Get() : ImTextureID_Invalid;
}

void FProjectBrowserView::DrawProjectImage(ImDrawList* DL, ImVec2 Min, ImVec2 Max, const fs::path& ProjectFile)
{
	if (const ImTextureID Texture = GetThumbnail(ProjectFile); Texture != ImTextureID_Invalid)
		DL->AddImage(Texture, Min, Max);
	else
		DrawThumbnail(DL, Min, Max, FPaths::ToUtf8(ProjectFile.stem()));
}

void FProjectBrowserView::Init()
{
	// 다시 열 때마다 썸네일을 새로 읽는다 (그사이 레벨을 저장해 새로 찍혔을 수 있다).
	Thumbnails.clear();
	Recent = RecentProjects::Load();
	SelectedRecent = Recent.Num() > 0 ? 0 : -1;
	const fs::path Location = DefaultProjectLocation();
	CopyToBuffer(FPaths::ToUtf8(fs::path(Location).make_preferred()), LocationBuffer, sizeof(LocationBuffer));
	CopyToBuffer(NextFreeProjectName(Location), NameBuffer, sizeof(NameBuffer));
}

fs::path FProjectBrowserView::ConsumeChosen()
{
	fs::path Result = std::move(Chosen);
	Chosen.clear();
	return Result;
}

void FProjectBrowserView::TryOpen(const fs::path& ProjectFile)
{
	FProjectDescriptor Descriptor;
	FString LoadError;
	if (!Descriptor.Load(ProjectFile, &LoadError))
	{
		Error = LoadError;
		return;
	}
	Chosen = ProjectFile;
}

void FProjectBrowserView::BrowseForProject(HWND Owner)
{
	const fs::path File = ShowOpenDialog(Owner, false, {});
	if (!File.empty())
		TryOpen(File);
}

void FProjectBrowserView::RemoveRecent(int32 Index)
{
	if (Index < 0 || Index >= Recent.Num())
		return;
	RecentProjects::Remove(Recent[Index]);
	Recent = RecentProjects::Load();
	SelectedRecent = Recent.Num() > 0 ? 0 : -1;
}

void FProjectBrowserView::Draw(HWND Owner, ImTextureID Logo, const fs::path& CurrentProject)
{
	const float S = Scale();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	if (Avail.x < 1.0f || Avail.y < 1.0f)
		return;
	const float SidebarWidth = std::clamp(Avail.x * 0.22f, 250.0f * S, 340.0f * S);

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f * S, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, SidebarBg);
	if (ImGui::BeginChild("##BrowserSidebar", ImVec2(SidebarWidth, Avail.y), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		DrawSidebar(Owner, Logo, CurrentProject);
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::SameLine(0.0f, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(56.0f * S, 44.0f * S));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ContentBg);
	if (ImGui::BeginChild("##BrowserContent", ImVec2(0.0f, Avail.y), ImGuiChildFlags_AlwaysUseWindowPadding))
	{
		switch (Page)
		{
		case EPage::Home:       DrawHome(CurrentProject); break;
		case EPage::MyProjects: DrawMyProjects(Owner, CurrentProject); break;
		case EPage::NewProject: DrawNewProject(Owner); break;
		}
		DrawError();
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::PopStyleVar(2);
}

void FProjectBrowserView::DrawSidebar(HWND Owner, ImTextureID Logo, const fs::path& CurrentProject)
{
	const float S = Scale();
	const float Width = ImGui::GetContentRegionAvail().x;
	ImDrawList* DL = ImGui::GetWindowDrawList();

	// 로고와 엔진 이름
	ImGui::Dummy(ImVec2(0.0f, 40.0f * S));
	const float LogoSize = 104.0f * S;
	const float CenterX = ImGui::GetCursorScreenPos().x + Width * 0.5f;
	const ImVec2 LogoMin(CenterX - LogoSize * 0.5f, ImGui::GetCursorScreenPos().y);
	if (Logo != ImTextureID_Invalid)
	{
		DL->AddImage(Logo, LogoMin, Add(LogoMin, ImVec2(LogoSize, LogoSize)));
	}
	else
	{
		const ImVec2 Center(CenterX, LogoMin.y + LogoSize * 0.5f);
		DL->AddCircleFilled(Center, LogoSize * 0.5f, IM_COL32(240, 240, 240, 255));
		ImGui::PushFont(nullptr, FontSize(3.0f));
		const ImVec2 Size = ImGui::CalcTextSize("H");
		DL->AddText(ImVec2(Center.x - Size.x * 0.5f, Center.y - Size.y * 0.5f), IM_COL32(30, 30, 32, 255), "H");
		ImGui::PopFont();
	}
	ImGui::Dummy(ImVec2(Width, LogoSize + 14.0f * S));

	auto CenteredText = [&](const char* Text, float Ratio, ImU32 Color)
	{
		ImGui::PushFont(nullptr, FontSize(Ratio));
		const float TextWidth = ImGui::CalcTextSize(Text).x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (Width - TextWidth) * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Text, Color);
		ImGui::TextUnformatted(Text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	};
	CenteredText("HITORI", 1.9f, TextColor);
	CenteredText("ENGINE", 1.15f, IM_COL32(210, 210, 214, 255));

	ImGui::Dummy(ImVec2(0.0f, 30.0f * S));
	const ImVec2 ButtonSize(Width, 40.0f * S);
	if (FilledButton("##NewProject", "New Project", &IconPlusCircle, Accent, AccentHovered, AccentText, ButtonSize))
		Page = EPage::NewProject;
	ImGui::Dummy(ImVec2(0.0f, 4.0f * S));
	if (FilledButton("##MyProjects", "My Projects", &IconFolder, GreyButton, GreyHovered, TextColor, ButtonSize))
		Page = EPage::MyProjects;

	ImGui::Dummy(ImVec2(0.0f, 18.0f * S));
	if (NavItem("##NavHome", "Home", &IconHouse, Page == EPage::Home, Width))
		Page = EPage::Home;
	if (NavItem("##NavAll", "All Projects", &IconList, Page == EPage::MyProjects, Width))
		Page = EPage::MyProjects;
	if (NavItem("##NavOpen", "Open Project...", &IconFolder, false, Width))
		BrowseForProject(Owner);

	// 아래쪽: 엔진 폴더 (에디터에서는 지금 연 프로젝트도)
	const float FooterHeight = (CurrentProject.empty() ? 3.0f : 5.0f) * ImGui::GetTextLineHeightWithSpacing() + 20.0f * S;
	const float FooterY = ImGui::GetWindowHeight() - FooterHeight;
	if (ImGui::GetCursorPosY() < FooterY)
		ImGui::SetCursorPosY(FooterY);
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width);
	if (!CurrentProject.empty())
	{
		ImGui::TextColored(ImColor(MutedText), "Current Project");
		ImGui::TextUnformatted(FPaths::ToUtf8(CurrentProject.stem()).c_str());
	}
	ImGui::TextColored(ImColor(MutedText), "Engine");
	ImGui::PushFont(nullptr, FontSize(0.85f));
	ImGui::TextColored(ImColor(MutedText), "%s", FPaths::ToUtf8(fs::path(FPaths::EngineDir()).make_preferred()).c_str());
	ImGui::PopFont();
	ImGui::PopTextWrapPos();
}

void FProjectBrowserView::DrawProjectContextMenu(int32 Index)
{
	if (!ImGui::BeginPopupContextItem("##ProjectMenu"))
		return;
	const fs::path Project = Recent[Index];
	if (ImGui::MenuItem("Open"))
		TryOpen(Project);
	if (ImGui::MenuItem("Show in Explorer"))
		ShowInExplorer(Project);
	ImGui::Separator();
	if (ImGui::MenuItem("Remove from List"))
		RemoveRecent(Index);
	ImGui::EndPopup();
}

void FProjectBrowserView::DrawHome(const fs::path& CurrentProject)
{
	const float S = Scale();
	const float ContentWidth = ImGui::GetContentRegionAvail().x;

	// 제목 줄: "Recent Projects" ........ [See all projects]
	const float TitleY = ImGui::GetCursorPosY();
	HeadingText("Recent Projects");
	const char* SeeAll = "See all projects";
	const ImVec2 SeeAllSize(OutlineButtonWidth(SeeAll), 44.0f * S);
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + ContentWidth - SeeAllSize.x, TitleY - 4.0f * S));
	if (OutlineButton("##SeeAll", SeeAll, SeeAllSize))
		Page = EPage::MyProjects;
	ImGui::Dummy(ImVec2(0.0f, 20.0f * S));

	// 둥근 판 안에 타일
	const float Pad = 44.0f * S;
	const float TileWidth = 152.0f * S;
	const float ThumbHeight = TileWidth;
	const float BarHeight = 78.0f * S;
	const float TileHeight = ThumbHeight + BarHeight;
	const float Gap = 22.0f * S;
	const int32 Columns = (std::max)(1, static_cast<int32>((ContentWidth - Pad * 2.0f + Gap) / (TileWidth + Gap)));
	const int32 Shown = (std::min)(Recent.Num(), Columns * 2);
	const int32 Rows = Shown > 0 ? (Shown + Columns - 1) / Columns : 0;
	const float PanelHeight = Rows > 0 ? Pad * 2.0f + Rows * TileHeight + (Rows - 1) * Gap : 150.0f * S;

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, PanelBg);
	ImGui::PushStyleColor(ImGuiCol_Border, PanelBorder);
	if (ImGui::BeginChild("##RecentPanel", ImVec2(ContentWidth, PanelHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar))
	{
		if (Shown == 0)
		{
			ImGui::TextColored(ImColor(MutedText), "No recent projects yet.");
			ImGui::TextColored(ImColor(MutedText), "Create one with New Project, or open an existing .hproject with Open Project...");
		}

		ImDrawList* DL = ImGui::GetWindowDrawList();
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			const int32 Column = Index % Columns;
			const int32 Row = Index / Columns;
			const ImVec2 Min(Origin.x + Column * (TileWidth + Gap), Origin.y + Row * (TileHeight + Gap));
			const ImVec2 Max(Min.x + TileWidth, Min.y + TileHeight);
			ImGui::SetCursorScreenPos(Min);
			ImGui::PushID(Index);
			if (ImGui::InvisibleButton("##Tile", ImVec2(TileWidth, TileHeight)))
				SelectedRecent = Index;
			const bool bHovered = ImGui::IsItemHovered();
			if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				TryOpen(Recent[Index]);
			if (bHovered)
				ImGui::SetTooltip("%s", FPaths::ToUtf8(fs::path(Recent[Index]).make_preferred()).c_str());
			DrawProjectContextMenu(Index);

			const FString Name = FPaths::ToUtf8(Recent[Index].stem());
			const bool bSelected = SelectedRecent == Index;
			DrawProjectImage(DL, Min, ImVec2(Max.x, Min.y + ThumbHeight), Recent[Index]);
			const ImVec2 BarMin(Min.x, Min.y + ThumbHeight);
			DL->AddRectFilled(BarMin, Max, bSelected ? Selection : IM_COL32(46, 46, 50, 255));
			DL->PushClipRect(BarMin, Max, true);
			DL->AddText(ImVec2(BarMin.x + 6.0f * S, BarMin.y + 8.0f * S), TextColor, Name.c_str());
			// 이름이 같은 프로젝트도 구분되게 폴더 이름을 붙인다.
			const FString Kind = ProjectKind(Recent[Index], CurrentProject) + "  " + FPaths::ToUtf8(Recent[Index].parent_path().filename());
			DL->AddText(ImVec2(BarMin.x + 6.0f * S, Max.y - ImGui::GetFontSize() - 8.0f * S),
				bSelected ? TextColor : MutedText, Kind.c_str());
			DL->PopClipRect();
			if (bHovered || bSelected)
				DL->AddRect(Min, Max, bSelected ? Accent : IM_COL32(200, 200, 205, 160), 0.0f, 0, 2.0f * S);
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);

	if (Recent.Num() > Shown)
	{
		ImGui::Dummy(ImVec2(0.0f, 6.0f * S));
		ImGui::TextColored(ImColor(MutedText), "%d more in All Projects", Recent.Num() - Shown);
	}
}

void FProjectBrowserView::DrawMyProjects(HWND Owner, const fs::path& CurrentProject)
{
	const float S = Scale();
	const float ContentWidth = ImGui::GetContentRegionAvail().x;

	const float TitleY = ImGui::GetCursorPosY();
	HeadingText("My Projects");
	const char* Browse = "Open Project...";
	const ImVec2 BrowseSize(OutlineButtonWidth(Browse), 44.0f * S);
	ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + ContentWidth - BrowseSize.x, TitleY - 4.0f * S));
	if (OutlineButton("##BrowseProject", Browse, BrowseSize))
		BrowseForProject(Owner);
	ImGui::Dummy(ImVec2(0.0f, 16.0f * S));

	const float ButtonRow = 44.0f * S;
	const float ErrorRow = Error.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f;
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * S);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * S, 10.0f * S));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, PanelBg);
	ImGui::PushStyleColor(ImGuiCol_Border, PanelBorder);
	if (ImGui::BeginChild("##ProjectList", ImVec2(ContentWidth, -(ButtonRow + 16.0f * S + ErrorRow)),
		ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
	{
		if (Recent.IsEmpty())
			ImGui::TextColored(ImColor(MutedText), "No projects in the list. Use Open Project... or New Project.");

		ImDrawList* DL = ImGui::GetWindowDrawList();
		const float RowHeight = 64.0f * S;
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		for (int32 Index = 0; Index < Recent.Num(); ++Index)
		{
			ImGui::PushID(Index);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const ImVec2 Max(Min.x + RowWidth, Min.y + RowHeight);
			if (ImGui::InvisibleButton("##Row", ImVec2(RowWidth, RowHeight)))
				SelectedRecent = Index;
			const bool bHovered = ImGui::IsItemHovered();
			if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				TryOpen(Recent[Index]);
			DrawProjectContextMenu(Index);

			const bool bSelected = SelectedRecent == Index;
			if (bSelected || bHovered)
				DL->AddRectFilled(Min, Max, bSelected ? Selection : NavHovered, 6.0f * S);
			const float Thumb = RowHeight - 12.0f * S;
			const ImVec2 ThumbMin(Min.x + 6.0f * S, Min.y + 6.0f * S);
			const FString Name = FPaths::ToUtf8(Recent[Index].stem());
			DrawProjectImage(DL, ThumbMin, Add(ThumbMin, ImVec2(Thumb, Thumb)), Recent[Index]);

			const float TextX = ThumbMin.x + Thumb + 14.0f * S;
			ImGui::PushFont(nullptr, FontSize(1.15f));
			DL->AddText(ImVec2(TextX, Min.y + 10.0f * S), TextColor, Name.c_str());
			ImGui::PopFont();
			const FString Detail = ProjectKind(Recent[Index], CurrentProject) + "   " + FPaths::ToUtf8(fs::path(Recent[Index].parent_path()).make_preferred());
			DL->PushClipRect(ImVec2(TextX, Min.y), Max, true);
			DL->AddText(ImVec2(TextX, Max.y - ImGui::GetFontSize() - 10.0f * S), bSelected ? TextColor : MutedText, Detail.c_str());
			DL->PopClipRect();
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);

	ImGui::Dummy(ImVec2(0.0f, 8.0f * S));
	const bool bHasSelection = SelectedRecent >= 0 && SelectedRecent < Recent.Num();
	ImGui::BeginDisabled(!bHasSelection);
	const ImVec2 Small(150.0f * S, ButtonRow);
	if (FilledButton("##OpenSelected", "Open", nullptr, Accent, AccentHovered, AccentText, Small) && bHasSelection)
		TryOpen(Recent[SelectedRecent]);
	ImGui::SameLine(0.0f, 8.0f * S);
	if (FilledButton("##ShowSelected", "Show in Explorer", nullptr, GreyButton, GreyHovered, TextColor, ImVec2(180.0f * S, ButtonRow)) && bHasSelection)
		ShowInExplorer(Recent[SelectedRecent]);
	ImGui::SameLine(0.0f, 8.0f * S);
	if (FilledButton("##RemoveSelected", "Remove from List", nullptr, GreyButton, GreyHovered, TextColor, ImVec2(180.0f * S, ButtonRow)) && bHasSelection)
		RemoveRecent(SelectedRecent);
	ImGui::EndDisabled();
}

void FProjectBrowserView::DrawNewProject(HWND Owner)
{
	const float S = Scale();
	HeadingText("New Project");
	ImGui::TextColored(ImColor(MutedText), "Choose a template, then name the project and pick where to save it.");
	ImGui::Dummy(ImVec2(0.0f, 18.0f * S));

	// 템플릿 카드
	struct FTemplateCard { EProjectTemplate Value; const char* Title; const char* Badge; const char* Description; };
	const FTemplateCard Cards[] = {
		{ EProjectTemplate::Blank, "Blank", "BLANK", "Content only. Opens with the engine's HitoriEditor. Script with Blueprints and Lua." },
		{ EProjectTemplate::Cpp, "C++", "C++", "Adds Source/<Name> with a sample GameMode and a project solution to build <Name>Editor." },
	};
	const ImVec2 CardSize(260.0f * S, 190.0f * S);
	ImDrawList* DL = ImGui::GetWindowDrawList();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FTemplateCard& Card = Cards[Index];
		if (Index > 0)
			ImGui::SameLine(0.0f, 18.0f * S);
		ImGui::PushID(Index);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max = Add(Min, CardSize);
		if (ImGui::InvisibleButton("##Card", CardSize))
			Template = Card.Value;
		const bool bHovered = ImGui::IsItemHovered();
		const bool bSelected = Template == Card.Value;
		DL->AddRectFilled(Min, Max, PanelBg, 10.0f * S);
		// 위쪽 그림 칸
		const ImVec2 ArtMax(Max.x, Min.y + CardSize.y * 0.5f);
		DL->AddRectFilledMultiColor(Min, ArtMax, IM_COL32(52, 70, 92, 255), IM_COL32(52, 70, 92, 255), IM_COL32(32, 38, 48, 255), IM_COL32(32, 38, 48, 255));
		ImGui::PushFont(nullptr, FontSize(2.0f));
		const ImVec2 BadgeSize = ImGui::CalcTextSize(Card.Badge);
		DL->AddText(ImVec2(Min.x + (CardSize.x - BadgeSize.x) * 0.5f, Min.y + (ArtMax.y - Min.y - BadgeSize.y) * 0.5f), IM_COL32(255, 255, 255, 220), Card.Badge);
		ImGui::PopFont();
		ImGui::PushFont(nullptr, FontSize(1.15f));
		DL->AddText(ImVec2(Min.x + 14.0f * S, ArtMax.y + 10.0f * S), TextColor, Card.Title);
		ImGui::PopFont();
		DL->AddText(nullptr, 0.0f, ImVec2(Min.x + 14.0f * S, ArtMax.y + 38.0f * S), MutedText, Card.Description, nullptr, CardSize.x - 28.0f * S);
		DL->AddRect(Min, Max, bSelected ? Accent : (bHovered ? IM_COL32(200, 200, 205, 160) : PanelBorder), 10.0f * S, 0, (bSelected ? 2.5f : 1.0f) * S);
		ImGui::PopID();
	}

	ImGui::Dummy(ImVec2(0.0f, 26.0f * S));
	HeadingText("Project Defaults", 1.25f);
	ImGui::Dummy(ImVec2(0.0f, 6.0f * S));

	const float LabelWidth = 110.0f * S;
	const float FieldWidth = (std::min)(560.0f * S, ImGui::GetContentRegionAvail().x - LabelWidth);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Name");
	ImGui::SameLine(LabelWidth + ImGui::GetStyle().WindowPadding.x);
	ImGui::SetNextItemWidth(FieldWidth);
	ImGui::InputText("##Name", NameBuffer, sizeof(NameBuffer));

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Location");
	ImGui::SameLine(LabelWidth + ImGui::GetStyle().WindowPadding.x);
	const float BrowseWidth = ImGui::GetFrameHeight() * 1.6f;
	ImGui::SetNextItemWidth(FieldWidth - BrowseWidth - ImGui::GetStyle().ItemSpacing.x);
	ImGui::InputText("##Location", LocationBuffer, sizeof(LocationBuffer));
	ImGui::SameLine();
	if (ImGui::Button("...", ImVec2(BrowseWidth, 0.0f)))
	{
		const fs::path Folder = ShowOpenDialog(Owner, true, FPaths::FromUtf8(LocationBuffer));
		if (!Folder.empty())
			CopyToBuffer(FPaths::ToUtf8(Folder), LocationBuffer, sizeof(LocationBuffer));
	}

	const FString Name = NameBuffer;
	const fs::path Location = FPaths::FromUtf8(LocationBuffer);
	const FString NameError = ProjectCreator::ValidateName(Name, Template);
	ImGui::Dummy(ImVec2(0.0f, 4.0f * S));
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelWidth);
	if (NameError.empty())
	{
		fs::path Preview = Location / FPaths::FromUtf8(Name) / (FPaths::FromUtf8(Name).native() + FPaths::ProjectExtension);
		ImGui::TextColored(ImColor(MutedText), "%s", FPaths::ToUtf8(Preview.make_preferred()).c_str());
	}
	else
	{
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", NameError.c_str());
	}

	// 오른쪽 아래: [Cancel] [Create]
	ImGui::Dummy(ImVec2(0.0f, 24.0f * S));
	const ImVec2 ButtonSize(150.0f * S, 44.0f * S);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + LabelWidth + FieldWidth - ButtonSize.x * 2.0f - 8.0f * S);
	if (FilledButton("##CancelCreate", "Cancel", nullptr, GreyButton, GreyHovered, TextColor, ButtonSize))
		Page = EPage::Home;
	ImGui::SameLine(0.0f, 8.0f * S);
	const bool bCanCreate = NameError.empty() && !Location.empty();
	ImGui::BeginDisabled(!bCanCreate);
	if (FilledButton("##Create", "Create", &IconPlusCircle, Accent, AccentHovered, AccentText, ButtonSize) && bCanCreate)
	{
		std::error_code Ec;
		fs::create_directories(Location, Ec);
		fs::path ProjectFile;
		FString CreateError;
		if (ProjectCreator::Create(Name, Location, ProjectFile, CreateError, Template))
			Chosen = ProjectFile;
		else
			Error = CreateError;
	}
	ImGui::EndDisabled();
}

void FProjectBrowserView::DrawError()
{
	if (Error.empty())
		return;
	ImGui::Dummy(ImVec2(0.0f, 10.0f * Scale()));
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(ImColor(ErrorColor), "%s", Error.c_str());
	ImGui::PopTextWrapPos();
	if (ImGui::SmallButton("Dismiss"))
		Error.clear();
}
