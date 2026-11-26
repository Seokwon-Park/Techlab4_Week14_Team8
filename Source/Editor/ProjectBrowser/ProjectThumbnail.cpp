#include "EnginePCH.h"
#include "Editor/ProjectBrowser/ProjectThumbnail.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <wincodec.h>
#include <DirectXPackedVector.h>

#include "Core/Misc/Paths.h"
#include "Render/ImageLoader.h"

#pragma comment(lib, "windowscodecs.lib")

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace
{
	float HalfToFloat(uint16 Value)
	{
		return DirectX::PackedVector::XMConvertHalfToFloat(Value);
	}

	uint8 ToByte(float Value)
	{
		return static_cast<uint8>(std::clamp(Value, 0.0f, 1.0f) * 255.0f + 0.5f);
	}

	// 매핑한 한 줄에서 픽셀 하나를 RGBA8로 읽는다. 모르는 형식이면 false.
	bool ReadPixel(DXGI_FORMAT Format, const uint8* Row, uint32 X, uint8 Out[4])
	{
		switch (Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			std::copy_n(Row + X * 4, 4, Out);
			return true;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			Out[0] = Row[X * 4 + 2];
			Out[1] = Row[X * 4 + 1];
			Out[2] = Row[X * 4 + 0];
			Out[3] = 255;
			return true;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		{
			const uint16* Pixel = reinterpret_cast<const uint16*>(Row) + X * 4;
			for (int32 Channel = 0; Channel < 3; ++Channel)
				Out[Channel] = ToByte(HalfToFloat(Pixel[Channel]));
			Out[3] = 255;
			return true;
		}
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		{
			const uint32 Pixel = reinterpret_cast<const uint32*>(Row)[X];
			Out[0] = ToByte((Pixel & 0x3FF) / 1023.0f);
			Out[1] = ToByte(((Pixel >> 10) & 0x3FF) / 1023.0f);
			Out[2] = ToByte(((Pixel >> 20) & 0x3FF) / 1023.0f);
			Out[3] = 255;
			return true;
		}
		default:
			return false;
		}
	}

	// RGBA8 픽셀을 WIC로 PNG에 쓴다 (새 라이브러리 없이 Windows 기본 기능만 쓴다).
	bool WritePng(const fs::path& File, uint32 Width, uint32 Height, const std::vector<uint8>& Rgba, FString& OutError)
	{
		const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		bool bOk = false;
		{
			ComPtr<IWICImagingFactory> Factory;
			ComPtr<IWICBitmap> Bitmap;
			ComPtr<IWICStream> Stream;
			ComPtr<IWICBitmapEncoder> Encoder;
			ComPtr<IWICBitmapFrameEncode> Frame;
			// PNG 인코더가 받는 픽셀 형식은 Windows 버전마다 다르다 (RGBA를 거절하면 파일만 생기고 0바이트로 남는다).
			// 픽셀을 RGBA 비트맵으로 감싸 WriteSource로 넘기면 WIC가 인코더가 원하는 형식으로 바꿔 준다.
			WICPixelFormatGUID Format = GUID_WICPixelFormat32bppBGRA;
			HRESULT Hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory));
			if (FAILED(Hr))
				OutError = "WIC is not available";
			else if (FAILED(Hr = Factory->CreateBitmapFromMemory(Width, Height, GUID_WICPixelFormat32bppRGBA, Width * 4,
				static_cast<UINT>(Rgba.size()), const_cast<BYTE*>(Rgba.data()), &Bitmap)))
				OutError = "cannot wrap the pixels";
			else if (FAILED(Hr = Factory->CreateStream(&Stream)) || FAILED(Hr = Stream->InitializeFromFilename(File.c_str(), GENERIC_WRITE)))
				OutError = "cannot write " + FPaths::ToUtf8(File);
			else if (FAILED(Hr = Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder)) ||
				FAILED(Hr = Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache)) ||
				FAILED(Hr = Encoder->CreateNewFrame(&Frame, nullptr)) || FAILED(Hr = Frame->Initialize(nullptr)) ||
				FAILED(Hr = Frame->SetSize(Width, Height)) || FAILED(Hr = Frame->SetPixelFormat(&Format)))
				OutError = "cannot create the PNG encoder";
			else if (FAILED(Hr = Frame->WriteSource(Bitmap.Get(), nullptr)) || FAILED(Hr = Frame->Commit()) || FAILED(Hr = Encoder->Commit()))
				OutError = "failed to encode the PNG";
			else
				bOk = true;
			if (!bOk)
			{
				char Code[16];
				std::snprintf(Code, sizeof(Code), "%08lX", static_cast<unsigned long>(Hr));
				OutError += " (HRESULT 0x" + FString(Code) + ")";
			}
		}
		if (SUCCEEDED(Init))
			CoUninitialize();
		// 실패하면 반쯤 쓴 파일을 지운다. 0바이트 파일이 남으면 브라우저가 깨진 썸네일을 읽으려 한다.
		if (!bOk)
		{
			std::error_code Ec;
			fs::remove(File, Ec);
		}
		return bOk;
	}
}

fs::path ProjectThumbnail::AutoScreenshotPath(const fs::path& ProjectDir)
{
	return ProjectDir / L"Saved" / L"AutoScreenshot.png";
}

fs::path ProjectThumbnail::Find(const fs::path& ProjectFile)
{
	// 빈 파일(쓰다 실패한 것)은 없는 것으로 본다.
	const auto IsUsable = [](const fs::path& Path)
	{
		std::error_code Ec;
		return fs::is_regular_file(Path, Ec) && fs::file_size(Path, Ec) > 0 && !Ec;
	};
	const fs::path Dir = ProjectFile.parent_path();
	const fs::path Custom = Dir / (ProjectFile.stem().native() + L".png");
	if (IsUsable(Custom))
		return Custom;
	const fs::path Auto = AutoScreenshotPath(Dir);
	if (IsUsable(Auto))
		return Auto;
	return {};
}

bool ProjectThumbnail::Capture(ID3D11Device* Device, ID3D11DeviceContext* Context, ID3D11Resource* Texture,
	const fs::path& OutFile, FString& OutError)
{
	ComPtr<ID3D11Texture2D> Source;
	if (!Device || !Context || !Texture || FAILED(Texture->QueryInterface(IID_PPV_ARGS(&Source))))
	{
		OutError = "no viewport image";
		return false;
	}
	D3D11_TEXTURE2D_DESC Desc{};
	Source->GetDesc(&Desc);
	if (Desc.Width == 0 || Desc.Height == 0)
	{
		OutError = "viewport is empty";
		return false;
	}

	// GPU 전용 텍스처는 바로 읽을 수 없어 CPU가 읽을 수 있는 복사본(staging)을 만든다.
	// 멀티샘플이면 먼저 한 장으로 합친다.
	ComPtr<ID3D11Texture2D> Resolved = Source;
	if (Desc.SampleDesc.Count > 1)
	{
		D3D11_TEXTURE2D_DESC ResolveDesc = Desc;
		ResolveDesc.SampleDesc = { 1, 0 };
		ResolveDesc.BindFlags = 0;
		ResolveDesc.MiscFlags = 0;
		if (FAILED(Device->CreateTexture2D(&ResolveDesc, nullptr, &Resolved)))
		{
			OutError = "cannot resolve the viewport image";
			return false;
		}
		Context->ResolveSubresource(Resolved.Get(), 0, Source.Get(), 0, Desc.Format);
	}

	D3D11_TEXTURE2D_DESC StagingDesc = Desc;
	StagingDesc.SampleDesc = { 1, 0 };
	StagingDesc.Usage = D3D11_USAGE_STAGING;
	StagingDesc.BindFlags = 0;
	StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	StagingDesc.MiscFlags = 0;
	StagingDesc.MipLevels = 1;
	StagingDesc.ArraySize = 1;
	ComPtr<ID3D11Texture2D> Staging;
	if (FAILED(Device->CreateTexture2D(&StagingDesc, nullptr, &Staging)))
	{
		OutError = "cannot create a readback texture";
		return false;
	}
	Context->CopySubresourceRegion(Staging.Get(), 0, 0, 0, 0, Resolved.Get(), 0, nullptr);

	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped)))
	{
		OutError = "cannot read the viewport image";
		return false;
	}

	// 가운데 정사각형을 Size x Size로 줄인다. 한 칸에 들어가는 원본 픽셀들의 평균을 낸다 (계단 현상 줄이기).
	const uint32 Side = (std::min)(Desc.Width, Desc.Height);
	const uint32 OffsetX = (Desc.Width - Side) / 2;
	const uint32 OffsetY = (Desc.Height - Side) / 2;
	const uint32 OutSize = (std::min)(Size, Side);
	std::vector<uint8> Rgba(static_cast<size_t>(OutSize) * OutSize * 4);
	bool bFormatKnown = true;
	for (uint32 Y = 0; Y < OutSize && bFormatKnown; ++Y)
	{
		const uint32 SrcY0 = OffsetY + Y * Side / OutSize;
		const uint32 SrcY1 = (std::max)(SrcY0 + 1, OffsetY + (Y + 1) * Side / OutSize);
		for (uint32 X = 0; X < OutSize && bFormatKnown; ++X)
		{
			const uint32 SrcX0 = OffsetX + X * Side / OutSize;
			const uint32 SrcX1 = (std::max)(SrcX0 + 1, OffsetX + (X + 1) * Side / OutSize);
			uint32 Sum[3] = {};
			uint32 Count = 0;
			for (uint32 SY = SrcY0; SY < SrcY1; SY += 2)   // 2칸씩 건너 표본을 줄인다 (썸네일에는 충분하다)
			{
				const uint8* Row = static_cast<const uint8*>(Mapped.pData) + static_cast<size_t>(SY) * Mapped.RowPitch;
				for (uint32 SX = SrcX0; SX < SrcX1; SX += 2)
				{
					uint8 Pixel[4];
					if (!ReadPixel(Desc.Format, Row, SX, Pixel))
					{
						bFormatKnown = false;
						break;
					}
					Sum[0] += Pixel[0];
					Sum[1] += Pixel[1];
					Sum[2] += Pixel[2];
					++Count;
				}
			}
			uint8* Out = &Rgba[(static_cast<size_t>(Y) * OutSize + X) * 4];
			for (int32 Channel = 0; Channel < 3; ++Channel)
				Out[Channel] = static_cast<uint8>(Count ? Sum[Channel] / Count : 0);
			Out[3] = 255;
		}
	}
	Context->Unmap(Staging.Get(), 0);
	if (!bFormatKnown)
	{
		OutError = "unsupported viewport format " + std::to_string(static_cast<int32>(Desc.Format));
		return false;
	}

	std::error_code Ec;
	fs::create_directories(OutFile.parent_path(), Ec);
	return WritePng(OutFile, OutSize, OutSize, Rgba, OutError);
}

ComPtr<ID3D11ShaderResourceView> ProjectThumbnail::LoadTexture(ID3D11Device* Device, const fs::path& File)
{
	if (!Device || File.empty())
		return nullptr;
	const FImageData Image = ImageLoader::Load(FPaths::ToUtf8(File));
	if (!Image.IsValid())
		return nullptr;

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Image.Width;
	Desc.Height = Image.Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = Image.Format != DXGI_FORMAT_UNKNOWN ? Image.Format : DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA Data{};
	Data.pSysMem = Image.Pixels.GetData();
	Data.SysMemPitch = Image.GetRowPitch();

	ComPtr<ID3D11Texture2D> Texture;
	ComPtr<ID3D11ShaderResourceView> View;
	if (FAILED(Device->CreateTexture2D(&Desc, &Data, &Texture)))
		return nullptr;
	if (FAILED(Device->CreateShaderResourceView(Texture.Get(), nullptr, &View)))
		return nullptr;
	return View;
}
