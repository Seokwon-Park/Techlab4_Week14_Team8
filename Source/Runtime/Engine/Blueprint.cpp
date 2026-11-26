#include "EnginePCH.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGraph.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstring>
#include <unordered_map>

#include "Core/EngineLog.h"
#include "Core/Misc/Paths.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "UObject/Class.h"
#include "UObject/UObjectHash.h"
#include "Json/Dom/JsonObject.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"

namespace
{
	struct FBlueprintData
	{
		FString AssetPath;        // Assets 기준 경로 ("Blueprints/BP_Door.bp")
		FString ParentClassName;
		TArray<uint8> FileBytes;  // 기본값을 적용할 때마다 다시 읽는다
		// "Graphs" (EventGraph, Construction Script). 없으면 nullptr.
		// 실행 중인 Delay가 옛 그래프를 붙잡고 있을 수 있어 shared_ptr로 둔다.
		std::shared_ptr<const FBPGraphSet> Graphs;
	};

	std::shared_ptr<const FBPGraphSet> ReadGraphs(const TArray<uint8>& Bytes)
	{
		const FJsonObject Root = FJsonObject::parse(Bytes.begin(), Bytes.end(), nullptr, false);
		if (Root.is_discarded() || !Root.is_object() || !Root.contains("Graphs") || !Root["Graphs"].is_object())
			return nullptr;
		auto Graphs = std::make_shared<FBPGraphSet>();
		if (!FBlueprintGraphSerializer::FromJson(Root["Graphs"].dump(), *Graphs))
			return nullptr;
		return Graphs;
	}

	// BP 클래스 -> 데이터. UClass는 등록된 뒤 지우지 않으므로 포인터를 키로 쓴다.
	std::unordered_map<const UClass*, FBlueprintData>& GetRegistry()
	{
		static std::unordered_map<const UClass*, FBlueprintData> Registry;
		return Registry;
	}

	const FBlueprintData* FindData(const UClass* Class)
	{
		auto It = GetRegistry().find(Class);
		return It != GetRegistry().end() ? &It->second : nullptr;
	}

	FString ToAssetPath(const std::filesystem::path& File)
	{
		std::error_code Ec;
		const std::filesystem::path Relative = std::filesystem::relative(File, "Assets", Ec);
		if (!Ec && !Relative.empty() && Relative.native().find(L"..") != 0)
			return FPaths::ToUtf8(Relative.generic_wstring());
		return FPaths::ToUtf8(File.generic_wstring());
	}

	bool ReadFileBytes(const std::filesystem::path& File, TArray<uint8>& OutBytes)
	{
		std::ifstream Stream(File, std::ios::binary | std::ios::ate);
		if (!Stream)
			return false;
		const std::streamoff Size = Stream.tellg();
		if (Size <= 0 || Size > 64ll * 1024 * 1024)
			return false;
		OutBytes.SetNum(static_cast<int32>(Size));
		Stream.seekg(0);
		Stream.read(reinterpret_cast<char*>(OutBytes.GetData()), Size);
		return static_cast<bool>(Stream);
	}

	// .bp의 머리(버전, 부모 클래스 이름)만 읽는다.
	bool ReadHeader(const TArray<uint8>& Bytes, FString& OutParentClass, FString& OutError)
	{
		FMemoryReader Reader(Bytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError())
		{
			OutError = "not valid JSON";
			return false;
		}
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		int32 Version = 0;
		Root << SA_VALUE("Version", Version) << SA_VALUE("ParentClass", OutParentClass);
		Ar.Close();
		if (Version != FBlueprint::FileVersion)
		{
			OutError = "unsupported version " + std::to_string(Version);
			return false;
		}
		if (OutParentClass.empty())
		{
			OutError = "ParentClass is empty";
			return false;
		}
		return true;
	}

	// Data의 "Defaults"를 Object에 읽어 들인다. bFull이면 객체의 Serialize 전체(액터는 컴포넌트까지,
	// 컴포넌트는 머티리얼 같은 추가 데이터까지), 아니면 프로퍼티만 읽는다.
	void ApplyDefaultsRecord(const FBlueprintData& Data, UObject* Object, bool bFull)
	{
		FMemoryReader Reader(Data.FileBytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError())
			return;
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		FStructuredArchive::FRecord Defaults = Root.EnterRecord("Defaults");
		if (bFull)
			Object->Serialize(Defaults);   // AActor::Serialize / UActorComponent 계열의 Serialize
		else
			Object->UObject::Serialize(Defaults);   // 프로퍼티만. CDO에는 컴포넌트를 만들지 않는다
		Ar.Close();
		if (Reader.IsError())
			HTR_LOG(Warning, "Blueprint: defaults of {} could not be read", Data.AssetPath);
	}

	// Class부터 위로 올라가며 BP 클래스를 모아, 가장 위(부모 쪽) BP부터 적용한다.
	void ApplyChain(UClass* Class, UObject* Object, bool bFull)
	{
		TArray<const FBlueprintData*> Chain;
		for (UClass* It = Class; It; It = It->Super)
			if (const FBlueprintData* Data = FindData(It))
				Chain.Add(Data);
		for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
			ApplyDefaultsRecord(*Chain[Index], Object, bFull);
	}

	// CDO를 만들 때 UClass::CreateDefaultObject가 부른다. 프로퍼티 기본값만 덮어쓴다.
	void ApplyClassDefaultObjectDefaults(UObject* DefaultObject)
	{
		if (DefaultObject)
			ApplyChain(DefaultObject->GetClass(), DefaultObject, false);
	}

	// ---------------- 인스턴스 차이 (JSON) ----------------

	// Actor를 AActor::Serialize 형식의 JSON 객체로 만든다 (씬 파일의 액터 하나, BP의 Defaults와 같은 모양).
	bool ActorToJson(AActor* Actor, FJsonObject& OutJson)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Writer.SetIsPersistent(true);
		{
			FJsonArchiveOutputFormatter Formatter(Writer);
			FStructuredArchive Ar(Formatter);
			FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
			Actor->Serialize(Root);
			Ar.Close();
		}
		if (Writer.IsError())
			return false;
		OutJson = FJsonObject::parse(Bytes.begin(), Bytes.end(), nullptr, false);
		return !OutJson.is_discarded() && OutJson.is_object();
	}

	// AActor::Serialize 형식의 JSON을 Actor에 읽어 들인다.
	bool JsonToActor(AActor* Actor, const FJsonObject& Json)
	{
		const std::string Text = Json.dump();
		TArray<uint8> Bytes;
		Bytes.SetNum(static_cast<int32>(Text.size()));
		std::memcpy(Bytes.GetData(), Text.data(), Text.size());

		FMemoryReader Reader(Bytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError())
			return false;
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		Actor->Serialize(Root);
		Ar.Close();
		return !Reader.IsError();
	}

	// BP 파일의 Defaults 객체
	bool DefaultsToJson(const FBlueprintData& Data, FJsonObject& OutJson)
	{
		const FJsonObject Root = FJsonObject::parse(Data.FileBytes.begin(), Data.FileBytes.end(), nullptr, false);
		if (Root.is_discarded() || !Root.is_object() || !Root.contains("Defaults") || !Root["Defaults"].is_object())
			return false;
		OutJson = Root["Defaults"];
		return true;
	}

	const FJsonObject* FindComponentByNameJson(const FJsonObject& Components, const FJsonObject& Name)
	{
		for (const FJsonObject& Component : Components)
			if (Component.is_object() && Component.contains("Name") && Component["Name"] == Name)
				return &Component;
		return nullptr;
	}

	FJsonObject DiffJson(const FJsonObject& Base, const FJsonObject& Full);

	// 컴포넌트 배열의 차이. 이름으로 짝짓는다. 바뀐 것이 없는 컴포넌트는 빠진다.
	FJsonObject DiffComponents(const FJsonObject& Base, const FJsonObject& Full)
	{
		FJsonObject Result = FJsonObject::array();
		for (const FJsonObject& Component : Full)
		{
			if (!Component.is_object() || !Component.contains("Name"))
				continue;
			const FJsonObject* BaseComponent = Base.is_array() ? FindComponentByNameJson(Base, Component["Name"]) : nullptr;
			if (!BaseComponent)
			{
				Result.push_back(Component);   // BP에 없는 컴포넌트 (인스턴스에서 추가)
				continue;
			}
			FJsonObject Delta = DiffJson(*BaseComponent, Component);
			// 루트(부모가 없는 컴포넌트)의 Transform은 항상 남긴다. 스폰할 때 루트 위치가 스폰 위치로 바뀌기 때문이다.
			const bool bRoot = Component.contains("Parent") && Component["Parent"] == "" &&
				!(Component.contains("ParentActor") && Component["ParentActor"] != "");
			if (bRoot && Component.contains("Properties") && Component["Properties"].contains("Transform"))
				Delta["Properties"]["Transform"] = Component["Properties"]["Transform"];
			if (Delta.empty())
				continue;
			Delta["Name"] = Component["Name"];
			Result.push_back(std::move(Delta));
		}
		return Result;
	}

	// Full에서 Base와 다른 값만 남긴 객체. 같으면 빈 객체.
	FJsonObject DiffJson(const FJsonObject& Base, const FJsonObject& Full)
	{
		FJsonObject Result = FJsonObject::object();
		if (!Full.is_object())
			return Result;
		for (auto It = Full.begin(); It != Full.end(); ++It)
		{
			const std::string& Key = It.key();
			const FJsonObject& Value = It.value();
			if (!Base.is_object() || !Base.contains(Key))
			{
				Result[Key] = Value;
				continue;
			}
			const FJsonObject& BaseValue = Base[Key];
			if (Key == "Components" && Value.is_array())
			{
				FJsonObject Delta = DiffComponents(BaseValue, Value);
				if (!Delta.empty())
					Result[Key] = std::move(Delta);
			}
			else if (Value.is_object() && BaseValue.is_object())
			{
				FJsonObject Delta = DiffJson(BaseValue, Value);
				if (!Delta.empty())
					Result[Key] = std::move(Delta);
			}
			else if (Value != BaseValue)
			{
				Result[Key] = Value;
			}
		}
		return Result;
	}

	// Delta를 Full 위에 얹는다. 컴포넌트는 이름으로 짝짓고, 없으면 새로 넣는다.
	void ApplyJsonDelta(FJsonObject& Full, const FJsonObject& Delta)
	{
		if (!Delta.is_object())
			return;
		for (auto It = Delta.begin(); It != Delta.end(); ++It)
		{
			const std::string& Key = It.key();
			const FJsonObject& Value = It.value();
			if (Key == "Components" && Value.is_array())
			{
				FJsonObject& Components = Full[Key];
				if (!Components.is_array())
					Components = FJsonObject::array();
				for (const FJsonObject& ComponentDelta : Value)
				{
					if (!ComponentDelta.is_object() || !ComponentDelta.contains("Name"))
						continue;
					FJsonObject* Target = nullptr;
					for (FJsonObject& Component : Components)
						if (Component.is_object() && Component.contains("Name") && Component["Name"] == ComponentDelta["Name"])
							Target = &Component;
					if (Target)
						ApplyJsonDelta(*Target, ComponentDelta);
					else
						Components.push_back(ComponentDelta);
				}
			}
			else if (Value.is_object() && Full.contains(Key) && Full[Key].is_object())
			{
				ApplyJsonDelta(Full[Key], Value);
			}
			else
			{
				Full[Key] = Value;
			}
		}
	}

	// Name이라는 BP 클래스를 만들거나 찾아 데이터를 채운다. 부모가 아직 없으면 nullptr.
	UClass* RegisterClassFromData(const FString& ClassName, FBlueprintData&& Data, FString& OutError)
	{
		UClass* Parent = FindClass(Data.ParentClassName);
		if (!Parent)
		{
			OutError = "parent class '" + Data.ParentClassName + "' not found";
			return nullptr;
		}
		if (!FBlueprint::IsValidParentClass(Parent))
		{
			OutError = "parent class '" + Data.ParentClassName + "' is not an Actor or an ActorComponent";
			return nullptr;
		}
		if (!Parent->Constructor)
		{
			OutError = "parent class '" + Data.ParentClassName + "' is abstract";
			return nullptr;
		}

		UClass* Class = FindClass(ClassName);
		if (Class && !FBlueprint::IsBlueprintClass(Class))
		{
			OutError = "a C++ class named '" + ClassName + "' already exists";
			return nullptr;
		}
		if (Class && Class->Super != Parent)
		{
			// 부모를 바꾸면 이미 만든 객체와 자식 목록이 어긋나므로 다시 시작할 때까지 미룬다.
			HTR_LOG(Warning, "Blueprint: parent of {} changed to {}; restart the editor to apply it", ClassName, Data.ParentClassName);
			Data.ParentClassName = Class->Super->Name;
		}
		if (!Class)
		{
			// 클래스는 프로그램이 끝날 때까지 산다 (C++ 클래스의 static UClass와 같다).
			Class = new UClass();
			Class->Name = ClassName;
			Class->Super = Parent;
			Class->Size = Parent->Size;
			Class->Constructor = Parent->Constructor;
			Class->PostConstructDefaults = &ApplyClassDefaultObjectDefaults;
			RegisterClass(Class);
		}

		// 기본값이 바뀌는 경우(BP를 저장하거나 다시 읽음), 에디터 레벨에 놓인 이 BP 액터들의 차이를 옛 기본값 기준으로 기억해 둔다.
		struct FInstanceDelta { AActor* Actor; FJsonObject Delta; };
		TArray<FInstanceDelta> Instances;
		FJsonObject OldDefaults;
		if (const FBlueprintData* OldData = FindData(Class); OldData && DefaultsToJson(*OldData, OldDefaults))
		{
			TArray<UObject*> Objects;
			GetObjectsOfClass(Class, Objects, false);
			for (UObject* Object : Objects)
			{
				AActor* Actor = Cast<AActor>(Object);
				if (!Actor || Actor->GetClass() != Class || Actor->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject))
					continue;
				// 레벨에 놓인 것만. Blueprint 에디터의 미리보기 액터나 PIE 액터는 건드리지 않는다.
				if (!Actor->GetWorld() || Actor->GetWorld()->GetWorldType() != EWorldType::Editor)
					continue;
				FJsonObject Full;
				if (ActorToJson(Actor, Full))
					Instances.Add({ Actor, DiffJson(OldDefaults, Full) });
			}
		}

		Data.Graphs = ReadGraphs(Data.FileBytes);
		GetRegistry()[Class] = std::move(Data);

		// 놓인 액터를 새 기본값 + 자기 차이로 다시 맞춘다. 인스턴스가 바꾸지 않은 값은 새 기본값을 따라간다.
		if (!Instances.IsEmpty())
		{
			FJsonObject NewDefaults;
			if (DefaultsToJson(GetRegistry()[Class], NewDefaults))
			{
				for (FInstanceDelta& Instance : Instances)
				{
					FJsonObject Merged = NewDefaults;
					ApplyJsonDelta(Merged, Instance.Delta);
					JsonToActor(Instance.Actor, Merged);
				}
				HTR_LOG(Info, "Blueprint: updated {} placed {} actor(s)", Instances.Num(), ClassName);
			}
		}

		// 이미 CDO가 있으면 새 기본값을 다시 적용한다 (편집한 BP를 다시 읽은 경우).
		if (Class->ClassDefaultObject)
			ApplyClassDefaultObjectDefaults(Class->ClassDefaultObject);
		return Class;
	}
}

bool FBlueprint::IsBlueprintPath(const FString& Path)
{
	FString Ext = FPaths::FromUtf8(Path).extension().string();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Ext == Extension;
}

UClass* FBlueprint::LoadBlueprint(const std::filesystem::path& File, FString* OutError)
{
	FString Error;
	FBlueprintData Data;
	Data.AssetPath = ToAssetPath(File);
	UClass* Result = nullptr;
	if (!ReadFileBytes(File, Data.FileBytes))
		Error = "cannot read file";
	else if (ReadHeader(Data.FileBytes, Data.ParentClassName, Error))
		Result = RegisterClassFromData(FPaths::ToUtf8(File.stem()), std::move(Data), Error);

	if (!Result)
	{
		HTR_LOG(Warning, "Blueprint: {} skipped ({})", FPaths::ToUtf8(File.generic_wstring()), Error);
		if (OutError) *OutError = Error;
	}
	return Result;
}

void FBlueprint::RegisterAll(const std::filesystem::path& AssetRoot)
{
	std::error_code Ec;
	if (!std::filesystem::exists(AssetRoot, Ec))
		return;

	// BP의 부모가 다른 BP일 수 있다. 부모가 아직 등록되지 않아 실패한 것은 다음 바퀴에 다시 시도한다.
	TArray<std::filesystem::path> Pending;
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(AssetRoot, Ec))
		if (Entry.is_regular_file() && IsBlueprintPath(FPaths::ToUtf8(Entry.path())))
			Pending.Add(Entry.path());

	int32 Registered = 0;
	while (!Pending.IsEmpty())
	{
		TArray<std::filesystem::path> Retry;
		for (const std::filesystem::path& File : Pending)
		{
			FBlueprintData Data;
			Data.AssetPath = ToAssetPath(File);
			FString Error;
			if (!ReadFileBytes(File, Data.FileBytes) || !ReadHeader(Data.FileBytes, Data.ParentClassName, Error))
			{
				HTR_LOG(Warning, "Blueprint: {} skipped ({})", Data.AssetPath, Error.empty() ? FString("cannot read file") : Error);
				continue;
			}
			if (!FindClass(Data.ParentClassName))
			{
				Retry.Add(File);
				continue;
			}
			if (RegisterClassFromData(FPaths::ToUtf8(File.stem()), std::move(Data), Error))
				++Registered;
			else
				HTR_LOG(Warning, "Blueprint: {} skipped ({})", FPaths::ToUtf8(File.generic_wstring()), Error);
		}
		// 한 바퀴 동안 하나도 못 풀었으면 부모를 끝내 찾지 못한 것이다.
		if (Retry.Num() == Pending.Num())
		{
			for (const std::filesystem::path& File : Retry)
				LoadBlueprint(File);   // 실패 이유를 로그에 남긴다
			break;
		}
		Pending = std::move(Retry);
	}
	if (Registered > 0)
		HTR_LOG(Info, "Blueprint: {} classes registered", Registered);
}

bool FBlueprint::IsValidParentClass(const UClass* Class)
{
	return Class && Class->Constructor &&
		(Class->IsChildOf(AActor::StaticClass()) || Class->IsChildOf(UActorComponent::StaticClass()));
}

bool FBlueprint::IsComponentBlueprint(const UClass* Class)
{
	return IsBlueprintClass(Class) && Class->IsChildOf(UActorComponent::StaticClass());
}

UClass* FBlueprint::SaveBlueprint(const std::filesystem::path& File, UClass* ParentClass, UObject* Template, FString& OutError,
	const FBPGraphSet* Graphs)
{
	if (!IsValidParentClass(ParentClass))
	{
		OutError = "parent class must be a non-abstract Actor or ActorComponent";
		return nullptr;
	}
	const bool bComponent = ParentClass->IsChildOf(UActorComponent::StaticClass());
	if (Template && !Template->IsA(ParentClass))
	{
		OutError = "template is not a " + ParentClass->Name;
		return nullptr;
	}
	// 컴포넌트 BP는 아직 노드 그래프를 실행하지 않으므로 쓰지 않는다.
	if (bComponent)
		Graphs = nullptr;

	// 템플릿이 없으면(새로 만드는 BP) 부모 클래스의 기본 객체를 그대로 기본값으로 쓴다.
	// 비워 두면 읽을 때마다 "필드 없음" 경고가 나오므로 부모 값을 채워 둔다.
	if (!Template)
		Template = ParentClass->GetDefaultObject();

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer.SetIsPersistent(true);   // 씬 파일처럼 Transient는 쓰지 않는다
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		int32 Version = FileVersion;
		FString ParentName = ParentClass->Name;
		Root << SA_VALUE("Version", Version) << SA_VALUE("ParentClass", ParentName);
		FStructuredArchive::FRecord Defaults = Root.EnterRecord("Defaults");
		if (Template)
			Template->Serialize(Defaults);
		Ar.Close();
	}
	if (Writer.IsError())
	{
		OutError = "failed to serialize defaults";
		return nullptr;
	}

	// 그래프: 넘겨받았으면 그것을, 아니면 지금 파일에 있던 것을 그대로 남긴다.
	{
		FJsonObject Root = FJsonObject::parse(Bytes.begin(), Bytes.end(), nullptr, false);
		FJsonObject GraphsJson;
		if (Graphs)
		{
			GraphsJson = FJsonObject::parse(FBlueprintGraphSerializer::ToJson(*Graphs), nullptr, false);
		}
		else if (!bComponent)
		{
			TArray<uint8> OldBytes;
			if (ReadFileBytes(File, OldBytes))
			{
				const FJsonObject Old = FJsonObject::parse(OldBytes.begin(), OldBytes.end(), nullptr, false);
				if (!Old.is_discarded() && Old.is_object() && Old.contains("Graphs"))
					GraphsJson = Old["Graphs"];
			}
		}
		if (!Root.is_discarded() && Root.is_object() && GraphsJson.is_object())
		{
			Root["Graphs"] = std::move(GraphsJson);
			const std::string Text = Root.dump(1, '\t');
			Bytes.SetNum(static_cast<int32>(Text.size()));
			std::memcpy(Bytes.GetData(), Text.data(), Text.size());
		}
	}

	std::error_code Ec;
	std::filesystem::create_directories(File.parent_path(), Ec);
	{
		std::ofstream Stream(File, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			OutError = "cannot write " + FPaths::ToUtf8(File.generic_wstring());
			return nullptr;
		}
		Stream.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	}
	return LoadBlueprint(File, &OutError);
}

std::shared_ptr<const FBPGraphSet> FBlueprint::GetGraphs(const UClass* Class)
{
	const FBlueprintData* Data = FindData(Class);
	return Data ? Data->Graphs : nullptr;
}

bool FBlueprint::IsBlueprintClass(const UClass* Class)
{
	return FindData(Class) != nullptr;
}

FString FBlueprint::GetAssetPath(const UClass* Class)
{
	const FBlueprintData* Data = FindData(Class);
	return Data ? Data->AssetPath : FString();
}

UClass* FBlueprint::GetNativeClass(UClass* Class)
{
	while (Class && IsBlueprintClass(Class))
		Class = Class->Super;
	return Class;
}

void FBlueprint::ApplyActorDefaults(AActor* Actor)
{
	if (Actor)
		ApplyChain(Actor->GetClass(), Actor, true);
}

void FBlueprint::ApplyComponentDefaults(UActorComponent* Component)
{
	if (Component)
		ApplyChain(Component->GetClass(), Component, true);
}

bool FBlueprint::SetDefaultStringProperty(UClass* BlueprintClass, const FString& PropertyName, const FString& Value, FString& OutError)
{
	const FBlueprintData* Data = FindData(BlueprintClass);
	if (!Data)
	{
		OutError = "not a blueprint class";
		return false;
	}

	// 기본값 전체를 다시 쓰지 않고 그 프로퍼티만 고친다. 컴포넌트 구성 등 나머지는 그대로 남는다.
	FJsonObject Root = FJsonObject::parse(Data->FileBytes.begin(), Data->FileBytes.end(), nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		OutError = "file is not valid JSON";
		return false;
	}
	Root["Defaults"]["Properties"][PropertyName] = Value;
	const std::string Text = Root.dump(1, '\t');

	const std::filesystem::path File = FPaths::FromUtf8(Data->AssetPath.find(':') == FString::npos
		? "Assets/" + Data->AssetPath : Data->AssetPath);
	{
		std::ofstream Stream(File, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			OutError = "cannot write " + Data->AssetPath;
			return false;
		}
		Stream.write(Text.data(), static_cast<std::streamsize>(Text.size()));
	}
	return LoadBlueprint(File, &OutError) != nullptr;
}

bool FBlueprint::MakeInstanceDelta(AActor* Actor, FString& OutDelta)
{
	const FBlueprintData* Data = Actor ? FindData(Actor->GetClass()) : nullptr;
	FJsonObject Defaults, Full;
	if (!Data || !DefaultsToJson(*Data, Defaults) || !ActorToJson(Actor, Full))
		return false;
	OutDelta = DiffJson(Defaults, Full).dump();
	return true;
}

bool FBlueprint::ApplyInstanceDelta(AActor* Actor, const FString& Delta)
{
	if (!Actor)
		return false;
	const FJsonObject DeltaJson = FJsonObject::parse(Delta, nullptr, false);
	if (DeltaJson.is_discarded() || !DeltaJson.is_object())
	{
		HTR_LOG(Warning, "Blueprint: broken instance data for {}", Actor->GetName());
		return false;
	}
	if (DeltaJson.empty())
		return true;   // 기본값 그대로

	// 지금 상태(BP 기본값이 적용된 액터)를 JSON으로 만들어 차이를 얹고 다시 읽는다.
	// 통째로 읽으므로 "필드 없음" 경고가 나지 않는다.
	FJsonObject Full;
	if (!ActorToJson(Actor, Full))
		return false;
	ApplyJsonDelta(Full, DeltaJson);
	return JsonToActor(Actor, Full);
}
