#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Math/Vector.h"

#include <functional>
#include <memory>

class AActor;
class UClass;
class UObject;

// Blueprint 노드 그래프 (UE의 EventGraph / Construction Script).
//
// 그래프는 노드(FBPNode), 핀(FBPPin), 선(FBPLink)으로 된 데이터다. .bp 파일의 "Graphs"에 JSON으로 들어간다.
// 실행은 컴파일하지 않고 그래프를 그대로 따라가며 해석한다 (인터프리터).
//   - 실행 핀(흰 화살표)을 따라 노드를 하나씩 실행한다. 노드가 "다음은 이 실행 핀"을 고른다 (Branch, Sequence).
//   - 데이터 핀은 필요할 때 거꾸로 따라가 값을 구한다. 순수 노드(Make Vector, + 등)는 물어볼 때마다 계산하고,
//     실행 노드의 출력(Event Tick의 Delta Seconds 등)은 그 노드가 실행될 때 저장해 둔 값을 쓴다.
//
// 노드 종류는 FBlueprintNodeLibrary에 이름(예: "PrintString")으로 등록된다. 파일에는 이 이름과
// 입력 핀 기본값만 저장하고, 핀 목록은 불러올 때 라이브러리 정의로 다시 만든다.

enum class EBPPinType : uint8
{
	Exec,
	Bool,
	Float,
	String,
	Vector,
	Object,   // 액터나 컴포넌트 참조
};

struct FBPValue
{
	EBPPinType Type = EBPPinType::Float;
	bool Bool = false;
	float Float = 0.0f;
	FString String;
	FVector Vector = FVector(0.0f, 0.0f, 0.0f);
	UObject* Object = nullptr;

	static FBPValue MakeBool(bool V) { FBPValue R; R.Type = EBPPinType::Bool; R.Bool = V; return R; }
	static FBPValue MakeFloat(float V) { FBPValue R; R.Type = EBPPinType::Float; R.Float = V; return R; }
	static FBPValue MakeString(const FString& V) { FBPValue R; R.Type = EBPPinType::String; R.String = V; return R; }
	static FBPValue MakeVector(const FVector& V) { FBPValue R; R.Type = EBPPinType::Vector; R.Vector = V; return R; }
	static FBPValue MakeObject(UObject* V) { FBPValue R; R.Type = EBPPinType::Object; R.Object = V; return R; }
};

struct FBPPin
{
	int32 Id = 0;
	FString Name;
	EBPPinType Type = EBPPinType::Exec;
	bool bOutput = false;
	FBPValue Default;   // 연결되지 않은 입력 핀의 값 (노드 위에서 고친다)
};

struct FBPNode
{
	int32 Id = 0;
	FString Type;       // 라이브러리 이름 ("PrintString")
	FString Param;      // 노드별 추가 값. "GetComponent"면 컴포넌트 이름
	float X = 0.0f;
	float Y = 0.0f;
	TArray<FBPPin> Pins;

	const FBPPin* FindPin(const FString& Name, bool bOutput) const;
	FBPPin* FindPin(const FString& Name, bool bOutput);
};

struct FBPLink
{
	int32 Id = 0;
	int32 FromPin = 0;   // 출력 핀
	int32 ToPin = 0;     // 입력 핀
};

struct FBPGraph
{
	FString Name;
	TArray<FBPNode> Nodes;
	TArray<FBPLink> Links;
	int32 NextId = 1;

	int32 NewId() { return NextId++; }

	FBPNode* FindNode(int32 Id);
	const FBPNode* FindNode(int32 Id) const;
	// 핀 Id로 핀과 그 노드를 찾는다.
	const FBPPin* FindPin(int32 PinId, const FBPNode** OutNode = nullptr) const;
	bool IsPinLinked(int32 PinId) const;

	// 라이브러리 정의대로 노드를 만들어 넣는다. 모르는 종류면 nullptr.
	FBPNode* AddNode(const FString& Type, float InX, float InY, const FString& Param = FString());
	void RemoveNode(int32 NodeId);
	// 두 핀을 잇는다. 순서는 상관없다. 이을 수 없으면 false.
	bool CanConnect(int32 PinA, int32 PinB, FString* OutReason = nullptr) const;
	bool Connect(int32 PinA, int32 PinB);
	void RemoveLink(int32 LinkId);
	// 출력 데이터 핀 / 입력 실행 핀은 여러 선을 가질 수 있고, 나머지는 하나만 갖는다.
	void BreakPinLinks(int32 PinId);
};

// BP 하나의 그래프 묶음
struct FBPGraphSet
{
	FBPGraph EventGraph;
	FBPGraph ConstructionScript;

	FBPGraphSet();   // 기본 노드 (Event BeginPlay, Event Tick, Construction Script) 를 넣는다
	bool IsEmpty() const;
};

namespace FBlueprintGraphSerializer
{
	// FBPGraphSet <-> JSON 문자열. 읽을 때 모르는 노드 종류는 건너뛰고 로그를 남긴다.
	FString ToJson(const FBPGraphSet& Graphs);
	bool FromJson(const FString& Json, FBPGraphSet& OutGraphs);
}

// ---------------- 노드 라이브러리 ----------------

class FBlueprintExecContext;

struct FBPPinDef
{
	FString Name;
	EBPPinType Type;
	bool bOutput;
	FBPValue Default;
};

enum class EBPNodeKind : uint8
{
	Event,      // 실행이 시작되는 곳 (출력 실행 핀 하나)
	Function,   // 실행 핀이 있는 노드
	Pure,       // 실행 핀 없이 값만 내는 노드
};

struct FBPNodeDef
{
	FString Type;
	FString Title;
	FString Category;
	FString Tooltip;
	EBPNodeKind Kind = EBPNodeKind::Function;
	TArray<FBPPinDef> Pins;
	bool bConstructionScriptOnly = false;   // Construction Script 그래프에만 놓을 수 있다
	bool bEventGraphOnly = false;            // EventGraph에만 놓을 수 있다 (이벤트, Delay)
	bool bUnique = false;                    // 그래프에 하나만 (이벤트)

	// Function/Event: 실행한다. 다음 실행 핀은 Ctx.Continue로 고른다.
	std::function<void(FBlueprintExecContext&, const FBPNode&)> Execute;
	// Pure: 출력 핀 하나의 값을 낸다.
	std::function<FBPValue(FBlueprintExecContext&, const FBPNode&, const FBPPin&)> Evaluate;
};

// C++ UFUNCTION(BlueprintCallable / BlueprintPure)을 부르는 노드.
// 노드 종류는 "CallFunction"(실행 핀 있음) 또는 "CallPureFunction"(값만)이고, Param에 "클래스::함수" 이름을 둔다.
// 핀은 FFunction의 인자 타입·이름으로 만든다: Target(비우면 self) + 인자들 + Return Value.
struct FBPCallableFunction
{
	UClass* Class = nullptr;
	const struct FFunction* Function = nullptr;
	FString Type;    // "CallFunction" / "CallPureFunction"
	FString Param;   // "ACharacter::Jump"
};

namespace FBlueprintNodeLibrary
{
	const FBPNodeDef* Find(const FString& Type);
	const TArray<const FBPNodeDef*>& GetAll();
	// 이 노드의 핀 정의. 보통은 정의(FBPNodeDef::Pins) 그대로이고, 함수 호출 노드는 그 함수를 보고 만든다.
	// 모르는 종류이거나 함수를 찾지 못하면 false.
	bool GetPinDefs(const FString& Type, const FString& Param, TArray<FBPPinDef>& OutPins);
	// 노드 제목. "GetComponent"는 컴포넌트 이름, 함수 호출은 "Add Movement Input"처럼 띄어 쓴 함수 이름이다.
	FString GetTitle(const FBPNode& Node);
	// 제목 아래 작은 글씨 ("Target is Character"). 없으면 빈 문자열.
	FString GetSubtitle(const FBPNode& Node);
	// 노드로 부를 수 있는 모든 UFUNCTION (BlueprintCallable / BlueprintPure). 클래스마다 자기가 선언한 것만 넣는다.
	void GetCallableFunctions(TArray<FBPCallableFunction>& OutFunctions);
	// "AddMovementInput" -> "Add Movement Input", "ACharacter" -> "Character"
	FString ToDisplayName(const FString& Name, bool bStripClassPrefix = false);
}

// ---------------- 실행 ----------------

class FBlueprintExecContext
{
public:
	struct FStoredOutput { int32 PinId; FBPValue Value; };

	// Owner는 Graph가 들어 있는 묶음이다. Delay가 나중에 이어서 실행할 때까지 그래프를 살려 둔다.
	FBlueprintExecContext(AActor* InSelf, const FBPGraph& InGraph, std::shared_ptr<const FBPGraphSet> InOwner)
		: Self(InSelf), Graph(InGraph), Owner(std::move(InOwner)) {}

	AActor* GetSelf() const { return Self; }
	const FBPGraph& GetGraph() const { return Graph; }

	// 입력 핀 값: 선이 있으면 따라가서 구하고, 없으면 핀 기본값
	FBPValue GetInput(const FBPNode& Node, const FString& PinName);
	float GetFloat(const FBPNode& Node, const FString& PinName) { return GetInput(Node, PinName).Float; }
	bool GetBool(const FBPNode& Node, const FString& PinName) { return GetInput(Node, PinName).Bool; }
	FVector GetVector(const FBPNode& Node, const FString& PinName) { return GetInput(Node, PinName).Vector; }
	FString GetString(const FBPNode& Node, const FString& PinName) { return GetInput(Node, PinName).String; }
	UObject* GetObject(const FBPNode& Node, const FString& PinName) { return GetInput(Node, PinName).Object; }

	// 실행 노드가 출력 핀 값을 정한다 (Event Tick의 Delta Seconds 등)
	void SetOutput(const FBPNode& Node, const FString& PinName, const FBPValue& Value);
	// 출력 실행 핀에 이어진 노드를 실행한다
	void Continue(const FBPNode& Node, const FString& ExecPinName);
	// Delay: Seconds 뒤에 이 노드의 ExecPinName부터 이어서 실행한다
	void ScheduleDelay(const FBPNode& Node, const FString& ExecPinName, float Seconds);

	void ExecuteNode(const FBPNode& Node);
	// 실행을 멈췄다: 무한 루프 같은 문제가 생겼거나, Destroy Actor로 자기 자신을 지웠다
	bool HasFailed() const { return bFailed; }
	void Halt();

	TArray<FStoredOutput> Outputs;

private:
	AActor* Self = nullptr;
	const FBPGraph& Graph;
	std::shared_ptr<const FBPGraphSet> Owner;
	int32 Depth = 0;
	int32 Steps = 0;
	bool bFailed = false;
};

// 액터에 BP 그래프를 붙여 실행한다. AActor와 UWorld가 부른다.
namespace FBlueprintGraphRuntime
{
	// 스폰할 때 (BP 기본값을 덮어쓴 직후). 부모 BP부터 Construction Script를 실행한다.
	// Blueprint 에디터 미리보기 월드에서는 실행하지 않는다 (그 액터의 상태가 그대로 BP 기본값으로 저장되기 때문이다).
	void RunConstructionScript(AActor* Actor);

	// 게임 월드에서 이벤트를 부른다. 이 액터의 클래스(또는 가장 가까운 부모 BP)의 EventGraph에 그 이벤트가 있을 때만 실행한다.
	void OnBeginPlay(AActor* Actor);
	void OnTick(AActor* Actor, float DeltaSeconds);
	void OnEndPlay(AActor* Actor);
	void OnActorBeginOverlap(AActor* Actor, AActor* OtherActor);
	void OnActorEndOverlap(AActor* Actor, AActor* OtherActor);

	// 이 클래스의 그래프에 Tick이나 Delay가 있어 매 프레임 불러야 하는지
	bool NeedsTick(const UClass* Class);

	// Class 또는 가장 가까운 부모 BP 중 이 종류의 이벤트 노드를 가진 그래프 묶음. 없으면 nullptr.
	std::shared_ptr<const FBPGraphSet> FindEventOwner(const UClass* Class, const FString& EventType);
}
