#include "EnginePCH.h"
#include "Editor/EditorUI/ComponentTypeInfo.h"

#include <cctype>
#include <cstring>

#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

namespace
{
	constexpr ImU32 IconWhite = IM_COL32(215, 215, 215, 255);
	constexpr ImU32 IconMesh = IM_COL32(63, 196, 214, 255);
	constexpr ImU32 IconLight = IM_COL32(245, 210, 90, 255);
	constexpr ImU32 IconCollision = IM_COL32(120, 200, 120, 255);
	constexpr ImU32 IconMovement = IM_COL32(150, 170, 255, 255);

	using ComponentTypeInfo::FEntry;

	const FEntry Entries[] = {
		{ "USceneComponent",                "Common",    EEditorIcon::World,          IconWhite },
		{ "UStaticMeshComponent",           "Common",    EEditorIcon::Local,          IconMesh },
		{ "USkeletalMeshComponent",         "Common",    EEditorIcon::SkeletalMesh,   IM_COL32(236, 92, 178, 255) },
		{ "UAudioComponent",                "Common",    EEditorIcon::Sound,          IM_COL32(130, 130, 255, 255) },
		{ "UCameraComponent",               "Common",    EEditorIcon::Camera,         IconWhite },
		{ "USpringArmComponent",            "Common",    EEditorIcon::Translate,      IconWhite },
		{ "UTextRenderComponent",           "Common",    EEditorIcon::Font,           IconWhite },
		{ "UDirectionalLightComponent",     "Lights",    EEditorIcon::Lit,            IconLight },
		{ "UPointLightComponent",           "Lights",    EEditorIcon::Lit,            IconLight },
		{ "USpotLightComponent",            "Lights",    EEditorIcon::Lit,            IconLight },
		{ "URectLightComponent",            "Lights",    EEditorIcon::Lit,            IconLight },
		{ "USkyLightComponent",             "Lights",    EEditorIcon::Lit,            IconLight },
		{ "UParticleSystemComponent",       "Effects",   EEditorIcon::Particle,       IconWhite },
		{ "UParticleSubUVComponent",        "Effects",   EEditorIcon::Particle,       IconWhite },
		{ "UDecalComponent",                "Effects",   EEditorIcon::ShowDecal,      IconWhite },
		{ "UExponentialHeightFogComponent", "Effects",   EEditorIcon::ShowFog,        IconWhite },
		{ "UBillboardComponent",            "Effects",   EEditorIcon::Texture,        IconWhite },
		{ "UBoxComponent",                  "Collision", EEditorIcon::ShowPrimitives, IconCollision },
		{ "USphereComponent",               "Collision", EEditorIcon::ShowPrimitives, IconCollision },
		{ "UCapsuleComponent",              "Collision", EEditorIcon::ShowPrimitives, IconCollision },
		{ "UCharacterMovementComponent",    "Movement",  EEditorIcon::Translate,      IconMovement },
		{ "UProjectileMovementComponent",   "Movement",  EEditorIcon::Translate,      IconMovement },
		{ "URotatingMovementComponent",     "Movement",  EEditorIcon::Rotate,         IconMovement },
	};
	const char* const Categories[] = { "Common", "Lights", "Effects", "Collision", "Movement", "Other" };

	// 액터 아이콘을 고를 때 이 종류는 액터를 대표하지 못한다 (빈 루트, 충돌 도형, 편집용 빌보드).
	bool IsWeakIconEntry(const FEntry& Entry)
	{
		return std::strcmp(Entry.ClassName, "USceneComponent") == 0 ||
			std::strcmp(Entry.ClassName, "UBillboardComponent") == 0 ||
			std::strcmp(Entry.Category, "Collision") == 0 ||
			std::strcmp(Entry.Category, "Movement") == 0;
	}

	const FEntry* FindEntryForClass(const UClass* Class)
	{
		for (const UStruct* It = Class; It; It = It->GetSuperStruct())
			if (const FEntry* Entry = ComponentTypeInfo::FindExactEntry(It->Name))
				return Entry;
		return nullptr;
	}
}

const FEntry* ComponentTypeInfo::GetEntries(int32& OutCount)
{
	OutCount = static_cast<int32>(sizeof(Entries) / sizeof(Entries[0]));
	return Entries;
}

const char* const* ComponentTypeInfo::GetCategories(int32& OutCount)
{
	OutCount = static_cast<int32>(sizeof(Categories) / sizeof(Categories[0]));
	return Categories;
}

const FEntry* ComponentTypeInfo::FindExactEntry(const FString& ClassName)
{
	for (const FEntry& Entry : Entries)
		if (ClassName == Entry.ClassName) return &Entry;
	return nullptr;
}

bool ComponentTypeInfo::IsAbstractComponentClass(const FString& ClassName)
{
	static const char* const Names[] = {
		"UActorComponent", "UPrimitiveComponent", "UMeshComponent", "USkinnedMeshComponent", "UShapeComponent",
		"ULightComponentBase", "ULightComponent", "ULocalLightComponent", "UMovementComponent", "UPawnMovementComponent",
	};
	for (const char* Name : Names)
		if (ClassName == Name) return true;
	return false;
}

void ComponentTypeInfo::GetComponentIcon(const UClass* Class, EEditorIcon& OutIcon, ImU32& OutColor)
{
	if (const FEntry* Entry = FindEntryForClass(Class))
	{
		OutIcon = Entry->Icon;
		OutColor = Entry->Color;
		return;
	}
	const bool bScene = Class && Class->IsChildOf(USceneComponent::StaticClass());
	OutIcon = bScene ? EEditorIcon::World : EEditorIcon::Settings;
	OutColor = IconWhite;
}

void ComponentTypeInfo::GetActorIcon(const AActor* Actor, EEditorIcon& OutIcon, ImU32& OutColor)
{
	OutIcon = EEditorIcon::World;
	OutColor = IconWhite;
	if (!Actor)
		return;

	// 루트부터 보고, 루트가 빈 씬 컴포넌트나 충돌 도형이면 액터를 더 잘 드러내는 컴포넌트를 찾는다.
	const USceneComponent* Root = Actor->GetRootComponent();
	if (const FEntry* Entry = Root ? FindEntryForClass(Root->GetClass()) : nullptr; Entry && !IsWeakIconEntry(*Entry))
	{
		OutIcon = Entry->Icon;
		OutColor = Entry->Color;
		return;
	}
	for (const UActorComponent* Component : Actor->GetComponents())
	{
		const FEntry* Entry = Component ? FindEntryForClass(Component->GetClass()) : nullptr;
		if (Entry && !IsWeakIconEntry(*Entry))
		{
			OutIcon = Entry->Icon;
			OutColor = Entry->Color;
			return;
		}
	}
	if (Root)
		GetComponentIcon(Root->GetClass(), OutIcon, OutColor);
}

FString ComponentTypeInfo::GetComponentDisplayName(const FString& ClassName)
{
	FString Name = ClassName;
	if (Name.size() > 1 && Name[0] == 'U' && std::isupper(static_cast<unsigned char>(Name[1])))
		Name.erase(0, 1);
	const FString Suffix = "Component";
	if (Name.size() > Suffix.size() && Name.ends_with(Suffix))
		Name.resize(Name.size() - Suffix.size());

	FString Out;
	for (size_t I = 0; I < Name.size(); ++I)
	{
		const unsigned char C = static_cast<unsigned char>(Name[I]);
		const bool bPrevLower = I > 0 && std::islower(static_cast<unsigned char>(Name[I - 1]));
		const bool bNextLower = I + 1 < Name.size() && std::islower(static_cast<unsigned char>(Name[I + 1]));
		const bool bPrevUpper = I > 0 && std::isupper(static_cast<unsigned char>(Name[I - 1]));
		if (I > 0 && std::isupper(C) && (bPrevLower || (bPrevUpper && bNextLower)))
			Out += ' ';
		Out += static_cast<char>(C);
	}
	return Out;
}
