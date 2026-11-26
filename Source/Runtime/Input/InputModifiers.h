#pragma once

#include "Input/InputActionValue.h"

// UE의 UInputModifier. 키의 원시 값을 Action에 넘기기 전에 가공한다.
// 매핑(키 단위)과 Action(합산 후) 양쪽에 붙일 수 있고, 붙인 순서대로 적용된다.
class FInputModifier
{
public:
	virtual ~FInputModifier() = default;
	virtual FInputActionValue ModifyRaw(const FInputActionValue& CurrentValue, float DeltaTime) = 0;
};

// 지정한 축의 부호를 뒤집는다. S, A 키를 -1로 만들 때 쓴다
class FInputModifierNegate : public FInputModifier
{
public:
	FInputModifierNegate(bool bInX = true, bool bInY = true, bool bInZ = true)
		: bX(bInX), bY(bInY), bZ(bInZ) {}

	FInputActionValue ModifyRaw(const FInputActionValue& CurrentValue, float) override
	{
		const FVector& V = CurrentValue.GetRaw();
		return FInputActionValue(CurrentValue.GetValueType(),
			FVector(bX ? -V.X : V.X, bY ? -V.Y : V.Y, bZ ? -V.Z : V.Z));
	}

	bool bX, bY, bZ;
};

enum class EInputAxisSwizzle : uint8
{
	YXZ,	// X와 Y를 바꾼다. 1D 키(W)를 2D의 Y축(앞)으로 보낼 때 쓴다
	ZYX,
	XZY,
	YZX,
	ZXY,
};

// 축 순서를 바꾼다
class FInputModifierSwizzleAxis : public FInputModifier
{
public:
	explicit FInputModifierSwizzleAxis(EInputAxisSwizzle InOrder = EInputAxisSwizzle::YXZ)
		: Order(InOrder) {}

	FInputActionValue ModifyRaw(const FInputActionValue& CurrentValue, float) override
	{
		const FVector& V = CurrentValue.GetRaw();
		FVector Out = V;
		switch (Order)
		{
		case EInputAxisSwizzle::YXZ: Out = FVector(V.Y, V.X, V.Z); break;
		case EInputAxisSwizzle::ZYX: Out = FVector(V.Z, V.Y, V.X); break;
		case EInputAxisSwizzle::XZY: Out = FVector(V.X, V.Z, V.Y); break;
		case EInputAxisSwizzle::YZX: Out = FVector(V.Y, V.Z, V.X); break;
		case EInputAxisSwizzle::ZXY: Out = FVector(V.Z, V.X, V.Y); break;
		}
		return FInputActionValue(CurrentValue.GetValueType(), Out);
	}

	EInputAxisSwizzle Order;
};

// 축별로 곱한다. 마우스 감도처럼 쓴다. bScaleByDeltaTime이면 DeltaTime도 곱한다
class FInputModifierScale : public FInputModifier
{
public:
	explicit FInputModifierScale(const FVector& InScalar = FVector(1.0f, 1.0f, 1.0f), bool bInScaleByDeltaTime = false)
		: Scalar(InScalar), bScaleByDeltaTime(bInScaleByDeltaTime) {}

	FInputActionValue ModifyRaw(const FInputActionValue& CurrentValue, float DeltaTime) override
	{
		const FVector& V = CurrentValue.GetRaw();
		const float T = bScaleByDeltaTime ? DeltaTime : 1.0f;
		return FInputActionValue(CurrentValue.GetValueType(),
			FVector(V.X * Scalar.X * T, V.Y * Scalar.Y * T, V.Z * Scalar.Z * T));
	}

	FVector Scalar;
	bool bScaleByDeltaTime;
};

// 크기가 LowerThreshold보다 작으면 0, 그 위는 0~1로 다시 펴 준다 (방사형 데드존)
class FInputModifierDeadZone : public FInputModifier
{
public:
	FInputModifierDeadZone(float InLower = 0.2f, float InUpper = 1.0f)
		: LowerThreshold(InLower), UpperThreshold(InUpper) {}

	FInputActionValue ModifyRaw(const FInputActionValue& CurrentValue, float) override
	{
		const float Magnitude = CurrentValue.GetMagnitude();
		if (Magnitude <= LowerThreshold || Magnitude <= 0.0f || UpperThreshold <= LowerThreshold)
			return FInputActionValue(CurrentValue.GetValueType(), FVector(0.0f, 0.0f, 0.0f));

		const float Remapped = std::min((Magnitude - LowerThreshold) / (UpperThreshold - LowerThreshold), 1.0f);
		const float Ratio = Remapped / Magnitude;
		const FVector& V = CurrentValue.GetRaw();
		return FInputActionValue(CurrentValue.GetValueType(), FVector(V.X * Ratio, V.Y * Ratio, V.Z * Ratio));
	}

	float LowerThreshold;
	float UpperThreshold;
};
