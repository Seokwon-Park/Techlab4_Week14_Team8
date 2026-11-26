#pragma once

#include "Core/Types.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"

// UInputAction이 어떤 모양의 값을 내보내는지
enum class EInputActionValueType : uint8
{
	Boolean,	// 눌림 여부 (X만 사용, 0 또는 1)
	Axis1D,		// float (X)
	Axis2D,		// FVector2 (X, Y)
	Axis3D,		// FVector (X, Y, Z)
};

// UE의 FInputActionValue. 내부는 항상 FVector에 담고 ValueType에 맞는 성분만 남긴다.
// 바인딩된 함수는 Value.Get<bool>() / Get<float>() / Get<FVector2>() / Get<FVector>()로 꺼낸다.
struct FInputActionValue
{
	FInputActionValue() = default;

	FInputActionValue(EInputActionValueType InType, const FVector& InValue)
		: Value(InValue), ValueType(InType)
	{
		Truncate();
	}

	explicit FInputActionValue(bool bInValue)
		: Value(bInValue ? 1.0f : 0.0f, 0.0f, 0.0f), ValueType(EInputActionValueType::Boolean) {}
	explicit FInputActionValue(float InValue)
		: Value(InValue, 0.0f, 0.0f), ValueType(EInputActionValueType::Axis1D) {}
	explicit FInputActionValue(const FVector2& InValue)
		: Value(InValue.X, InValue.Y, 0.0f), ValueType(EInputActionValueType::Axis2D) {}

	template <typename T>
	T Get() const;

	EInputActionValueType GetValueType() const { return ValueType; }
	const FVector& GetRaw() const { return Value; }

	float GetMagnitudeSq() const { return Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z; }
	float GetMagnitude() const { return std::sqrt(GetMagnitudeSq()); }
	bool IsNonZero(float Tolerance = 1.0e-4f) const { return GetMagnitudeSq() >= Tolerance * Tolerance; }

	void Reset() { Value = FVector(0.0f, 0.0f, 0.0f); }

	// 같은 Action에 매핑된 여러 키의 값을 합칠 때 사용한다 (W + S = 0)
	FInputActionValue& operator+=(const FInputActionValue& Other)
	{
		Value = FVector(Value.X + Other.Value.X, Value.Y + Other.Value.Y, Value.Z + Other.Value.Z);
		Truncate();
		return *this;
	}

	// ValueType을 바꾸면서 쓰지 않는 성분을 0으로 만든다
	void ConvertTo(EInputActionValueType InType)
	{
		ValueType = InType;
		Truncate();
	}

private:
	void Truncate()
	{
		switch (ValueType)
		{
		case EInputActionValueType::Boolean:
			Value = FVector(IsNonZero() ? 1.0f : 0.0f, 0.0f, 0.0f);
			break;
		case EInputActionValueType::Axis1D:
			Value = FVector(Value.X, 0.0f, 0.0f);
			break;
		case EInputActionValueType::Axis2D:
			Value = FVector(Value.X, Value.Y, 0.0f);
			break;
		default:
			break;
		}
	}

	FVector Value = FVector(0.0f, 0.0f, 0.0f);
	EInputActionValueType ValueType = EInputActionValueType::Boolean;
};

template <> inline bool FInputActionValue::Get<bool>() const { return IsNonZero(); }
template <> inline float FInputActionValue::Get<float>() const { return Value.X; }
template <> inline FVector2 FInputActionValue::Get<FVector2>() const { return FVector2(Value.X, Value.Y); }
template <> inline FVector FInputActionValue::Get<FVector>() const { return Value; }
