#include "EnginePCH.h"
#include "Input/InputAssetLibrary.h"

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <cctype>
#include <cstring>

#include "Asset/AssetUtils.h"
#include "Core/EngineLog.h"
#include "Input/InputAction.h"
#include "Input/InputCoreTypes.h"
#include "Input/InputMappingContext.h"

namespace
{
	// FString은 UTF-8이다 (u8path는 C++20에서 deprecated)
	std::filesystem::path ToFsPath(const FString& Text)
	{
		return std::filesystem::path(reinterpret_cast<const char8_t*>(Text.c_str()));
	}

	struct FCachedInputAsset
	{
		UObject* Object = nullptr;
		std::filesystem::file_time_type WriteTime{};
	};

	std::unordered_map<FString, FCachedInputAsset>& GetCache()
	{
		static std::unordered_map<FString, FCachedInputAsset> Cache;
		return Cache;
	}

	// ---- 이름 <-> enum ----

	const char* ValueTypeToString(EInputActionValueType Type)
	{
		switch (Type)
		{
		case EInputActionValueType::Boolean: return "Boolean";
		case EInputActionValueType::Axis1D:  return "Axis1D";
		case EInputActionValueType::Axis2D:  return "Axis2D";
		case EInputActionValueType::Axis3D:  return "Axis3D";
		}
		return "Boolean";
	}

	bool ValueTypeFromString(const FString& Name, EInputActionValueType& Out)
	{
		if (Name == "Boolean" || Name == "Bool") { Out = EInputActionValueType::Boolean; return true; }
		if (Name == "Axis1D" || Name == "Float") { Out = EInputActionValueType::Axis1D; return true; }
		if (Name == "Axis2D" || Name == "Vector2D") { Out = EInputActionValueType::Axis2D; return true; }
		if (Name == "Axis3D" || Name == "Vector") { Out = EInputActionValueType::Axis3D; return true; }
		return false;
	}

	const char* SwizzleToString(EInputAxisSwizzle Order)
	{
		switch (Order)
		{
		case EInputAxisSwizzle::YXZ: return "YXZ";
		case EInputAxisSwizzle::ZYX: return "ZYX";
		case EInputAxisSwizzle::XZY: return "XZY";
		case EInputAxisSwizzle::YZX: return "YZX";
		case EInputAxisSwizzle::ZXY: return "ZXY";
		}
		return "YXZ";
	}

	EInputAxisSwizzle SwizzleFromString(const FString& Name)
	{
		if (Name == "ZYX") return EInputAxisSwizzle::ZYX;
		if (Name == "XZY") return EInputAxisSwizzle::XZY;
		if (Name == "YZX") return EInputAxisSwizzle::YZX;
		if (Name == "ZXY") return EInputAxisSwizzle::ZXY;
		return EInputAxisSwizzle::YXZ;
	}

	// 숫자 하나면 세 축 모두, 배열이면 [X, Y, Z]
	FVector ReadVector(const json& Value, const FVector& Default)
	{
		if (Value.is_number())
		{
			const float F = Value.get<float>();
			return FVector(F, F, F);
		}
		if (Value.is_array())
		{
			FVector Out = Default;
			if (Value.size() > 0 && Value[0].is_number()) Out.X = Value[0].get<float>();
			if (Value.size() > 1 && Value[1].is_number()) Out.Y = Value[1].get<float>();
			if (Value.size() > 2 && Value[2].is_number()) Out.Z = Value[2].get<float>();
			return Out;
		}
		return Default;
	}

	// ---- Modifier ----

	std::shared_ptr<FInputModifier> ReadModifier(const json& Node, const FString& Context)
	{
		const FString Type = Node.value("Type", FString());
		if (Type == "Negate")
			return std::make_shared<FInputModifierNegate>(Node.value("X", true), Node.value("Y", true), Node.value("Z", true));
		if (Type == "SwizzleAxis" || Type == "Swizzle")
			return std::make_shared<FInputModifierSwizzleAxis>(SwizzleFromString(Node.value("Order", FString("YXZ"))));
		if (Type == "Scale" || Type == "Scalar")
		{
			const FVector Scalar = Node.contains("Scalar") ? ReadVector(Node["Scalar"], FVector(1.0f, 1.0f, 1.0f)) : FVector(1.0f, 1.0f, 1.0f);
			return std::make_shared<FInputModifierScale>(Scalar, Node.value("ScaleByDeltaTime", false));
		}
		if (Type == "DeadZone")
			return std::make_shared<FInputModifierDeadZone>(Node.value("Lower", 0.2f), Node.value("Upper", 1.0f));

		HTR_LOG(Warning, "Input asset {}: unknown modifier type '{}'", Context, Type);
		return nullptr;
	}

	json WriteModifier(const FInputModifier& Modifier)
	{
		json Node = json::object();
		if (const auto* Negate = dynamic_cast<const FInputModifierNegate*>(&Modifier))
		{
			Node["Type"] = "Negate";
			if (!Negate->bX) Node["X"] = false;
			if (!Negate->bY) Node["Y"] = false;
			if (!Negate->bZ) Node["Z"] = false;
		}
		else if (const auto* Swizzle = dynamic_cast<const FInputModifierSwizzleAxis*>(&Modifier))
		{
			Node["Type"] = "SwizzleAxis";
			Node["Order"] = SwizzleToString(Swizzle->Order);
		}
		else if (const auto* Scale = dynamic_cast<const FInputModifierScale*>(&Modifier))
		{
			Node["Type"] = "Scale";
			Node["Scalar"] = { Scale->Scalar.X, Scale->Scalar.Y, Scale->Scalar.Z };
			if (Scale->bScaleByDeltaTime) Node["ScaleByDeltaTime"] = true;
		}
		else if (const auto* DeadZone = dynamic_cast<const FInputModifierDeadZone*>(&Modifier))
		{
			Node["Type"] = "DeadZone";
			Node["Lower"] = DeadZone->LowerThreshold;
			Node["Upper"] = DeadZone->UpperThreshold;
		}
		return Node;
	}

	// ---- Trigger ----

	std::shared_ptr<FInputTrigger> ReadTrigger(const json& Node, const FString& Context)
	{
		const FString Type = Node.value("Type", FString());
		std::shared_ptr<FInputTrigger> Trigger;
		if (Type == "Down")
			Trigger = std::make_shared<FInputTriggerDown>();
		else if (Type == "Pressed")
			Trigger = std::make_shared<FInputTriggerPressed>();
		else if (Type == "Released")
			Trigger = std::make_shared<FInputTriggerReleased>();
		else if (Type == "Hold")
			Trigger = std::make_shared<FInputTriggerHold>(Node.value("HoldTime", 0.5f), Node.value("OneShot", false));
		else
		{
			HTR_LOG(Warning, "Input asset {}: unknown trigger type '{}'", Context, Type);
			return nullptr;
		}

		Trigger->ActuationThreshold = Node.value("Threshold", Trigger->ActuationThreshold);
		return Trigger;
	}

	json WriteTrigger(const FInputTrigger& Trigger)
	{
		json Node = json::object();
		if (dynamic_cast<const FInputTriggerDown*>(&Trigger)) Node["Type"] = "Down";
		else if (dynamic_cast<const FInputTriggerPressed*>(&Trigger)) Node["Type"] = "Pressed";
		else if (dynamic_cast<const FInputTriggerReleased*>(&Trigger)) Node["Type"] = "Released";
		else if (const auto* Hold = dynamic_cast<const FInputTriggerHold*>(&Trigger))
		{
			Node["Type"] = "Hold";
			Node["HoldTime"] = Hold->HoldTimeThreshold;
			if (Hold->bIsOneShot) Node["OneShot"] = true;
		}
		if (Trigger.ActuationThreshold != 0.5f) Node["Threshold"] = Trigger.ActuationThreshold;
		return Node;
	}

	template <typename T, typename ReadFn>
	std::vector<std::shared_ptr<T>> ReadList(const json& Parent, const char* Field, const FString& Context, ReadFn Read)
	{
		std::vector<std::shared_ptr<T>> Out;
		const auto It = Parent.find(Field);
		if (It == Parent.end() || !It->is_array()) return Out;
		for (const json& Node : *It)
		{
			if (!Node.is_object()) continue;
			if (auto Item = Read(Node, Context)) Out.push_back(std::move(Item));
		}
		return Out;
	}

	template <typename T, typename WriteFn>
	json WriteList(const std::vector<std::shared_ptr<T>>& Items, WriteFn Write)
	{
		json Out = json::array();
		for (const auto& Item : Items)
			if (Item) Out.push_back(Write(*Item));
		return Out;
	}

	// ---- 파일 ----

	bool ReadJsonFile(const FString& FilePath, json& Out)
	{
		std::ifstream File(ToFsPath(FilePath));
		if (!File) return false;
		Out = json::parse(File, nullptr, false);
		return !Out.is_discarded() && Out.is_object();
	}

	bool WriteJsonFile(const FString& FilePath, const json& Root)
	{
		const std::filesystem::path Path = ToFsPath(FilePath);
		std::error_code Error;
		if (Path.has_parent_path()) std::filesystem::create_directories(Path.parent_path(), Error);
		std::ofstream File(Path, std::ios::trunc);
		if (!File) return false;
		File << Root.dump(4) << '\n';
		return static_cast<bool>(File);
	}

	std::filesystem::file_time_type GetWriteTime(const FString& FilePath)
	{
		std::error_code Error;
		const auto Time = std::filesystem::last_write_time(ToFsPath(FilePath), Error);
		return Error ? std::filesystem::file_time_type{} : Time;
	}

	FString GetStem(const FString& FilePath)
	{
		return ToFsPath(FilePath).stem().string();
	}

	// ---- 에셋 → 객체 ----

	bool ReadActionInto(UInputAction& Action, const json& Root, const FString& Key)
	{
		EInputActionValueType ValueType = EInputActionValueType::Boolean;
		const FString TypeName = Root.value("ValueType", FString("Boolean"));
		if (!ValueTypeFromString(TypeName, ValueType))
		{
			HTR_LOG(Warning, "Input action {}: unknown ValueType '{}', using Boolean", Key, TypeName);
		}

		Action.ValueType = ValueType;
		Action.bConsumeInput = Root.value("ConsumeInput", true);
		Action.Modifiers = ReadList<FInputModifier>(Root, "Modifiers", Key, ReadModifier);
		Action.Triggers = ReadList<FInputTrigger>(Root, "Triggers", Key, ReadTrigger);
		Action.AssetPath = Key;
		return true;
	}

	bool ReadContextInto(UInputMappingContext& Context, const json& Root, const FString& Key)
	{
		Context.UnmapAll();
		Context.AssetPath = Key;

		const auto It = Root.find("Mappings");
		if (It == Root.end() || !It->is_array()) return true;

		for (const json& Node : *It)
		{
			if (!Node.is_object()) continue;

			const FString ActionPath = Node.value("Action", FString());
			const FString KeyName = Node.value("Key", FString());

			// 빈 Action/Key는 에디터에서 아직 고르지 않은 매핑이다. 그대로 두고 Subsystem은 건너뛴다
			UInputAction* Action = ActionPath.empty() ? nullptr : FInputAssetLibrary::LoadInputAction(ActionPath);
			if (!ActionPath.empty() && !Action)
			{
				HTR_LOG(Warning, "Input context {}: cannot load action '{}'", Key, ActionPath);
				continue;
			}
			const FKey InputKey = KeyName.empty() ? EKeys::Invalid : FInputKeyNames::FromString(KeyName);
			if (!KeyName.empty() && !InputKey.IsValid())
			{
				HTR_LOG(Warning, "Input context {}: unknown key '{}'", Key, KeyName);
				continue;
			}

			FEnhancedActionKeyMapping& Mapping = Context.MapKey(Action, InputKey);
			Mapping.Modifiers = ReadList<FInputModifier>(Node, "Modifiers", Key, ReadModifier);
			Mapping.Triggers = ReadList<FInputTrigger>(Node, "Triggers", Key, ReadTrigger);
		}
		return true;
	}

	// 캐시 확인 → 없거나 파일이 바뀌었으면 (다시) 읽는다
	template <typename T, typename CreateFn, typename ReadFn>
	T* LoadCached(const FString& Path, const char* Extension, CreateFn Create, ReadFn ReadInto)
	{
		if (Path.empty()) return nullptr;

		const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
		const FString Key = FAssetUtils::GetAssetKey(FilePath.empty() ? Path : FilePath);
		if (FilePath.empty())
		{
			HTR_LOG(Error, "Input asset '{}': cannot resolve file path", Path);
			return nullptr;
		}

		auto& Cache = GetCache();
		const auto It = Cache.find(Key);
		T* Existing = It != Cache.end() ? static_cast<T*>(It->second.Object) : nullptr;

		const auto WriteTime = GetWriteTime(FilePath);
		if (Existing && It->second.WriteTime == WriteTime) return Existing;

		json Root;
		if (!ReadJsonFile(FilePath, Root))
		{
			// 이미 읽어 둔 게 있으면 (편집 중 잘못 저장한 경우 등) 이전 내용을 계속 쓴다
			HTR_LOG(Error, "Input asset {}: cannot open or parse '{}'", Key, FilePath);
			return Existing;
		}

		const FString Stem = GetStem(FilePath);
		if (ToFsPath(FilePath).extension().string() != Extension)
		{
			HTR_LOG(Warning, "Input asset {}: expected extension {}", Key, Extension);
		}

		T* Object = Existing ? Existing : Create(Stem.c_str());
		if (!Object) return nullptr;

		// 캐시 등록을 먼저 해서 Context가 같은 Action을 여러 번 참조해도 한 번만 만든다
		Cache[Key] = { Object, WriteTime };
		ReadInto(*Object, Root, Key);
		return Object;
	}
}

UInputAction* FInputAssetLibrary::LoadInputAction(const FString& Path)
{
	return LoadCached<UInputAction>(Path, ActionExtension,
		[](const char* Name) { return UInputAction::Create(nullptr, Name, EInputActionValueType::Boolean); },
		ReadActionInto);
}

UInputMappingContext* FInputAssetLibrary::LoadMappingContext(const FString& Path)
{
	return LoadCached<UInputMappingContext>(Path, ContextExtension,
		[](const char* Name) { return UInputMappingContext::Create(nullptr, Name); },
		ReadContextInto);
}

namespace
{
	json BuildActionJson(const UInputAction& Action)
	{
		json Root = json::object();
		Root["ValueType"] = ValueTypeToString(Action.ValueType);
		Root["ConsumeInput"] = Action.bConsumeInput;
		Root["Modifiers"] = WriteList(Action.Modifiers, WriteModifier);
		Root["Triggers"] = WriteList(Action.Triggers, WriteTrigger);
		return Root;
	}

	json BuildContextJson(const UInputMappingContext& Context)
	{
		json Mappings = json::array();
		for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
		{
			// Action이 비어 있거나 에셋이 아닌 매핑도 편집 중일 수 있으므로 빈 경로로 남긴다
			json Node = json::object();
			Node["Action"] = Mapping.Action ? Mapping.Action->AssetPath : FString();
			Node["Key"] = Mapping.Key.IsValid() ? FInputKeyNames::ToString(Mapping.Key) : FString();
			if (!Mapping.Modifiers.empty()) Node["Modifiers"] = WriteList(Mapping.Modifiers, WriteModifier);
			if (!Mapping.Triggers.empty()) Node["Triggers"] = WriteList(Mapping.Triggers, WriteTrigger);
			Mappings.push_back(std::move(Node));
		}

		json Root = json::object();
		Root["Mappings"] = std::move(Mappings);
		return Root;
	}

	bool EndsWith(const FString& Text, const char* Suffix)
	{
		const size_t N = std::strlen(Suffix);
		if (Text.size() < N) return false;
		for (size_t I = 0; I < N; ++I)
		{
			if (std::tolower(static_cast<unsigned char>(Text[Text.size() - N + I])) != Suffix[I]) return false;
		}
		return true;
	}
}

bool FInputAssetLibrary::IsInputActionPath(const FString& Path)
{
	return EndsWith(Path, ActionExtension);
}

bool FInputAssetLibrary::IsMappingContextPath(const FString& Path)
{
	return EndsWith(Path, ContextExtension);
}

FString FInputAssetLibrary::SerializeInputAction(const UInputAction& Action)
{
	return BuildActionJson(Action).dump(4);
}

FString FInputAssetLibrary::SerializeMappingContext(const UInputMappingContext& Context)
{
	return BuildContextJson(Context).dump(4);
}

bool FInputAssetLibrary::SaveInputAction(UInputAction& Action, const FString& Path)
{
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	if (FilePath.empty()) return false;

	if (!WriteJsonFile(FilePath, BuildActionJson(Action)))
	{
		HTR_LOG(Error, "Input action: cannot write '{}'", FilePath);
		return false;
	}

	const FString Key = FAssetUtils::GetAssetKey(FilePath);
	Action.AssetPath = Key;
	GetCache()[Key] = { &Action, GetWriteTime(FilePath) };
	return true;
}

bool FInputAssetLibrary::SaveMappingContext(UInputMappingContext& Context, const FString& Path)
{
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	if (FilePath.empty()) return false;

	for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
	{
		if (Mapping.Action && Mapping.Action->AssetPath.empty())
			HTR_LOG(Warning, "Input context: action '{}' has no asset path; it will not load back", Mapping.Action->GetName());
	}

	if (!WriteJsonFile(FilePath, BuildContextJson(Context)))
	{
		HTR_LOG(Error, "Input context: cannot write '{}'", FilePath);
		return false;
	}

	const FString Key = FAssetUtils::GetAssetKey(FilePath);
	Context.AssetPath = Key;
	GetCache()[Key] = { &Context, GetWriteTime(FilePath) };
	return true;
}

bool FInputAssetLibrary::CreateDefaultInputAction(const FString& Path, FString& OutError)
{
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	json Root = json::object();
	Root["ValueType"] = "Boolean";
	Root["ConsumeInput"] = true;
	Root["Modifiers"] = json::array();
	Root["Triggers"] = json::array();
	if (FilePath.empty() || !WriteJsonFile(FilePath, Root))
	{
		OutError = "cannot write " + Path;
		return false;
	}
	return true;
}

bool FInputAssetLibrary::CreateDefaultMappingContext(const FString& Path, FString& OutError)
{
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	json Root = json::object();
	Root["Mappings"] = json::array();
	if (FilePath.empty() || !WriteJsonFile(FilePath, Root))
	{
		OutError = "cannot write " + Path;
		return false;
	}
	return true;
}

void FInputAssetLibrary::ReloadFromDisk(const FString& Path)
{
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	const FString Key = FAssetUtils::GetAssetKey(FilePath.empty() ? Path : FilePath);
	auto& Cache = GetCache();
	const auto It = Cache.find(Key);
	if (It == Cache.end()) return;

	// 기록해 둔 시간을 지우면 다음 Load가 파일을 다시 읽어 같은 객체에 덮어쓴다
	It->second.WriteTime = {};
	if (IsInputActionPath(Key)) LoadInputAction(Key);
	else if (IsMappingContextPath(Key)) LoadMappingContext(Key);
}
