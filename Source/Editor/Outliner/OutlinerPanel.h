#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <functional>
#include <map>
#include <set>
#include <vector>

class UWorld;
class ULevel;
class AActor;

using DeleteActorCallback = std::function<void(AActor*)>;

// UE5 Outliner. 맨 위 월드 줄 아래에 폴더와 액터를 나무로 보여 준다.
// 폴더는 오브젝트가 아니라 경로다. 액터는 자기 폴더 경로(AActor::FolderPath)를 갖고,
// 빈 폴더도 남도록 레벨이 폴더 목록(ULevel::EditorFolders)을 갖는다. 둘 다 씬 파일과 실행 취소 기록에 들어간다.
class FOutlinerPanel : public IEditorPanel
{
public:
    FOutlinerPanel() = default;
    ~FOutlinerPanel() = default;

    void SetDeleteActorCallback(DeleteActorCallback InCallback) { DeleteCallback = InCallback; }
    // 빈 곳 오른쪽 클릭 메뉴의 World Settings가 부른다.
    void SetOpenWorldSettingsCallback(std::function<void()> InCallback) { OpenWorldSettingsCallback = std::move(InCallback); }

    bool Init() override;
    void Tick(float DeltaTime) override;
    void OnRender() override;
    const char* GetPanelName() const override { return "Outliner"; }

    void SetWorld(UWorld* InWorld) { World = InWorld; };

private:
    enum class ERowType : uint8 { World, Folder, Actor };

    // 이번 프레임 표에 그릴 한 줄. 펼친 폴더 안쪽만 들어간다.
    struct FRow
    {
        ERowType Type = ERowType::Actor;
        int32 Depth = 0;
        FString Folder;            // Folder 줄이면 그 폴더 경로
        AActor* Actor = nullptr;   // Actor 줄이면 그 액터
        bool bHasChildren = false;
    };

    // 폴더 목록 = 레벨에 적힌 폴더 + 액터가 들어 있는 폴더와 그 부모들
    void GatherFolders(ULevel* Level, std::set<FString>& OutFolders) const;
    void BuildRows(ULevel* Level);

    void DrawToolbar(ULevel* Level);
    void DrawTable(ULevel* Level);
    void DrawRow(const FRow& Row, ULevel* Level);
    // 액터 줄을 왼쪽 클릭했을 때. Ctrl은 넣고 빼기, Shift는 기준 줄부터 이 줄까지 고른다.
    void HandleActorRowClick(AActor* Actor);
    void DrawRowLabel(const FRow& Row, float RowHeight, bool bDimmed);

    // 오른쪽 클릭 메뉴. 액터 줄, 폴더 줄, 빈 곳(과 월드 줄)이 각자 다르다.
    void DrawActorContextMenu(AActor* Actor, ULevel* Level);
    void DrawFolderContextMenu(const FString& Folder, ULevel* Level);
    void DrawBackgroundContextMenu(ULevel* Level);
    // "Move To" 하위 메뉴. 액터나 폴더 하나를 다른 폴더(또는 맨 위)로 옮긴다.
    void DrawMoveToMenu(ULevel* Level, AActor* MovingActor, const FString* MovingFolder);

    // 액터나 폴더를 이 줄 위에 놓으면 그 폴더로 옮긴다. TargetFolder가 비면 맨 위다.
    void AcceptDrop(ULevel* Level, const FString& TargetFolder);
    // 액터를 다른 액터 줄 위에 놓으면 그 액터에 붙인다 (UE5 Outliner에서 계층을 바꾸는 방법).
    void AcceptDropOnActor(AActor* TargetActor);
    // "Attach To" 하위 메뉴. 고를 수 있는 다른 액터를 늘어놓는다.
    void DrawAttachToMenu(ULevel* Level, AActor* Actor);

    // 에디터 월드일 때만 폴더와 액터를 고친다. PIE 월드는 끝나면 사라진다.
    bool IsEditable() const;

    // ---------------- 폴더 편집 ----------------
    // Parent 아래에 겹치지 않는 이름("NewFolder", "NewFolder1", ...)으로 폴더를 만들고 그 경로를 돌려준다.
    FString CreateFolder(ULevel* Level, const FString& Parent);
    // 선택한 액터가 있으면 그 액터가 있던 자리에 새 폴더를 만들고 액터를 넣는다 (UE의 Create Folder 버튼).
    void CreateFolderForSelection(ULevel* Level);
    bool RenameFolder(ULevel* Level, const FString& Folder, const FString& NewLeafName);
    // 폴더를 지우고 안의 액터와 폴더는 한 단계 위로 올린다.
    void DeleteFolder(ULevel* Level, const FString& Folder);
    void MoveFolder(ULevel* Level, const FString& Folder, const FString& NewParent);
    void MoveActorToFolder(ULevel* Level, AActor* Actor, const FString& Folder);
    // 액터가 하나도 없는 폴더를 지운다.
    void CleanupFolders(ULevel* Level);
    // 경로가 OldPrefix이거나 그 아래인 액터와 폴더를 NewPrefix 아래로 옮긴다.
    void RemapFolderPrefix(ULevel* Level, const FString& OldPrefix, const FString& NewPrefix);

    // 이름 바꾸기. 액터와 폴더 중 하나만 바꾼다.
    void BeginRenameActor(AActor* Actor);
    void BeginRenameFolder(const FString& Folder);
    void CancelRename();

    // 표를 다 그린 뒤 실행한다. 그리는 도중에 액터나 폴더를 바꾸면 이번 프레임의 줄 목록이 어긋난다.
    void Defer(std::function<void()> Op) { PendingOps.push_back(std::move(Op)); }

    UWorld* World = nullptr;
    DeleteActorCallback DeleteCallback;
    std::function<void()> OpenWorldSettingsCallback;

    char SearchBuffer[128] = {};

    // 이번 프레임의 줄과 폴더별 내용
    std::vector<FRow> Rows;
    // Shift+클릭 범위의 기준 줄 (마지막으로 그냥 클릭하거나 Ctrl+클릭한 액터)
    AActor* SelectionAnchor = nullptr;
    std::set<FString> Folders;
    std::map<FString, std::vector<FString>> ChildFolders;
    std::map<FString, std::vector<AActor*>> ActorsInFolder;
    // 다른 액터에 붙은 액터는 폴더 대신 부모 액터 아래에 보인다 (UE5와 같다).
    std::map<const AActor*, std::vector<AActor*>> AttachedChildren;

    // 접은 폴더. 기본은 펼침이다.
    std::set<FString> CollapsedFolders;
    // 접은 부모 액터. 포인터는 비교에만 쓴다.
    std::set<const AActor*> CollapsedActors;
    bool bWorldCollapsed = false;

    // "Attach To" 메뉴의 검색 칸. 액터가 많아도 고를 수 있게 한다.
    char AttachSearch[64] = {};

    // 폴더는 엔진 선택에 들어가지 않으므로 Outliner가 기억한다.
    bool bFolderSelected = false;
    FString SelectedFolder;

    AActor* RenamingActor = nullptr;
    bool bRenamingFolder = false;
    FString RenamingFolder;
    bool bRenameDrawnThisFrame = false;
    bool bFocusRename = false;
    char RenameBuffer[256] = {};

    std::vector<std::function<void()>> PendingOps;
};
