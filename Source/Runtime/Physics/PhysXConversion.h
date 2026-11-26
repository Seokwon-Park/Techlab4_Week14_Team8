#pragma once

#include "Math/Transform.h"
#include "Physics/PhysicsSettingsEnums.h"
#include "PxPhysicsAPI.h"   // premake: Source/ThirdParty/PhysX/include

// UE 4.27 PhysicsCore/Public/PhysXPublicCore.h와 같은 이름. 축 변환 없이 값을 그대로 옮긴다 (UE도 동일)
inline physx::PxVec3 U2PVector(const FVector& V) { return physx::PxVec3(V.X, V.Y, V.Z); }
inline FVector	  P2UVector(const physx::PxVec3& V) { return FVector(V.x, V.y, V.z); }
inline physx::PxQuat U2PQuat(const FQuat& Q) { return physx::PxQuat(Q.X, Q.Y, Q.Z, Q.W); }
inline FQuat		P2UQuat(const physx::PxQuat& Q) { return FQuat(Q.x, Q.y, Q.z, Q.w); }

// PhysX Transform에는 스케일이 없다. 스케일은 Shape 크기에 미리 곱한다
inline physx::PxTransform U2PTransform(const FTransform& T)
{
	return physx::PxTransform(U2PVector(T.Location), U2PQuat(T.Rotation));
}

inline FTransform P2UTransform(const physx::PxTransform& T)
{
	return FTransform(P2UQuat(T.q), P2UVector(T.p));
}

// UE 4.27 BodySetup.cpp:115 — Sphyl(Z축) → PhysX 캡슐(X축). (1,0,1) 축 180° = X·Z 교환 + Y 반전
inline const physx::PxQuat U2PSphylBasis(physx::PxPi, physx::PxVec3(0.70710678f, 0.f, 0.70710678f));

// UE 4.27 PhysicsGeometry.cpp:173 — 캡슐 Shape의 로컬 포즈
inline physx::PxTransform U2PSphylLocalPose(const FVector& Center, const FRotator& Rotation)
{
	return physx::PxTransform(U2PVector(Center), U2PQuat(Rotation.Quaternion()) * U2PSphylBasis);
}

// 엔진 결합 모드와 PhysX 결합 모드의 값 일치 확인
static_assert(
    static_cast<physx::PxCombineMode::Enum>(EPhysicalMaterialCombineMode::Average) == physx::PxCombineMode::eAVERAGE &&
    static_cast<physx::PxCombineMode::Enum>(EPhysicalMaterialCombineMode::Min) == physx::PxCombineMode::eMIN &&
    static_cast<physx::PxCombineMode::Enum>(EPhysicalMaterialCombineMode::Multiply) == physx::PxCombineMode::eMULTIPLY &&
    static_cast<physx::PxCombineMode::Enum>(EPhysicalMaterialCombineMode::Max) == physx::PxCombineMode::eMAX);

// 공통 번호를 사용하는 PhysX 결합 모드 변환
inline constexpr physx::PxCombineMode::Enum U2PCombineMode(EPhysicalMaterialCombineMode Mode)
{
    if (static_cast<uint8>(Mode) > static_cast<uint8>(EPhysicalMaterialCombineMode::Max))
    {
        return physx::PxCombineMode::eAVERAGE;
    }
    return static_cast<physx::PxCombineMode::Enum>(Mode);
}

// UE 4.27 PhysXPublicCore.cpp:26 참고 — 행 순서의 float 버퍼로 PhysX 열 생성
inline physx::PxMat44 U2PMatrix(const FMatrix& Matrix)
{
    float Values[16];
    std::memcpy(&Values, &Matrix.M, sizeof(Values));
    return physx::PxMat44(Values);
}

// UE 4.27 PhysXPublicCore.cpp:63 — PhysX 열 순서 그대로 행 배열에 복사
inline FMatrix P2UMatrix(const physx::PxMat44& Matrix)
{
    FMatrix Result;
    std::memcpy(&Result.M, &Matrix, sizeof(Result.M));
    return Result;
}

// 엔진 내부 행렬의 4×4 float 배열 확인
static_assert(std::is_same_v<decltype(FMatrix::M), float[4][4]>);

// PhysX 열 벡터의 float 성분과 연속 배치 확인
static_assert(std::is_standard_layout_v<physx::PxVec4>);
static_assert(
    std::is_same_v<decltype(physx::PxVec4::x), float>&&
    std::is_same_v<decltype(physx::PxVec4::y), float>&&
    std::is_same_v<decltype(physx::PxVec4::z), float>&&
    std::is_same_v<decltype(physx::PxVec4::w), float> &&
    sizeof(physx::PxVec4) == 4 * sizeof(float) &&
    offsetof(physx::PxVec4, x) == 0 &&
    offsetof(physx::PxVec4, y) == sizeof(float) &&
    offsetof(physx::PxVec4, z) == 2 * sizeof(float) &&
    offsetof(physx::PxVec4, w) == 3 * sizeof(float));

// PhysX 4×4 행렬의 열 순서와 연속 배치 확인
static_assert(std::is_standard_layout_v<physx::PxMat44>);
static_assert(
    sizeof(physx::PxMat44) == 16 * sizeof(float) &&
    offsetof(physx::PxMat44, column0) == 0 &&
    offsetof(physx::PxMat44, column1) == sizeof(physx::PxVec4) &&
    offsetof(physx::PxMat44, column2) == 2 * sizeof(physx::PxVec4) &&
    offsetof(physx::PxMat44, column3) == 3 * sizeof(physx::PxVec4));
