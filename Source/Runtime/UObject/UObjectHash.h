#pragma once

class UObject;
class UClass;

void RegisterClass(UClass* Class);

UClass* FindClass(const FString& Name);

UObject* FindObjectWithOuter(const UObject* outer, const UClass* Class, FName Name);

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIsIncludeDerivedClass = true);

void HashObject(UObject* Obj, const UClass* Class); 
void UnhashObject(UObject* Obj, const UClass* Class);

void GetDerivedClasses(const UClass* ClassToLookFor, TArray<UClass*>& Results, bool bRecursive = true);

// 클래스 미지정 시 전체 생존 객체에서 주소 확인 후 UUID 반환
bool TryGetLiveObjectUUID(const UObject* Object, const UClass* ObjectClass, uint32& OutUUID);
