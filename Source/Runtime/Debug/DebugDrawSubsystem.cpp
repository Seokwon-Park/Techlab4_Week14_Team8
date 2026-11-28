#include "EnginePCH.h"
#include "Debug/DebugDrawSubsystem.h"
#include <algorithm>

namespace
{
	template <class T, class FPredicate>
	void RemoveIf(TArray<T>& Array, FPredicate Predicate)
	{
		auto NewEnd = std::remove_if(Array.begin(), Array.end(), Predicate);
		const int32 Removed = static_cast<int32>(Array.end() - NewEnd);
		for (int32 i = 0; i < Removed; ++i)
			Array.RemoveLast();
	}
}

void FDebugDrawSubsystem::Tick(float DeltaTime)
{
	RemoveIf(Lines, [](const FLine& L) {return !L.bPersistent && L.RemainingLife <= 0.0f;});
	RemoveIf(Triangles, [](const FTriangle& T) {return !T.bPersistent && T.RemainingLife <= 0.0f;});

	for (FLine& L : Lines)
		if (!L.bPersistent) L.RemainingLife -= DeltaTime;
	for (FTriangle& T : Triangles)
		if (!T.bPersistent) T.RemainingLife -= DeltaTime;

	FlagsLastFrame = FlagsThisFrame;
	FlagsThisFrame = 0;
}

void FDebugDrawSubsystem::OnLevelUnload()
{
	Lines.Empty();
	Triangles.Empty();
	PreviewGeometry = nullptr;
}

void FDebugDrawSubsystem::AddLine(const FVector& Start, const FVector& End, const FVector4& Color, bool bPersistent, float LifeTime, EDebugDepthPriority Depth)
{
	Lines.Add({ Start, End, Color, LifeTime > 0.0f ? LifeTime : 0.0f, bPersistent, Depth });
}

void FDebugDrawSubsystem::AddTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color, bool bPersistent, float LifeTime)
{
	Triangles.Add({ A,B,C,Color, LifeTime > 0.0f ? LifeTime : 0.0f, bPersistent });
}

void FDebugDrawSubsystem::FlushPersistent()
{
	RemoveIf(Lines, [](const FLine& L) {return L.bPersistent || L.RemainingLife > 0.0f;});
	RemoveIf(Triangles, [](const FTriangle& T) { return T.bPersistent || T.RemainingLife > 0.0f; });
}

REGISTER_WORLD_SUBSYSTEM(FDebugDrawSubsystem);


