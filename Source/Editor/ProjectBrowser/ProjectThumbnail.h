#pragma once

#include <filesystem>
#include <d3d11.h>
#include <wrl/client.h>

#include "Core/EngineString.h"

// 프로젝트 썸네일 (UE와 같은 규칙).
//   1. <프로젝트 폴더>/<프로젝트 이름>.png 가 있으면 그것 (직접 넣은 썸네일)
//   2. 없으면 <프로젝트 폴더>/Saved/AutoScreenshot.png (에디터가 레벨을 저장할 때 뷰포트를 찍어 둔다)
// 프로젝트 브라우저와 에디터 Home 탭이 이 파일을 읽어 타일에 그린다.
namespace ProjectThumbnail
{
	constexpr unsigned int Size = 256;   // 저장하는 썸네일 한 변 (정사각형)

	std::filesystem::path AutoScreenshotPath(const std::filesystem::path& ProjectDir);
	// 쓸 썸네일 파일. 둘 다 없으면 빈 경로.
	std::filesystem::path Find(const std::filesystem::path& ProjectFile);

	// GPU 텍스처(뷰포트 렌더 타깃)를 읽어 가운데를 정사각형으로 자르고 Size로 줄여 PNG로 쓴다.
	// 8비트 RGBA/BGRA와 16비트 float RGBA를 읽는다. 화면에 보이는 그대로(값을 0~1로 자름) 저장한다.
	bool Capture(ID3D11Device* Device, ID3D11DeviceContext* Context, ID3D11Resource* Texture,
		const std::filesystem::path& OutFile, FString& OutError);

	// PNG/JPG를 읽어 셰이더 리소스 뷰를 만든다. 실패하면 nullptr.
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> LoadTexture(ID3D11Device* Device, const std::filesystem::path& File);
}
