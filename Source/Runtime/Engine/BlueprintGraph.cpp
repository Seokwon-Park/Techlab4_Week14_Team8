#include "EnginePCH.h"
#include "Engine/BlueprintGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Core/EngineLog.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Json/Dom/JsonObject.h"
#include "Math/Rotator.h"
#include "UObject/Class.h"
#include "UObject/Function.h"
#include "UObject/UObjectHash.h"

#include <cctype>
#include <variant>

// ---------------- 그래프 데이터 ----------------

const FBPPin* FBPNode::FindPin(const FString& Name, bool bOutput) const
{
	for (const FBPPin& Pin : Pins)
		if (Pin.bOutput == bOutput && Pin.Name == Name)
			return &Pin;
	return nullptr;
}

FBPPin* FBPNode::FindPin(const FString& Name, bool bOutput)
{
	for (FBPPin& Pin : Pins)
		if (Pin.bOutput == bOutput && Pin.Name == Name)
			return &Pin;
	return nullptr;
}

FBPNode* FBPGraph::FindNode(int32 Id)
{
	for (FBPNode& Node : Nodes)
		if (Node.Id == Id)
			return &Node;
	return nullptr;
}

const FBPNode* FBPGraph::FindNode(int32 Id) const
{
	for (const FBPNode& Node : Nodes)
		if (Node.Id == Id)
			return &Node;
	return nullptr;
}

const FBPPin* FBPGraph::FindPin(int32 PinId, const FBPNode** OutNode) const
{
	for (const FBPNode& Node : Nodes)
		for (const FBPPin& Pin : Node.Pins)
			if (Pin.Id == PinId)
			{
				if (OutNode) *OutNode = &Node;
				return &Pin;
			}
	return nullptr;
}

bool FBPGraph::IsPinLinked(int32 PinId) const
{
	for (const FBPLink& Link : Links)
		if (Link.FromPin == PinId || Link.ToPin == PinId)
			return true;
	return false;
}

FBPNode* FBPGraph::AddNode(const FString& Type, float InX, float InY, const FString& Param)
{
	TArray<FBPPinDef> PinDefs;
	if (!FBlueprintNodeLibrary::GetPinDefs(Type, Param, PinDefs))
		return nullptr;
	FBPNode Node;
	Node.Id = NewId();
	Node.Type = Type;
	Node.Param = Param;
	Node.X = InX;
	Node.Y = InY;
	for (const FBPPinDef& PinDef : PinDefs)
	{
		FBPPin Pin;
		Pin.Id = NewId();
		Pin.Name = PinDef.Name;
		Pin.Type = PinDef.Type;
		Pin.bOutput = PinDef.bOutput;
		Pin.Default = PinDef.Default;
		Pin.Default.Type = PinDef.Type;
		Node.Pins.Add(Pin);
	}
	Nodes.Add(std::move(Node));
	return &Nodes.Last();
}

void FBPGraph::RemoveNode(int32 NodeId)
{
	const FBPNode* Node = FindNode(NodeId);
	if (!Node)
		return;
	for (const FBPPin& Pin : Node->Pins)
		BreakPinLinks(Pin.Id);
	for (int32 Index = Nodes.Num() - 1; Index >= 0; --Index)
		if (Nodes[Index].Id == NodeId)
			Nodes.RemoveAt(Index);
}

bool FBPGraph::CanConnect(int32 PinA, int32 PinB, FString* OutReason) const
{
	auto Fail = [OutReason](const char* Reason) { if (OutReason) *OutReason = Reason; return false; };
	const FBPNode* NodeA = nullptr;
	const FBPNode* NodeB = nullptr;
	const FBPPin* A = FindPin(PinA, &NodeA);
	const FBPPin* B = FindPin(PinB, &NodeB);
	if (!A || !B)
		return Fail("Unknown pin");
	if (NodeA == NodeB)
		return Fail("Both pins are on the same node");
	if (A->bOutput == B->bOutput)
		return Fail(A->bOutput ? "Both pins are outputs" : "Both pins are inputs");
	if (A->Type != B->Type)
		return Fail("Pin types do not match");
	return true;
}

bool FBPGraph::Connect(int32 PinA, int32 PinB)
{
	if (!CanConnect(PinA, PinB))
		return false;
	const FBPPin* A = FindPin(PinA);
	const int32 From = A->bOutput ? PinA : PinB;
	const int32 To = A->bOutput ? PinB : PinA;
	const FBPPin* FromPin = FindPin(From);

	// 데이터 입력 핀은 값 하나만 받고, 실행 출력 핀은 다음 노드 하나만 가리킨다 (UE와 같다).
	for (int32 Index = Links.Num() - 1; Index >= 0; --Index)
	{
		const FBPLink& Link = Links[Index];
		if (Link.FromPin == From && Link.ToPin == To)
			return true;
		if ((FromPin->Type != EBPPinType::Exec && Link.ToPin == To) ||
			(FromPin->Type == EBPPinType::Exec && Link.FromPin == From))
			Links.RemoveAt(Index);
	}
	Links.Add(FBPLink{ NewId(), From, To });
	return true;
}

void FBPGraph::RemoveLink(int32 LinkId)
{
	for (int32 Index = Links.Num() - 1; Index >= 0; --Index)
		if (Links[Index].Id == LinkId)
			Links.RemoveAt(Index);
}

void FBPGraph::BreakPinLinks(int32 PinId)
{
	for (int32 Index = Links.Num() - 1; Index >= 0; --Index)
		if (Links[Index].FromPin == PinId || Links[Index].ToPin == PinId)
			Links.RemoveAt(Index);
}

FBPGraphSet::FBPGraphSet()
{
	EventGraph.Name = "EventGraph";
	ConstructionScript.Name = "Construction Script";
	// UE의 새 BP처럼 자주 쓰는 이벤트를 미리 놓는다.
	EventGraph.AddNode("EventBeginPlay", 0.0f, 0.0f);
	EventGraph.AddNode("EventActorBeginOverlap", 0.0f, 160.0f);
	EventGraph.AddNode("EventTick", 0.0f, 320.0f);
	ConstructionScript.AddNode("ConstructionScript", 0.0f, 0.0f);
}

bool FBPGraphSet::IsEmpty() const
{
	return EventGraph.Links.IsEmpty() && ConstructionScript.Links.IsEmpty();
}

// ---------------- JSON ----------------

namespace
{
	FJsonObject ValueToJson(const FBPValue& Value)
	{
		switch (Value.Type)
		{
		case EBPPinType::Bool:   return Value.Bool;
		case EBPPinType::Float:  return Value.Float;
		case EBPPinType::String: return Value.String;
		case EBPPinType::Vector: return FJsonObject::array({ Value.Vector.X, Value.Vector.Y, Value.Vector.Z });
		default:                 return nullptr;
		}
	}

	void JsonToValue(const FJsonObject& Json, FBPValue& Value)
	{
		switch (Value.Type)
		{
		case EBPPinType::Bool:   if (Json.is_boolean()) Value.Bool = Json.get<bool>(); break;
		case EBPPinType::Float:  if (Json.is_number()) Value.Float = Json.get<float>(); break;
		case EBPPinType::String: if (Json.is_string()) Value.String = Json.get<std::string>(); break;
		case EBPPinType::Vector:
			if (Json.is_array() && Json.size() == 3)
				Value.Vector = FVector(Json[0].get<float>(), Json[1].get<float>(), Json[2].get<float>());
			break;
		default: break;
		}
	}

	FJsonObject GraphToJson(const FBPGraph& Graph)
	{
		FJsonObject Nodes = FJsonObject::array();
		for (const FBPNode& Node : Graph.Nodes)
		{
			FJsonObject Pins = FJsonObject::array();
			for (const FBPPin& Pin : Node.Pins)
			{
				FJsonObject PinJson = { { "Id", Pin.Id }, { "Name", Pin.Name }, { "Out", Pin.bOutput } };
				if (!Pin.bOutput && Pin.Type != EBPPinType::Exec && Pin.Type != EBPPinType::Object)
					PinJson["Default"] = ValueToJson(Pin.Default);
				Pins.push_back(std::move(PinJson));
			}
			FJsonObject NodeJson = { { "Id", Node.Id }, { "Type", Node.Type }, { "X", Node.X }, { "Y", Node.Y }, { "Pins", std::move(Pins) } };
			if (!Node.Param.empty())
				NodeJson["Param"] = Node.Param;
			Nodes.push_back(std::move(NodeJson));
		}
		FJsonObject Links = FJsonObject::array();
		for (const FBPLink& Link : Graph.Links)
			Links.push_back({ { "Id", Link.Id }, { "From", Link.FromPin }, { "To", Link.ToPin } });
		return { { "NextId", Graph.NextId }, { "Nodes", std::move(Nodes) }, { "Links", std::move(Links) } };
	}

	void JsonToGraph(const FJsonObject& Json, FBPGraph& Graph)
	{
		Graph.Nodes.Empty();
		Graph.Links.Empty();
		Graph.NextId = Json.value("NextId", 1);
		int32 MaxId = 0;

		if (Json.contains("Nodes") && Json["Nodes"].is_array())
		{
			for (const FJsonObject& NodeJson : Json["Nodes"])
			{
				const FString Type = NodeJson.value("Type", std::string());
				const FString Param = NodeJson.value("Param", std::string());
				TArray<FBPPinDef> PinDefs;
				if (!FBlueprintNodeLibrary::GetPinDefs(Type, Param, PinDefs))
				{
					// 지워졌거나 이름이 바뀐 UFUNCTION도 여기로 온다.
					HTR_LOG(Warning, "Blueprint graph: unknown node '{}' {} skipped", Type, Param);
					continue;
				}
				FBPNode Node;
				Node.Id = NodeJson.value("Id", 0);
				Node.Type = Type;
				Node.Param = Param;
				Node.X = NodeJson.value("X", 0.0f);
				Node.Y = NodeJson.value("Y", 0.0f);
				MaxId = (std::max)(MaxId, Node.Id);

				// 핀은 지금 라이브러리 정의대로 만들고, 저장된 핀은 이름으로 짝지어 Id와 기본값을 되살린다.
				for (const FBPPinDef& PinDef : PinDefs)
				{
					FBPPin Pin;
					Pin.Id = 0;
					Pin.Name = PinDef.Name;
					Pin.Type = PinDef.Type;
					Pin.bOutput = PinDef.bOutput;
					Pin.Default = PinDef.Default;
					Pin.Default.Type = PinDef.Type;
					if (NodeJson.contains("Pins"))
					{
						for (const FJsonObject& PinJson : NodeJson["Pins"])
						{
							if (PinJson.value("Name", std::string()) != Pin.Name || PinJson.value("Out", false) != Pin.bOutput)
								continue;
							Pin.Id = PinJson.value("Id", 0);
							if (PinJson.contains("Default"))
								JsonToValue(PinJson["Default"], Pin.Default);
							break;
						}
					}
					MaxId = (std::max)(MaxId, Pin.Id);
					Node.Pins.Add(Pin);
				}
				Graph.Nodes.Add(std::move(Node));
			}
		}
		Graph.NextId = (std::max)(Graph.NextId, MaxId + 1);
		// 새로 생긴 핀(정의에 핀이 늘어난 경우)에 Id를 준다.
		for (FBPNode& Node : Graph.Nodes)
			for (FBPPin& Pin : Node.Pins)
				if (Pin.Id == 0)
					Pin.Id = Graph.NewId();

		if (Json.contains("Links") && Json["Links"].is_array())
		{
			for (const FJsonObject& LinkJson : Json["Links"])
			{
				FBPLink Link{ LinkJson.value("Id", 0), LinkJson.value("From", 0), LinkJson.value("To", 0) };
				if (Graph.CanConnect(Link.FromPin, Link.ToPin))
				{
					Graph.NextId = (std::max)(Graph.NextId, Link.Id + 1);
					Graph.Links.Add(Link);
				}
			}
		}
	}
}

FString FBlueprintGraphSerializer::ToJson(const FBPGraphSet& Graphs)
{
	const FJsonObject Root = {
		{ "EventGraph", GraphToJson(Graphs.EventGraph) },
		{ "ConstructionScript", GraphToJson(Graphs.ConstructionScript) },
	};
	return Root.dump();
}

bool FBlueprintGraphSerializer::FromJson(const FString& Json, FBPGraphSet& OutGraphs)
{
	const FJsonObject Root = FJsonObject::parse(Json, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
		return false;
	if (Root.contains("EventGraph"))
		JsonToGraph(Root["EventGraph"], OutGraphs.EventGraph);
	if (Root.contains("ConstructionScript"))
		JsonToGraph(Root["ConstructionScript"], OutGraphs.ConstructionScript);
	// Construction Script 시작 노드는 언제나 하나 있어야 한다.
	bool bHasEntry = false;
	for (const FBPNode& Node : OutGraphs.ConstructionScript.Nodes)
		bHasEntry |= Node.Type == "ConstructionScript";
	if (!bHasEntry)
		OutGraphs.ConstructionScript.AddNode("ConstructionScript", 0.0f, 0.0f);
	return true;
}

// ---------------- 노드 라이브러리 ----------------

namespace
{
	FBPValue DefaultOf(EBPPinType Type) { FBPValue V; V.Type = Type; return V; }

	FBPPinDef ExecIn() { return { "In", EBPPinType::Exec, false, DefaultOf(EBPPinType::Exec) }; }
	FBPPinDef ExecOut(const char* Name = "Then") { return { Name, EBPPinType::Exec, true, DefaultOf(EBPPinType::Exec) }; }
	FBPPinDef InPin(const char* Name, EBPPinType Type) { return { Name, Type, false, DefaultOf(Type) }; }
	FBPPinDef InFloat(const char* Name, float V) { return { Name, EBPPinType::Float, false, FBPValue::MakeFloat(V) }; }
	FBPPinDef InBool(const char* Name, bool V) { return { Name, EBPPinType::Bool, false, FBPValue::MakeBool(V) }; }
	FBPPinDef InString(const char* Name, const char* V) { return { Name, EBPPinType::String, false, FBPValue::MakeString(V) }; }
	FBPPinDef InVector(const char* Name, const FVector& V) { return { Name, EBPPinType::Vector, false, FBPValue::MakeVector(V) }; }
	FBPPinDef OutPin(const char* Name, EBPPinType Type) { return { Name, Type, true, DefaultOf(Type) }; }
	// 연결하지 않으면 Self(액터) 또는 Self의 루트 컴포넌트를 쓰는 Target 핀
	FBPPinDef TargetPin() { return InPin("Target", EBPPinType::Object); }

	FVector VAdd(const FVector& A, const FVector& B) { return FVector(A.X + B.X, A.Y + B.Y, A.Z + B.Z); }
	FVector VSub(const FVector& A, const FVector& B) { return FVector(A.X - B.X, A.Y - B.Y, A.Z - B.Z); }
	FVector VScale(const FVector& A, float S) { return FVector(A.X * S, A.Y * S, A.Z * S); }

	// 회전 핀은 UE의 FRotator 표시처럼 (X=Roll, Y=Pitch, Z=Yaw) 벡터로 다룬다.
	FRotator ToRotator(const FVector& V) { return FRotator(V.Y, V.Z, V.X); }
	FVector FromRotator(const FRotator& R) { return FVector(R.Roll, R.Pitch, R.Yaw); }

	FString FloatToString(float Value)
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), "%g", Value);
		return Buffer;
	}

	AActor* TargetActor(FBlueprintExecContext& Ctx, const FBPNode& Node)
	{
		UObject* Object = Ctx.GetObject(Node, "Target");
		if (AActor* Actor = Cast<AActor>(Object))
			return Actor;
		if (UActorComponent* Component = Cast<UActorComponent>(Object))
			return Component->GetOwner();
		return Ctx.GetSelf();
	}

	USceneComponent* TargetScene(FBlueprintExecContext& Ctx, const FBPNode& Node)
	{
		UObject* Object = Ctx.GetObject(Node, "Target");
		if (USceneComponent* Scene = Cast<USceneComponent>(Object))
			return Scene;
		if (AActor* Actor = Cast<AActor>(Object))
			return Actor->GetRootComponent();
		return Ctx.GetSelf() ? Ctx.GetSelf()->GetRootComponent() : nullptr;
	}

	// ---- UFUNCTION 호출 ----

	// "ACharacter::Jump" -> 클래스와 함수. 함수는 그 클래스나 부모가 선언한 것이다.
	const FFunction* ResolveFunction(const FString& Param, UClass** OutClass = nullptr)
	{
		const size_t Split = Param.find("::");
		if (Split == FString::npos)
			return nullptr;
		UClass* Class = FindClass(Param.substr(0, Split));
		if (!Class)
			return nullptr;
		if (OutClass)
			*OutClass = Class;
		const FFunction* Function = Class->FindFunction(Param.substr(Split + 2));
		return Function && Function->HasAnyFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure) ? Function : nullptr;
	}

	// 함수 인자 타입 -> 핀 타입. int32는 Float 핀으로 받아 반올림하고, FRotator는 (Roll, Pitch, Yaw) 벡터 핀으로 받는다.
	EBPPinType ToPinType(EScriptValueType Type)
	{
		switch (Type)
		{
		case EScriptValueType::Bool:    return EBPPinType::Bool;
		case EScriptValueType::String:  return EBPPinType::String;
		case EScriptValueType::Vector:
		case EScriptValueType::Rotator: return EBPPinType::Vector;
		default:                        return EBPPinType::Float;
		}
	}

	FString ParamPinName(const FFunction& Function, int32 Index)
	{
		FString Name;
		if (Function.ParamNames.Num() == Function.ParamTypes.Num())
			Name = FBlueprintNodeLibrary::ToDisplayName(Function.ParamNames[Index]);
		// 이름이 없거나 고정 핀과 겹치면 번호로 쓴다.
		if (Name.empty() || Name == "Target" || Name == "In" || Name == "Then" || Name == "Return Value")
			Name = "Arg " + std::to_string(Index);
		return Name;
	}

	FScriptValue ToScriptValue(EScriptValueType Type, const FBPValue& Value)
	{
		switch (Type)
		{
		case EScriptValueType::Bool:    return Value.Bool;
		case EScriptValueType::Int:     return static_cast<int32>(std::lround(Value.Float));
		case EScriptValueType::Float:   return Value.Float;
		case EScriptValueType::String:  return Value.String;
		case EScriptValueType::Vector:  return Value.Vector;
		case EScriptValueType::Rotator: return ToRotator(Value.Vector);
		default:                        return {};
		}
	}

	FBPValue FromScriptValue(const FScriptValue& Value)
	{
		if (const bool* V = std::get_if<bool>(&Value)) return FBPValue::MakeBool(*V);
		if (const int32* V = std::get_if<int32>(&Value)) return FBPValue::MakeFloat(static_cast<float>(*V));
		if (const float* V = std::get_if<float>(&Value)) return FBPValue::MakeFloat(*V);
		if (const FString* V = std::get_if<FString>(&Value)) return FBPValue::MakeString(*V);
		if (const FVector* V = std::get_if<FVector>(&Value)) return FBPValue::MakeVector(*V);
		if (const FRotator* V = std::get_if<FRotator>(&Value)) return FBPValue::MakeVector(FromRotator(*V));
		return FBPValue();
	}

	// 함수를 부를 객체. Target이 그 클래스면 그대로, 액터인데 함수가 컴포넌트 것이면 그 액터의 첫 컴포넌트,
	// 컴포넌트인데 함수가 액터 것이면 그 주인. Target을 비우면 Self에서 같은 규칙으로 찾는다.
	UObject* ResolveCallTarget(FBlueprintExecContext& Ctx, const FBPNode& Node, UClass* Class)
	{
		UObject* Object = Ctx.GetObject(Node, "Target");
		if (!Object)
			Object = Ctx.GetSelf();
		if (!Object || !Class)
			return nullptr;
		if (Object->IsA(Class))
			return Object;
		if (AActor* Actor = Cast<AActor>(Object))
		{
			for (UActorComponent* Component : Actor->GetComponents())
				if (Component && Component->IsA(Class))
					return Component;
		}
		if (UActorComponent* Component = Cast<UActorComponent>(Object))
			if (AActor* Owner = Component->GetOwner(); Owner && Owner->IsA(Class))
				return Owner;
		return nullptr;
	}

	// 함수를 부르고 반환값을 낸다. 실패하면 경고를 남기고 false.
	bool CallReflected(FBlueprintExecContext& Ctx, const FBPNode& Node, FBPValue& OutReturn)
	{
		UClass* Class = nullptr;
		const FFunction* Function = ResolveFunction(Node.Param, &Class);
		if (!Function)
		{
			HTR_LOG(Warning, "Blueprint graph: function {} not found", Node.Param);
			return false;
		}
		UObject* Target = ResolveCallTarget(Ctx, Node, Class);
		if (!Target)
		{
			HTR_LOG(Warning, "Blueprint graph: {} needs a {} target", Node.Param, Class->Name);
			return false;
		}
		TArray<FScriptValue> Args;
		for (int32 Index = 0; Index < Function->ParamTypes.Num(); ++Index)
			Args.Add(ToScriptValue(Function->ParamTypes[Index], Ctx.GetInput(Node, ParamPinName(*Function, Index))));
		FScriptValue Return;
		FString Error;
		if (!Function->Invoke(Target, Args, Return, Error))
		{
			HTR_LOG(Warning, "Blueprint graph: {}", Error);
			return false;
		}
		OutReturn = FromScriptValue(Return);
		return true;
	}

	struct FLibrary
	{
		std::vector<std::unique_ptr<FBPNodeDef>> Storage;
		TArray<const FBPNodeDef*> All;
		std::unordered_map<FString, const FBPNodeDef*> ByType;

		FBPNodeDef& Add(const char* Type, const char* Title, const char* Category, EBPNodeKind Kind, std::initializer_list<FBPPinDef> Pins,
			const char* Tooltip = "")
		{
			auto Def = std::make_unique<FBPNodeDef>();
			Def->Type = Type;
			Def->Title = Title;
			Def->Category = Category;
			Def->Kind = Kind;
			Def->Tooltip = Tooltip;
			for (const FBPPinDef& Pin : Pins)
				Def->Pins.Add(Pin);
			FBPNodeDef& Ref = *Def;
			ByType[Type] = Def.get();
			All.Add(Def.get());
			Storage.push_back(std::move(Def));
			return Ref;
		}

		FBPNodeDef& Event(const char* Type, const char* Title, std::initializer_list<FBPPinDef> Outputs, const char* Tooltip)
		{
			FBPNodeDef& Def = Add(Type, Title, "Events", EBPNodeKind::Event, { ExecOut() }, Tooltip);
			for (const FBPPinDef& Pin : Outputs)
				Def.Pins.Add(Pin);
			Def.bEventGraphOnly = true;
			Def.bUnique = true;
			Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node) { Ctx.Continue(Node, "Then"); };
			return Def;
		}

		// 실행 핀 In/Then이 있고 Body를 실행한 뒤 Then으로 넘어가는 노드
		FBPNodeDef& Action(const char* Type, const char* Title, const char* Category, std::initializer_list<FBPPinDef> Inputs,
			std::function<void(FBlueprintExecContext&, const FBPNode&)> Body, const char* Tooltip = "")
		{
			FBPNodeDef& Def = Add(Type, Title, Category, EBPNodeKind::Function, { ExecIn(), ExecOut() }, Tooltip);
			for (const FBPPinDef& Pin : Inputs)
				Def.Pins.Add(Pin);
			Def.Execute = [Body = std::move(Body)](FBlueprintExecContext& Ctx, const FBPNode& Node)
			{
				Body(Ctx, Node);
				if (!Ctx.HasFailed())
					Ctx.Continue(Node, "Then");
			};
			return Def;
		}

		// 출력 하나("Return Value")를 내는 순수 노드
		FBPNodeDef& Pure(const char* Type, const char* Title, const char* Category, std::initializer_list<FBPPinDef> Inputs, EBPPinType ReturnType,
			std::function<FBPValue(FBlueprintExecContext&, const FBPNode&)> Body, const char* Tooltip = "")
		{
			FBPNodeDef& Def = Add(Type, Title, Category, EBPNodeKind::Pure, Inputs, Tooltip);
			Def.Pins.Add(OutPin("Return Value", ReturnType));
			Def.Evaluate = [Body = std::move(Body)](FBlueprintExecContext& Ctx, const FBPNode& Node, const FBPPin&) { return Body(Ctx, Node); };
			return Def;
		}

		FLibrary()
		{
			using T = EBPPinType;

			// ---- 이벤트 ----
			Event("EventBeginPlay", "Event BeginPlay", {}, "Called when play begins for this actor");
			Event("EventTick", "Event Tick", { OutPin("Delta Seconds", T::Float) }, "Called every frame");
			Event("EventEndPlay", "Event EndPlay", {}, "Called when this actor is removed from play");
			Event("EventActorBeginOverlap", "Event ActorBeginOverlap", { OutPin("Other Actor", T::Object) },
				"Called when another actor begins to overlap this actor");
			Event("EventActorEndOverlap", "Event ActorEndOverlap", { OutPin("Other Actor", T::Object) },
				"Called when another actor stops overlapping this actor");
			{
				FBPNodeDef& Def = Add("ConstructionScript", "Construction Script", "Events", EBPNodeKind::Event, { ExecOut() },
					"Runs when this blueprint is spawned or placed, before BeginPlay");
				Def.bConstructionScriptOnly = true;
				Def.bUnique = true;
				Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node) { Ctx.Continue(Node, "Then"); };
			}

			// ---- 흐름 제어 ----
			{
				FBPNodeDef& Def = Add("Branch", "Branch", "Flow Control", EBPNodeKind::Function,
					{ ExecIn(), InBool("Condition", true), ExecOut("True"), ExecOut("False") }, "Runs True or False depending on Condition");
				Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					Ctx.Continue(Node, Ctx.GetBool(Node, "Condition") ? "True" : "False");
				};
			}
			{
				FBPNodeDef& Def = Add("Sequence", "Sequence", "Flow Control", EBPNodeKind::Function,
					{ ExecIn(), ExecOut("Then 0"), ExecOut("Then 1"), ExecOut("Then 2") }, "Runs each output in order");
				Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					for (const char* Pin : { "Then 0", "Then 1", "Then 2" })
					{
						if (Ctx.HasFailed())
							break;
						Ctx.Continue(Node, Pin);
					}
				};
			}
			{
				FBPNodeDef& Def = Add("ForLoop", "For Loop", "Flow Control", EBPNodeKind::Function,
					{ ExecIn(), InFloat("First Index", 0.0f), InFloat("Last Index", 3.0f),
					  ExecOut("Loop Body"), OutPin("Index", T::Float), ExecOut("Completed") }, "Runs Loop Body once for each index");
				Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					const int32 First = static_cast<int32>(Ctx.GetFloat(Node, "First Index"));
					const int32 Last = static_cast<int32>(Ctx.GetFloat(Node, "Last Index"));
					for (int32 Index = First; Index <= Last && !Ctx.HasFailed(); ++Index)
					{
						Ctx.SetOutput(Node, "Index", FBPValue::MakeFloat(static_cast<float>(Index)));
						Ctx.Continue(Node, "Loop Body");
					}
					if (!Ctx.HasFailed())
						Ctx.Continue(Node, "Completed");
				};
			}
			{
				FBPNodeDef& Def = Add("Delay", "Delay", "Flow Control", EBPNodeKind::Function,
					{ ExecIn(), InFloat("Duration", 1.0f), ExecOut("Completed") },
					"Continues from Completed after Duration seconds. Calling it again while waiting is ignored");
				Def.bEventGraphOnly = true;
				Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					Ctx.ScheduleDelay(Node, "Completed", Ctx.GetFloat(Node, "Duration"));
				};
			}

			// ---- 유틸리티 ----
			Action("PrintString", "Print String", "Utilities", { InString("In String", "Hello") },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					const FString Owner = Ctx.GetSelf() ? Ctx.GetSelf()->GetName() : FString("?");
					HTR_LOG(Info, "[{}] {}", Owner, Ctx.GetString(Node, "In String"));
				}, "Prints a message to the Output Log");

			// ---- 액터 ----
			Pure("Self", "Self", "Actor", {}, T::Object,
				[](FBlueprintExecContext& Ctx, const FBPNode&) { return FBPValue::MakeObject(Ctx.GetSelf()); },
				"A reference to this actor");
			Pure("GetActorLocation", "Get Actor Location", "Actor", { TargetPin() }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					AActor* Actor = TargetActor(Ctx, Node);
					return FBPValue::MakeVector(Actor ? Actor->GetActorLocation() : FVector(0.0f, 0.0f, 0.0f));
				});
			Pure("GetActorRotation", "Get Actor Rotation", "Actor", { TargetPin() }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					AActor* Actor = TargetActor(Ctx, Node);
					return FBPValue::MakeVector(Actor ? FromRotator(Actor->GetActorRotation()) : FVector(0.0f, 0.0f, 0.0f));
				}, "Rotation as (Roll, Pitch, Yaw) in degrees");
			Action("SetActorLocation", "Set Actor Location", "Actor", { TargetPin(), InVector("New Location", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (AActor* Actor = TargetActor(Ctx, Node); Actor && Actor->GetRootComponent())
						Actor->GetRootComponent()->SetWorldLocation(Ctx.GetVector(Node, "New Location"), false);
				});
			Action("AddActorWorldOffset", "Add Actor World Offset", "Actor", { TargetPin(), InVector("Delta Location", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (AActor* Actor = TargetActor(Ctx, Node); Actor && Actor->GetRootComponent())
						Actor->GetRootComponent()->SetWorldLocation(VAdd(Actor->GetActorLocation(), Ctx.GetVector(Node, "Delta Location")), false);
				});
			Action("SetActorRotation", "Set Actor Rotation", "Actor", { TargetPin(), InVector("New Rotation", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (AActor* Actor = TargetActor(Ctx, Node); Actor && Actor->GetRootComponent())
						Actor->GetRootComponent()->SetWorldRotation(ToRotator(Ctx.GetVector(Node, "New Rotation")), false);
				}, "Rotation as (Roll, Pitch, Yaw) in degrees");
			Action("AddActorWorldRotation", "Add Actor World Rotation", "Actor", { TargetPin(), InVector("Delta Rotation", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (AActor* Actor = TargetActor(Ctx, Node); Actor && Actor->GetRootComponent())
						Actor->GetRootComponent()->SetWorldRotation(
							ToRotator(VAdd(FromRotator(Actor->GetActorRotation()), Ctx.GetVector(Node, "Delta Rotation"))), false);
				}, "Rotation as (Roll, Pitch, Yaw) in degrees");
			Action("SetActorScale3D", "Set Actor Scale 3D", "Actor", { TargetPin(), InVector("New Scale 3D", FVector(1.0f, 1.0f, 1.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (AActor* Actor = TargetActor(Ctx, Node); Actor && Actor->GetRootComponent())
						Actor->GetRootComponent()->SetWorldScale3D(Ctx.GetVector(Node, "New Scale 3D"));
				});
			Action("SetActorHiddenInGame", "Set Actor Hidden In Game", "Actor", { TargetPin(), InBool("New Hidden", true) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (AActor* Actor = TargetActor(Ctx, Node); Actor && Actor->GetRootComponent())
						Actor->GetRootComponent()->SetHiddenInGame(Ctx.GetBool(Node, "New Hidden"), true);
				});
			{
				FBPNodeDef& Def = Add("DestroyActor", "Destroy Actor", "Actor", EBPNodeKind::Function, { ExecIn(), TargetPin() },
					"Destroys the target actor. Nothing after this node runs when the target is Self");
				Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					AActor* Actor = TargetActor(Ctx, Node);
					if (!Actor)
						return;
					const bool bSelf = Actor == Ctx.GetSelf();
					Actor->Destroy();
					if (bSelf)
						Ctx.Halt();
				};
			}

			// ---- C++ UFUNCTION 호출 (핀은 GetPinDefs가 함수를 보고 만든다) ----
			{
				FBPNodeDef& Def = Add("CallFunction", "Call Function", "Functions", EBPNodeKind::Function, {});
				Def.Execute = [](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					FBPValue Return;
					if (CallReflected(Ctx, Node, Return))
						Ctx.SetOutput(Node, "Return Value", Return);
					Ctx.Continue(Node, "Then");
				};
			}
			{
				FBPNodeDef& Def = Add("CallPureFunction", "Call Pure Function", "Functions", EBPNodeKind::Pure, {});
				Def.Evaluate = [](FBlueprintExecContext& Ctx, const FBPNode& Node, const FBPPin& Pin)
				{
					FBPValue Return;
					if (!CallReflected(Ctx, Node, Return))
						Return = DefaultOf(Pin.Type);
					return Return;
				};
			}

			// ---- 컴포넌트 ----
			{
				// Param = 컴포넌트 이름. My Blueprint의 Components 변수를 그래프로 끌어오면 만들어진다.
				FBPNodeDef& Def = Add("GetComponent", "Get Component", "Variables", EBPNodeKind::Pure, { OutPin("Component", T::Object) });
				Def.Evaluate = [](FBlueprintExecContext& Ctx, const FBPNode& Node, const FBPPin&)
				{
					AActor* Self = Ctx.GetSelf();
					return FBPValue::MakeObject(Self ? Self->FindComponentByName(FName(Node.Param)) : nullptr);
				};
			}
			Pure("GetWorldLocation", "Get World Location", "Components", { TargetPin() }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					USceneComponent* Scene = TargetScene(Ctx, Node);
					return FBPValue::MakeVector(Scene ? Scene->GetWorldLocation() : FVector(0.0f, 0.0f, 0.0f));
				});
			Pure("GetRelativeLocation", "Get Relative Location", "Components", { TargetPin() }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					USceneComponent* Scene = TargetScene(Ctx, Node);
					return FBPValue::MakeVector(Scene ? Scene->GetRelativeLocation() : FVector(0.0f, 0.0f, 0.0f));
				});
			Action("SetRelativeLocation", "Set Relative Location", "Components", { TargetPin(), InVector("New Location", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (USceneComponent* Scene = TargetScene(Ctx, Node))
						Scene->SetRelativeLocation(Ctx.GetVector(Node, "New Location"));
				});
			Action("AddLocalOffset", "Add Local Offset", "Components", { TargetPin(), InVector("Delta Location", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (USceneComponent* Scene = TargetScene(Ctx, Node))
						Scene->SetRelativeLocation(VAdd(Scene->GetRelativeLocation(), Ctx.GetVector(Node, "Delta Location")));
				});
			Action("SetRelativeRotation", "Set Relative Rotation", "Components", { TargetPin(), InVector("New Rotation", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (USceneComponent* Scene = TargetScene(Ctx, Node))
						Scene->SetRelativeRotation(ToRotator(Ctx.GetVector(Node, "New Rotation")));
				}, "Rotation as (Roll, Pitch, Yaw) in degrees");
			Action("AddLocalRotation", "Add Local Rotation", "Components", { TargetPin(), InVector("Delta Rotation", FVector(0.0f, 0.0f, 0.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (USceneComponent* Scene = TargetScene(Ctx, Node))
						Scene->SetRelativeRotation(ToRotator(VAdd(FromRotator(Scene->GetRelativeRotation()), Ctx.GetVector(Node, "Delta Rotation"))));
				}, "Rotation as (Roll, Pitch, Yaw) in degrees");
			Action("SetRelativeScale3D", "Set Relative Scale 3D", "Components", { TargetPin(), InVector("New Scale 3D", FVector(1.0f, 1.0f, 1.0f)) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (USceneComponent* Scene = TargetScene(Ctx, Node))
						Scene->SetRelativeScale3D(Ctx.GetVector(Node, "New Scale 3D"));
				});
			Action("SetVisibility", "Set Visibility", "Components", { TargetPin(), InBool("New Visibility", true) },
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					if (USceneComponent* Scene = TargetScene(Ctx, Node))
						Scene->SetVisibility(Ctx.GetBool(Node, "New Visibility"), true);
				});

			// ---- 수학 ----
			auto FloatOp = [this](const char* Type, const char* Title, float (*Op)(float, float))
			{
				Pure(Type, Title, "Math|Float", { InFloat("A", 0.0f), InFloat("B", 0.0f) }, EBPPinType::Float,
					[Op](FBlueprintExecContext& Ctx, const FBPNode& Node)
					{
						return FBPValue::MakeFloat(Op(Ctx.GetFloat(Node, "A"), Ctx.GetFloat(Node, "B")));
					});
			};
			FloatOp("AddFloat", "+ (Float)", [](float A, float B) { return A + B; });
			FloatOp("SubtractFloat", "- (Float)", [](float A, float B) { return A - B; });
			FloatOp("MultiplyFloat", "x (Float)", [](float A, float B) { return A * B; });
			FloatOp("DivideFloat", "/ (Float)", [](float A, float B) { return B != 0.0f ? A / B : 0.0f; });
			FloatOp("MinFloat", "Min (Float)", [](float A, float B) { return A < B ? A : B; });
			FloatOp("MaxFloat", "Max (Float)", [](float A, float B) { return A > B ? A : B; });

			auto CompareOp = [this](const char* Type, const char* Title, bool (*Op)(float, float))
			{
				Pure(Type, Title, "Math|Float", { InFloat("A", 0.0f), InFloat("B", 0.0f) }, EBPPinType::Bool,
					[Op](FBlueprintExecContext& Ctx, const FBPNode& Node)
					{
						return FBPValue::MakeBool(Op(Ctx.GetFloat(Node, "A"), Ctx.GetFloat(Node, "B")));
					});
			};
			CompareOp("GreaterFloat", "> (Float)", [](float A, float B) { return A > B; });
			CompareOp("LessFloat", "< (Float)", [](float A, float B) { return A < B; });
			CompareOp("EqualFloat", "== (Float)", [](float A, float B) { return std::fabs(A - B) <= 1e-4f; });

			Pure("Sin", "Sin (Radians)", "Math|Trig", { InFloat("Value", 0.0f) }, T::Float,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeFloat(std::sin(Ctx.GetFloat(Node, "Value"))); });
			Pure("Cos", "Cos (Radians)", "Math|Trig", { InFloat("Value", 0.0f) }, T::Float,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeFloat(std::cos(Ctx.GetFloat(Node, "Value"))); });
			Pure("Lerp", "Lerp", "Math|Float", { InFloat("A", 0.0f), InFloat("B", 1.0f), InFloat("Alpha", 0.5f) }, T::Float,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					const float A = Ctx.GetFloat(Node, "A");
					return FBPValue::MakeFloat(A + (Ctx.GetFloat(Node, "B") - A) * Ctx.GetFloat(Node, "Alpha"));
				});
			Pure("RandomFloatInRange", "Random Float in Range", "Math|Random", { InFloat("Min", 0.0f), InFloat("Max", 1.0f) }, T::Float,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					static std::mt19937 Engine{ std::random_device{}() };
					const float Min = Ctx.GetFloat(Node, "Min");
					const float Max = Ctx.GetFloat(Node, "Max");
					std::uniform_real_distribution<float> Dist(Min < Max ? Min : Max, Min < Max ? Max : Min);
					return FBPValue::MakeFloat(Dist(Engine));
				});
			Pure("GetGameTimeInSeconds", "Get Game Time in Seconds", "Utilities|Time", {}, T::Float,
				[](FBlueprintExecContext& Ctx, const FBPNode&)
				{
					UWorld* World = Ctx.GetSelf() ? Ctx.GetSelf()->GetWorld() : nullptr;
					return FBPValue::MakeFloat(World ? World->GetTimeSeconds() : 0.0f);
				});

			Pure("MakeVector", "Make Vector", "Math|Vector", { InFloat("X", 0.0f), InFloat("Y", 0.0f), InFloat("Z", 0.0f) }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					return FBPValue::MakeVector(FVector(Ctx.GetFloat(Node, "X"), Ctx.GetFloat(Node, "Y"), Ctx.GetFloat(Node, "Z")));
				});
			{
				FBPNodeDef& Def = Add("BreakVector", "Break Vector", "Math|Vector", EBPNodeKind::Pure,
					{ InVector("In Vec", FVector(0.0f, 0.0f, 0.0f)), OutPin("X", T::Float), OutPin("Y", T::Float), OutPin("Z", T::Float) });
				Def.Evaluate = [](FBlueprintExecContext& Ctx, const FBPNode& Node, const FBPPin& Pin)
				{
					const FVector V = Ctx.GetVector(Node, "In Vec");
					return FBPValue::MakeFloat(Pin.Name == "X" ? V.X : Pin.Name == "Y" ? V.Y : V.Z);
				};
			}
			Pure("AddVector", "+ (Vector)", "Math|Vector", { InVector("A", FVector(0.0f, 0.0f, 0.0f)), InVector("B", FVector(0.0f, 0.0f, 0.0f)) }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeVector(VAdd(Ctx.GetVector(Node, "A"), Ctx.GetVector(Node, "B"))); });
			Pure("SubtractVector", "- (Vector)", "Math|Vector", { InVector("A", FVector(0.0f, 0.0f, 0.0f)), InVector("B", FVector(0.0f, 0.0f, 0.0f)) }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeVector(VSub(Ctx.GetVector(Node, "A"), Ctx.GetVector(Node, "B"))); });
			Pure("MultiplyVectorFloat", "x (Vector, Float)", "Math|Vector", { InVector("A", FVector(0.0f, 0.0f, 0.0f)), InFloat("B", 1.0f) }, T::Vector,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeVector(VScale(Ctx.GetVector(Node, "A"), Ctx.GetFloat(Node, "B"))); });

			Pure("And", "AND", "Math|Boolean", { InBool("A", false), InBool("B", false) }, T::Bool,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeBool(Ctx.GetBool(Node, "A") && Ctx.GetBool(Node, "B")); });
			Pure("Or", "OR", "Math|Boolean", { InBool("A", false), InBool("B", false) }, T::Bool,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeBool(Ctx.GetBool(Node, "A") || Ctx.GetBool(Node, "B")); });
			Pure("Not", "NOT", "Math|Boolean", { InBool("A", false) }, T::Bool,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeBool(!Ctx.GetBool(Node, "A")); });
			Pure("IsValid", "Is Valid", "Utilities", { InPin("Object", T::Object) }, T::Bool,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeBool(Ctx.GetObject(Node, "Object") != nullptr); });

			// ---- 문자열 ----
			Pure("Append", "Append", "Utilities|String", { InString("A", ""), InString("B", "") }, T::String,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeString(Ctx.GetString(Node, "A") + Ctx.GetString(Node, "B")); });
			Pure("FloatToString", "To String (Float)", "Utilities|String", { InFloat("In Float", 0.0f) }, T::String,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeString(FloatToString(Ctx.GetFloat(Node, "In Float"))); });
			Pure("BoolToString", "To String (Boolean)", "Utilities|String", { InBool("In Bool", false) }, T::String,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node) { return FBPValue::MakeString(Ctx.GetBool(Node, "In Bool") ? "true" : "false"); });
			Pure("VectorToString", "To String (Vector)", "Utilities|String", { InVector("In Vec", FVector(0.0f, 0.0f, 0.0f)) }, T::String,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					const FVector V = Ctx.GetVector(Node, "In Vec");
					return FBPValue::MakeString("X=" + FloatToString(V.X) + " Y=" + FloatToString(V.Y) + " Z=" + FloatToString(V.Z));
				});
			Pure("ObjectToString", "Get Display Name", "Utilities|String", { InPin("Object", T::Object) }, T::String,
				[](FBlueprintExecContext& Ctx, const FBPNode& Node)
				{
					UObject* Object = Ctx.GetObject(Node, "Object");
					return FBPValue::MakeString(Object ? Object->GetName() : FString("None"));
				});
		}
	};

	FLibrary& GetLibrary()
	{
		static FLibrary Library;
		return Library;
	}
}

const FBPNodeDef* FBlueprintNodeLibrary::Find(const FString& Type)
{
	auto It = GetLibrary().ByType.find(Type);
	return It != GetLibrary().ByType.end() ? It->second : nullptr;
}

const TArray<const FBPNodeDef*>& FBlueprintNodeLibrary::GetAll()
{
	return GetLibrary().All;
}

bool FBlueprintNodeLibrary::GetPinDefs(const FString& Type, const FString& Param, TArray<FBPPinDef>& OutPins)
{
	OutPins.Empty();
	const FBPNodeDef* Def = Find(Type);
	if (!Def)
		return false;
	if (Type != "CallFunction" && Type != "CallPureFunction")
	{
		OutPins = Def->Pins;
		return true;
	}

	const FFunction* Function = ResolveFunction(Param);
	if (!Function)
		return false;
	if (Type == "CallFunction")
		OutPins.Add(ExecIn());
	OutPins.Add(TargetPin());
	for (int32 Index = 0; Index < Function->ParamTypes.Num(); ++Index)
	{
		const EBPPinType PinType = ToPinType(Function->ParamTypes[Index]);
		OutPins.Add({ ParamPinName(*Function, Index), PinType, false, DefaultOf(PinType) });
	}
	if (Type == "CallFunction")
		OutPins.Add(ExecOut());
	if (Function->ReturnsValue())
		OutPins.Add(OutPin("Return Value", ToPinType(Function->ReturnType)));
	return true;
}

FString FBlueprintNodeLibrary::GetTitle(const FBPNode& Node)
{
	if (Node.Type == "GetComponent")
		return Node.Param;
	if (Node.Type == "CallFunction" || Node.Type == "CallPureFunction")
	{
		const size_t Split = Node.Param.find("::");
		return ToDisplayName(Split == FString::npos ? Node.Param : Node.Param.substr(Split + 2));
	}
	const FBPNodeDef* Def = Find(Node.Type);
	return Def ? Def->Title : Node.Type;
}

FString FBlueprintNodeLibrary::GetSubtitle(const FBPNode& Node)
{
	if (Node.Type != "CallFunction" && Node.Type != "CallPureFunction")
		return FString();
	const size_t Split = Node.Param.find("::");
	if (Split == FString::npos)
		return FString();
	return "Target is " + ToDisplayName(Node.Param.substr(0, Split), true);
}

void FBlueprintNodeLibrary::GetCallableFunctions(TArray<FBPCallableFunction>& OutFunctions)
{
	OutFunctions.Empty();
	TArray<UClass*> Classes;
	GetDerivedClasses(UObject::StaticClass(), Classes, true);
	for (UClass* Class : Classes)
	{
		// BP 클래스는 함수를 선언하지 않는다 (부모의 함수가 FindFunction으로 보일 뿐이다).
		if (!Class || FBlueprint::IsBlueprintClass(Class))
			continue;
		for (const FFunction& Function : Class->GetFunctions())
		{
			const bool bPure = Function.HasAnyFlags(FUNC_BlueprintPure) && Function.ReturnsValue();
			if (!bPure && !Function.HasAnyFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
				continue;
			OutFunctions.Add(FBPCallableFunction{ Class, &Function, bPure ? "CallPureFunction" : "CallFunction", Class->Name + "::" + Function.Name });
		}
	}
}

FString FBlueprintNodeLibrary::ToDisplayName(const FString& Name, bool bStripClassPrefix)
{
	FString Source = Name;
	// "ACharacter", "UAudioComponent" -> 앞의 A/U 접두사를 뗀다
	if (bStripClassPrefix && Source.size() > 1 && (Source[0] == 'A' || Source[0] == 'U') && std::isupper(static_cast<unsigned char>(Source[1])))
		Source = Source.substr(1);
	// bool 인자의 b 접두사 ("bReset" -> "Reset")
	if (Source.size() > 1 && Source[0] == 'b' && std::isupper(static_cast<unsigned char>(Source[1])))
		Source = Source.substr(1);

	FString Result;
	for (size_t Index = 0; Index < Source.size(); ++Index)
	{
		const unsigned char C = static_cast<unsigned char>(Source[Index]);
		if (Index > 0 && std::isupper(C))
		{
			const unsigned char Prev = static_cast<unsigned char>(Source[Index - 1]);
			const bool bNextLower = Index + 1 < Source.size() && std::islower(static_cast<unsigned char>(Source[Index + 1]));
			// "AddMovement" -> "Add Movement", "HTTPRequest" -> "HTTP Request"
			if (std::islower(Prev) || std::isdigit(Prev) || (std::isupper(Prev) && bNextLower))
				Result += ' ';
		}
		if (C == '_')
		{
			Result += ' ';
			continue;
		}
		Result += static_cast<char>(C);
	}
	return Result;
}

// ---------------- 실행 ----------------

namespace
{
	constexpr int32 MaxDepth = 256;        // 순수 노드·실행 핀을 따라 들어가는 깊이
	constexpr int32 MaxSteps = 100000;     // 이벤트 한 번에 실행하는 노드 수 (무한 루프 막기)

	struct FPendingDelay
	{
		std::shared_ptr<const FBPGraphSet> Owner;
		const FBPGraph* Graph = nullptr;
		int32 NodeId = 0;
		FString ExecPin;
		float Remaining = 0.0f;
		TArray<FBlueprintExecContext::FStoredOutput> Outputs;
	};

	std::unordered_map<const AActor*, TArray<FPendingDelay>>& GetDelays()
	{
		static std::unordered_map<const AActor*, TArray<FPendingDelay>> Delays;
		return Delays;
	}

	bool HasNodeOfType(const FBPGraph& Graph, const FString& Type)
	{
		for (const FBPNode& Node : Graph.Nodes)
			if (Node.Type == Type)
				return true;
		return false;
	}

	const FBPNode* FindNodeOfType(const FBPGraph& Graph, const FString& Type)
	{
		for (const FBPNode& Node : Graph.Nodes)
			if (Node.Type == Type)
				return &Node;
		return nullptr;
	}

	// 이벤트 하나를 실행한다. SetOutputs로 이벤트 출력 핀 값을 채운다.
	void RunEvent(AActor* Actor, const FString& EventType, const std::function<void(FBlueprintExecContext&, const FBPNode&)>& SetOutputs = nullptr)
	{
		if (!Actor)
			return;
		std::shared_ptr<const FBPGraphSet> Owner = FBlueprintGraphRuntime::FindEventOwner(Actor->GetClass(), EventType);
		if (!Owner)
			return;
		const FBPNode* Event = FindNodeOfType(Owner->EventGraph, EventType);
		if (!Event)
			return;
		FBlueprintExecContext Ctx(Actor, Owner->EventGraph, Owner);
		if (SetOutputs)
			SetOutputs(Ctx, *Event);
		Ctx.ExecuteNode(*Event);
	}
}

void FBlueprintExecContext::Halt()
{
	bFailed = true;
}

FBPValue FBlueprintExecContext::GetInput(const FBPNode& Node, const FString& PinName)
{
	const FBPPin* Pin = Node.FindPin(PinName, false);
	if (!Pin)
		return FBPValue();

	for (const FBPLink& Link : Graph.Links)
	{
		if (Link.ToPin != Pin->Id)
			continue;
		const FBPNode* FromNode = nullptr;
		const FBPPin* FromPin = Graph.FindPin(Link.FromPin, &FromNode);
		if (!FromPin || !FromNode)
			break;

		const FBPNodeDef* Def = FBlueprintNodeLibrary::Find(FromNode->Type);
		if (Def && Def->Kind == EBPNodeKind::Pure && Def->Evaluate)
		{
			if (bFailed || ++Depth > MaxDepth)
			{
				--Depth;
				if (!bFailed)
					HTR_LOG(Warning, "Blueprint graph: pure node chain too deep (loop?) in {}", Graph.Name);
				bFailed = true;
				return DefaultOf(Pin->Type);
			}
			FBPValue Value = Def->Evaluate(*this, *FromNode, *FromPin);
			--Depth;
			return Value;
		}
		// 실행 노드의 출력: 그 노드가 실행될 때 남긴 값
		for (const FStoredOutput& Output : Outputs)
			if (Output.PinId == FromPin->Id)
				return Output.Value;
		return DefaultOf(Pin->Type);
	}
	return Pin->Default;
}

void FBlueprintExecContext::SetOutput(const FBPNode& Node, const FString& PinName, const FBPValue& Value)
{
	const FBPPin* Pin = Node.FindPin(PinName, true);
	if (!Pin)
		return;
	for (FStoredOutput& Output : Outputs)
		if (Output.PinId == Pin->Id)
		{
			Output.Value = Value;
			return;
		}
	Outputs.Add(FStoredOutput{ Pin->Id, Value });
}

void FBlueprintExecContext::Continue(const FBPNode& Node, const FString& ExecPinName)
{
	const FBPPin* Pin = Node.FindPin(ExecPinName, true);
	if (!Pin || bFailed)
		return;
	for (const FBPLink& Link : Graph.Links)
	{
		if (Link.FromPin != Pin->Id)
			continue;
		const FBPNode* Next = nullptr;
		Graph.FindPin(Link.ToPin, &Next);
		if (!Next)
			continue;
		if (++Depth > MaxDepth)
		{
			HTR_LOG(Warning, "Blueprint graph: execution too deep in {}", Graph.Name);
			bFailed = true;
		}
		else
		{
			ExecuteNode(*Next);
		}
		--Depth;
		break;   // 실행 출력 핀은 선 하나
	}
}

void FBlueprintExecContext::ScheduleDelay(const FBPNode& Node, const FString& ExecPinName, float Seconds)
{
	if (!Self || !Owner)
		return;
	TArray<FPendingDelay>& Delays = GetDelays()[Self];
	for (const FPendingDelay& Delay : Delays)
		if (Delay.Graph == &Graph && Delay.NodeId == Node.Id)
			return;   // 기다리는 중에 다시 들어오면 무시 (UE Delay와 같다)
	Delays.Add(FPendingDelay{ Owner, &Graph, Node.Id, ExecPinName, Seconds > 0.0f ? Seconds : 0.0f, Outputs });
}

void FBlueprintExecContext::ExecuteNode(const FBPNode& Node)
{
	if (bFailed)
		return;
	if (++Steps > MaxSteps)
	{
		HTR_LOG(Warning, "Blueprint graph: more than {} nodes ran in one event of {} (infinite loop?)", MaxSteps, Graph.Name);
		bFailed = true;
		return;
	}
	const FBPNodeDef* Def = FBlueprintNodeLibrary::Find(Node.Type);
	if (Def && Def->Execute)
		Def->Execute(*this, Node);
}

std::shared_ptr<const FBPGraphSet> FBlueprintGraphRuntime::FindEventOwner(const UClass* Class, const FString& EventType)
{
	for (const UClass* It = Class; It; It = It->Super)
		if (std::shared_ptr<const FBPGraphSet> Graphs = FBlueprint::GetGraphs(It); Graphs && HasNodeOfType(Graphs->EventGraph, EventType))
			return Graphs;
	return nullptr;
}

bool FBlueprintGraphRuntime::NeedsTick(const UClass* Class)
{
	return FindEventOwner(Class, "EventTick") || FindEventOwner(Class, "Delay");
}

void FBlueprintGraphRuntime::RunConstructionScript(AActor* Actor)
{
	if (!Actor || (Actor->GetWorld() && Actor->GetWorld()->GetWorldType() == EWorldType::EditorPreview))
		return;
	// 부모 BP의 Construction Script부터 실행한다 (UE와 같다).
	TArray<std::shared_ptr<const FBPGraphSet>> Chain;
	for (const UClass* It = Actor->GetClass(); It; It = It->Super)
		if (std::shared_ptr<const FBPGraphSet> Graphs = FBlueprint::GetGraphs(It))
			Chain.Add(Graphs);
	for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
	{
		const FBPGraph& Graph = Chain[Index]->ConstructionScript;
		if (const FBPNode* Entry = FindNodeOfType(Graph, "ConstructionScript"))
		{
			FBlueprintExecContext Ctx(Actor, Graph, nullptr);
			Ctx.ExecuteNode(*Entry);
		}
	}
}

void FBlueprintGraphRuntime::OnBeginPlay(AActor* Actor)
{
	RunEvent(Actor, "EventBeginPlay");
}

void FBlueprintGraphRuntime::OnTick(AActor* Actor, float DeltaSeconds)
{
	if (!Actor)
		return;
	RunEvent(Actor, "EventTick", [DeltaSeconds](FBlueprintExecContext& Ctx, const FBPNode& Event)
	{
		Ctx.SetOutput(Event, "Delta Seconds", FBPValue::MakeFloat(DeltaSeconds));
	});

	// 끝난 Delay를 이어서 실행한다. 실행하다 새 Delay가 생길 수 있으므로 끝난 것을 먼저 빼낸다.
	auto It = GetDelays().find(Actor);
	if (It == GetDelays().end())
		return;
	TArray<FPendingDelay> Due;
	TArray<FPendingDelay>& Delays = It->second;
	for (int32 Index = Delays.Num() - 1; Index >= 0; --Index)
	{
		Delays[Index].Remaining -= DeltaSeconds;
		if (Delays[Index].Remaining <= 0.0f)
		{
			Due.Add(std::move(Delays[Index]));
			Delays.RemoveAt(Index);
		}
	}
	if (Delays.IsEmpty())
		GetDelays().erase(It);

	for (int32 Index = Due.Num() - 1; Index >= 0; --Index)
	{
		const FPendingDelay& Delay = Due[Index];
		const FBPNode* Node = Delay.Graph->FindNode(Delay.NodeId);
		if (!Node)
			continue;
		FBlueprintExecContext Ctx(Actor, *Delay.Graph, Delay.Owner);
		Ctx.Outputs = Delay.Outputs;
		Ctx.Continue(*Node, Delay.ExecPin);
		if (Ctx.HasFailed())
			break;   // 액터가 지워졌을 수 있다
	}
}

void FBlueprintGraphRuntime::OnEndPlay(AActor* Actor)
{
	RunEvent(Actor, "EventEndPlay");
	GetDelays().erase(Actor);
}

void FBlueprintGraphRuntime::OnActorBeginOverlap(AActor* Actor, AActor* OtherActor)
{
	RunEvent(Actor, "EventActorBeginOverlap", [OtherActor](FBlueprintExecContext& Ctx, const FBPNode& Event)
	{
		Ctx.SetOutput(Event, "Other Actor", FBPValue::MakeObject(OtherActor));
	});
}

void FBlueprintGraphRuntime::OnActorEndOverlap(AActor* Actor, AActor* OtherActor)
{
	RunEvent(Actor, "EventActorEndOverlap", [OtherActor](FBlueprintExecContext& Ctx, const FBPNode& Event)
	{
		Ctx.SetOutput(Event, "Other Actor", FBPValue::MakeObject(OtherActor));
	});
}
