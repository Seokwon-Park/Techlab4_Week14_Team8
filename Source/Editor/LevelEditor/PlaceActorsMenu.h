#pragma once

#include "Editor/EditorUI/EditorIcons.h"

#include <functional>

class UClass;

// 레벨 에디터 툴바 Quick Add 메뉴에 놓을 수 있는 한 줄.
// MeshPath가 있으면 Static Mesh Actor를 놓고 그 메시를 넣는다 (Shapes의 Cube, Sphere ...).
struct FPlaceableActor
{
	FString Label;
	EEditorIcon Icon = EEditorIcon::ClassActor;
	UClass* Class = nullptr;
	FString MeshPath;
};

// UE5 레벨 에디터 툴바의 "Quickly add to the project" 메뉴 내용.
//   검색 칸 / GET CONTENT (Import Content..., Content Browser) / PLACE ACTORS (분류 하위 메뉴, All, Place Actors Panel 없음) / RECENT
// 엔진에 있는 클래스만 보인다. 빈 분류는 숨긴다.
class FPlaceActorsMenu
{
public:
	// 액터를 놓는다. 놓았으면 true (RECENT에 남긴다).
	using FPlaceFunction = std::function<bool(const FPlaceableActor&)>;

	void SetPlaceFunction(FPlaceFunction InPlace) { Place = std::move(InPlace); }
	void SetCanPlace(std::function<bool()> InCanPlace) { CanPlace = std::move(InCanPlace); }
	// Import Content 하위 메뉴. bSkeletal이면 Skeletal Mesh, 아니면 Static Mesh로 FBX를 가져온다.
	void SetImportContent(std::function<void(bool bSkeletal)> InImport) { ImportContent = std::move(InImport); }
	void SetOpenContentBrowser(std::function<void()> InOpen) { OpenContentBrowser = std::move(InOpen); }

	// 툴바 팝업 안에서 부른다.
	void Draw();

private:
	void PlaceItem(const FPlaceableActor& Item);
	// 아이콘 + 이름 한 줄. 눌리면 true.
	bool ItemRow(const FPlaceableActor& Item, bool bEnabled);
	void DrawSearchResults();

	FPlaceFunction Place;
	std::function<bool()> CanPlace;
	std::function<void(bool)> ImportContent;
	std::function<void()> OpenContentBrowser;

	char Search[64] = {};
	TArray<FPlaceableActor> Recent;   // 최근에 놓은 순서 (앞이 최신), 이번 실행 동안만 기억한다
};

// UE5 레벨 에디터 툴바의 Blueprints 메뉴 내용.
//   NEW: New Empty Blueprint Class... (Pick Parent Class 창) / OPEN BLUEPRINT CLASS: 검색 + 이미 있는 Blueprint 목록
class FBlueprintsToolbarMenu
{
public:
	void SetNewBlueprint(std::function<void()> InNew) { NewBlueprint = std::move(InNew); }
	// Blueprint 에셋 경로(.bp)를 Blueprint 에디터로 연다.
	void SetOpenBlueprint(std::function<void(const FString&)> InOpen) { OpenBlueprint = std::move(InOpen); }

	void Draw();

private:
	std::function<void()> NewBlueprint;
	std::function<void(const FString&)> OpenBlueprint;
	char Search[64] = {};
};
