#pragma once

// 에디터 패널 간 드래그앤드롭 페이로드 식별자.
// ImGui는 이 문자열로 소스와 타깃을 짝지으므로 양쪽이 같은 값을 써야 한다.
// (ImGui 제한: 널 포함 32자 이내)
namespace EditorDragDrop
{
	// 페이로드 내용: UTexture2D* 한 개
	inline constexpr const char* Texture = "ASSET_TEXTURE";

	// 페이로드 내용: UFont* 한 개
	inline constexpr const char* Font = "ASSET_FONT";

	inline constexpr const char* Component = "COMPONENT";

	// 페이로드 내용: 널로 끝나는 UTF-8 에셋 경로 ("Content/Blueprints/BP_Door.bp")
	inline constexpr const char* Blueprint = "ASSET_BLUEPRINT";

	// 콘텐츠 드로어 안에서 파일/폴더를 다른 폴더로 옮길 때. 옮길 목록은 드로어가 들고 있고 페이로드는 비어 있다.
	inline constexpr const char* ContentMove = "CONTENT_MOVE";
}
