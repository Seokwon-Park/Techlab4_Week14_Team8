#include "EnginePCH.h"
#include "Editor/HitoriEd/ActorClipboard.h"

#include <cstring>

#include "imgui.h"
#include "Core/EngineLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectHash.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"

namespace
{
	// 클립보드에 아무 JSON이나 들어 있을 수 있으므로 형식 이름과 버전으로 우리 것인지 가린다.
	constexpr const char* ClipboardFormat = "HitoriActorClipboard";
	constexpr int32 ClipboardVersion = 1;
}

bool FActorClipboard::ExportActor(AActor* Actor, FString& OutText)
{
	if (!Actor)
		return false;

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer.SetIsPersistent(true);   // 씬 파일처럼 Transient는 쓰지 않는다
	{
		// 선언 순서 = 소멸 역순: Ar(Close) → Formatter(dump → Writer)
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);

		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		FString Format = ClipboardFormat;
		int32 Version = ClipboardVersion;
		FString ClassName = Actor->GetClass()->Name;
		Root << SA_VALUE("Format", Format) << SA_VALUE("Version", Version) << SA_VALUE("Class", ClassName);
		// 레벨 저장과 같은 방식으로 액터와 컴포넌트를 쓴다.
		Actor->Serialize(Root.EnterRecord("Actor"));

		Ar.Close();
	}
	if (Writer.IsError())
		return false;

	OutText.assign(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	return true;
}

bool FActorClipboard::LooksLikeActorText(const char* Text)
{
	return Text && std::strstr(Text, ClipboardFormat) != nullptr;
}

AActor* FActorClipboard::ImportActor(UWorld* World, const FString& Text)
{
	if (!World || !LooksLikeActorText(Text.c_str()))
		return nullptr;

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Text.size()));
	std::memcpy(Bytes.GetData(), Text.data(), Text.size());

	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		HTR_LOG(Warning, "Paste: clipboard does not hold a valid actor");
		return nullptr;
	}

	FStructuredArchive Ar(Formatter);
	FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();

	FString Format, ClassName;
	int32 Version = 0;
	Root << SA_VALUE("Format", Format) << SA_VALUE("Version", Version) << SA_VALUE("Class", ClassName);
	if (Format != ClipboardFormat || Version != ClipboardVersion)
	{
		HTR_LOG(Warning, "Paste: unsupported clipboard data ({} v{})", Format, Version);
		return nullptr;
	}

	UClass* Class = FindClass(ClassName);
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		HTR_LOG(Warning, "Paste: unknown actor class '{}'", ClassName);
		return nullptr;
	}

	// 이름은 비워 두어 원본과 겹치지 않는 새 이름을 받는다. 레벨 로드와 같이 생성한 뒤 읽어 들인다.
	AActor* Actor = World->SpawnActor(Class);
	if (!Actor)
		return nullptr;
	Actor->Serialize(Root.EnterRecord("Actor"));
	Ar.Close();

	if (Reader.IsError())
	{
		HTR_LOG(Warning, "Paste: clipboard actor data is broken");
		Actor->Destroy();
		return nullptr;
	}

	// 원본이 다른 액터에 붙어 있었으면 같은 부모에 붙인다. 부모 기준 Transform이라 원본과 같은 자리에 놓인다.
	Actor->ResolvePendingActorAttachments();

	// 레벨 로드처럼 피킹과 컬링에 쓰는 BVH를 다시 만든다.
	World->GetScene().BuildBVH();
	return Actor;
}

bool FActorClipboard::CopyToClipboard(AActor* Actor)
{
	FString Text;
	if (!ExportActor(Actor, Text))
		return false;
	ImGui::SetClipboardText(Text.c_str());
	return true;
}

AActor* FActorClipboard::PasteFromClipboard(UWorld* World)
{
	const char* Text = ImGui::GetClipboardText();
	if (!LooksLikeActorText(Text))
		return nullptr;
	return ImportActor(World, FString(Text));
}

AActor* FActorClipboard::Duplicate(AActor* Actor, UWorld* World)
{
	FString Text;
	if (!ExportActor(Actor, Text))
		return nullptr;
	return ImportActor(World, Text);
}
