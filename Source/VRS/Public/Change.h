// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VRS.h"
#include "ChangeKey.h"
#include "GameplayTagContainer.h"

/**
 * Arithmetic operation applied to a value during Flush.
 *
 * Operations are evaluated in priority order per layer.
 * Notation: V = current value, X = change operand.
 */
enum class EValueOperation : uint8
{
	/** No operation. Changes with this value are considered invalid. */
	None,

	/** V = X. Overwrites the current value entirely. */
	Set,

	/** V = V + X. */
	Add,

	/** V = V - X. */
	Sub,

	/** V = V * X. */
	Mul,

	/** V = V / X. Division by zero is silently skipped. */
	Div,
};

/** @return  String representation of the operation (e.g. "Add", "Set"). */
FORCEINLINE static FString GetOperationAsString(const EValueOperation Type)
{
	switch (Type)
	{
	case EValueOperation::None: return FString("None");
	case EValueOperation::Set:  return FString("Set");
	case EValueOperation::Add:  return FString("Add");
	case EValueOperation::Sub:  return FString("Sub");
	case EValueOperation::Mul:  return FString("Mul");
	case EValueOperation::Div:  return FString("Div");
	default:                    return FString("Unknown");
	}
}

/**
 * Behaviour flags of a change. Combinable as a bitmask.
 *
 * Common combinations:
 *   - None            — transient: applied by the next Flush, then removed.
 *   - Static          — persists in its layer and is re-applied by every Flush
 *                       until removed explicitly or force-flushed.
 *   - Static|Consume  — persists but applies exactly once. The equipment pattern:
 *                       apply the bonus once, keep the change addressable, withdraw
 *                       it later via RemoveChange.
 */
enum class EChangeFlags : uint8
{
	/** No special behaviour — the change is transient. */
	None    = 0,

	/** The change survives a normal Flush and stays in its layer. */
	Static  = 1 << 0,

	/** The change is applied at most once — later flushes skip it. */
	Consume = 1 << 1,
};
ENUM_CLASS_FLAGS(EChangeFlags);

/**
 * Describes how a change is sorted, applied and retained.
 *
 * Within a single layer, changes are sorted by Priority (ascending) and then by Operation.
 * Retention and re-application are controlled by Flags — see EChangeFlags.
 */
struct FChangeSettings
{
	FChangeSettings(const uint8 InPriority, const EValueOperation InOperation, const EChangeFlags InFlags = EChangeFlags::None)
		: Priority(InPriority)
		, Flags(InFlags)
		, Operation(InOperation) {}

	/** @return  True if the operation is not None. */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return Operation != EValueOperation::None;
	}

	/** Sort order within the layer — higher value = higher priority = applied later. */
	uint8 Priority = 0;

	/** Retention / re-application behaviour of the change. See EChangeFlags. */
	EChangeFlags Flags = EChangeFlags::None;


	/** The arithmetic operation applied to the target value. */
	EValueOperation Operation = EValueOperation::None;
};

#if !UE_BUILD_SHIPPING
/** Debug-only metadata attached to a change in non-shipping builds. Compiled out in shipping. */
struct FChangeDebugData
{
	/** Name of the system / component that created this change. */
	FString ChangerName = FString("Invalid");

	/** Timestamp recorded when AddChange was called. */
	FDateTime ChangeTime;
};
#endif

/**
 * Type-erased base for all changes.
 *
 * Stores the key, settings, and optional debug metadata. The concrete value and
 * Flush logic live in the typed subclass TChangeTyped<T>.
 */
struct FChangeBase
{
	virtual ~FChangeBase() = default;
	explicit FChangeBase(const FChangeKey& InKey, const FChangeSettings InSettings)
		: Key(InKey)
		, Settings(InSettings) {}

	/** @return  True if Settings.Operation is not None. */
	[[nodiscard]] FORCEINLINE bool IsValid() const { return Settings.Operation != EValueOperation::None; }

	/** Unique identifier of this change within the resolver. */
	FChangeKey Key;

	/** Set once a Flush has applied this change. Guards Consume changes against re-application. */
	bool bIsApplied = false;

	/** Priority, persistence and operation for this change. */
	FChangeSettings Settings;

#if !UE_BUILD_SHIPPING
	/** Runtime type of the value, used to detect type mismatches in development builds. */
	const std::type_info* ValueType = nullptr;

	/** Who created this change and when. Stripped in shipping builds. */
	FChangeDebugData DebugData;
#endif
};


template<typename>
inline constexpr bool TAlwaysFalse = false;

/**
 * Primary template — intentionally unimplemented.
 *
 * Instantiating TChangeTyped<T> for an unsupported type produces a clear static_assert.
 * Add a new explicit specialisation to support additional value types.
 *
 * Built-in specialisations cover all common engine types: integer types (int8..uint64),
 * float / double / bool, string types (FString, FName, FText), vectors and int-vectors,
 * rotation and transform types, colors, time types, FGuid, gameplay tags, and object pointers.
 */
template <typename TValue>
struct TChangeTyped : FChangeBase
{
	TChangeTyped(const TValue InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
		: FChangeBase(InKey, InSettings)
		, Value(InValue)
	{
		static_assert(TAlwaysFalse<TValue>, "TChangeTyped not specialized for this type. Add specialization.");
	}

	TValue Value;
};



// ===========================================================================
// Integer specialisations
// ===========================================================================

#define VRS_INT_SPECIALISATION(TYPE)                                                                  \
template <>                                                                                           \
struct TChangeTyped<TYPE> : FChangeBase                                                               \
{                                                                                                     \
    TChangeTyped(const TYPE InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)      \
        : FChangeBase(InKey, InSettings), Value(InValue)                                              \
    {                                                                                                 \
        VRS_NON_SHIPPING(ValueType = &typeid(TYPE);)                                                  \
    }                                                                                                 \
                                                                                                      \
    [[nodiscard]] FORCEINLINE bool Flush(TYPE& BaseValue) const                                       \
    {                                                                                                 \
        switch (Settings.Operation)                                                                   \
        {                                                                                             \
        case EValueOperation::Set: BaseValue  = Value; return true;                                   \
        case EValueOperation::Add: BaseValue += Value; return true;                                   \
        case EValueOperation::Sub: BaseValue -= Value; return true;                                   \
        case EValueOperation::Mul: BaseValue *= Value; return true;                                   \
        case EValueOperation::Div:                                                                    \
            if (Value == 0) return false;                                                             \
            BaseValue /= Value; return true;                                                          \
        default: return false;                                                                        \
        }                                                                                             \
    }                                                                                                 \
                                                                                                      \
    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const TYPE Value)                       \
    {                                                                                                 \
        return LexToString(Value);                                                                    \
    }                                                                                                 \
                                                                                                      \
    TYPE Value;                                                                                       \
};

VRS_INT_SPECIALISATION(int8)
VRS_INT_SPECIALISATION(int16)
VRS_INT_SPECIALISATION(int32)
VRS_INT_SPECIALISATION(int64)
VRS_INT_SPECIALISATION(uint8)
VRS_INT_SPECIALISATION(uint16)
VRS_INT_SPECIALISATION(uint32)
VRS_INT_SPECIALISATION(uint64)

#undef VRS_INT_SPECIALISATION

// ---------------------------------------------------------------------------
// float specialisation
// ---------------------------------------------------------------------------

template <>
struct TChangeTyped<float> : FChangeBase
{
    TChangeTyped(const float InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings)
        , Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(float);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(float& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value;  return true;
        case EValueOperation::Add: BaseValue += Value;  return true;
        case EValueOperation::Sub: BaseValue -= Value;  return true;
        case EValueOperation::Mul: BaseValue *= Value;  return true;
        case EValueOperation::Div:
            if (Value == 0.0f) return false;
            BaseValue /= Value;
            return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const float Value)
    {
        return FString::SanitizeFloat(Value);
    }

    float Value;
};

// ===========================================================================
// double
// ===========================================================================

template <>
struct TChangeTyped<double> : FChangeBase
{
    TChangeTyped(const double InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(double);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(double& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        case EValueOperation::Sub: BaseValue -= Value; return true;
        case EValueOperation::Mul: BaseValue *= Value; return true;
        case EValueOperation::Div:
            if (Value == 0.0) return false;
            BaseValue /= Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const double Value)
    {
        return FString::SanitizeFloat(Value);
    }

    double Value;
};

// ===========================================================================
// bool — only Set is meaningful
// ===========================================================================

template <>
struct TChangeTyped<bool> : FChangeBase
{
    TChangeTyped(const bool InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(bool);
#endif
    }

    /** Only Set is supported; arithmetic operations have no meaning for bool. */
    [[nodiscard]] FORCEINLINE bool Flush(bool& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const bool Value)
    {
        return Value ? TEXT("true") : TEXT("false");
    }

    bool Value;
};

// ===========================================================================
// FString — Set + Add (concatenation)
// ===========================================================================

template <>
struct TChangeTyped<FString> : FChangeBase
{
    TChangeTyped(const FString& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FString);
#endif
    }

    /** Set assigns; Add concatenates. Other operations are rejected. */
    [[nodiscard]] FORCEINLINE bool Flush(FString& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FString& Value)
    {
        return Value;
    }

    FString Value;
};

// ===========================================================================
// FName — only Set
// ===========================================================================

template <>
struct TChangeTyped<FName> : FChangeBase
{
    TChangeTyped(const FName InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FName);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FName& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FName Value)
    {
        return Value.ToString();
    }

    FName Value;
};

// ===========================================================================
// FText — only Set
// ===========================================================================

template <>
struct TChangeTyped<FText> : FChangeBase
{
    TChangeTyped(const FText& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FText);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FText& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FText& Value)
    {
        return Value.ToString();
    }

    FText Value;
};

// ---------------------------------------------------------------------------
// FVector specialisation
// ---------------------------------------------------------------------------

template <>
struct TChangeTyped<FVector> : FChangeBase
{
    TChangeTyped(const FVector& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings)
        , Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FVector);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FVector& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value;  return true;
        case EValueOperation::Add: BaseValue += Value;  return true;
        case EValueOperation::Sub: BaseValue -= Value;  return true;
        case EValueOperation::Mul: BaseValue *= Value;  return true;
        case EValueOperation::Div: BaseValue /= Value;  return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FVector& Value)
    {
        return Value.ToString();
    }
    FVector Value;
};

// ===========================================================================
// FVector2D — same algebra as FVector
// ===========================================================================

template <>
struct TChangeTyped<FVector2D> : FChangeBase
{
    TChangeTyped(const FVector2D& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FVector2D);
#endif
    }

    /** Component-wise for Mul/Div, like FVector. */
    [[nodiscard]] FORCEINLINE bool Flush(FVector2D& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        case EValueOperation::Sub: BaseValue -= Value; return true;
        case EValueOperation::Mul: BaseValue *= Value; return true;
        case EValueOperation::Div: BaseValue /= Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FVector2D& Value)
    {
        return Value.ToString();
    }

    FVector2D Value;
};

// ===========================================================================
// FVector4
// ===========================================================================

template <>
struct TChangeTyped<FVector4> : FChangeBase
{
    TChangeTyped(const FVector4& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FVector4);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FVector4& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        case EValueOperation::Sub: BaseValue -= Value; return true;
        case EValueOperation::Mul: BaseValue *= Value; return true;
        case EValueOperation::Div: BaseValue /= Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FVector4& Value)
    {
        return Value.ToString();
    }

    FVector4 Value;
};

// ===========================================================================
// FIntVector / FIntVector2 / FIntVector4 / FIntPoint
// ===========================================================================

#define VRS_INT_VECTOR_SPECIALISATION(TYPE)                                                           \
template <>                                                                                           \
struct TChangeTyped<TYPE> : FChangeBase                                                               \
{                                                                                                     \
    TChangeTyped(const TYPE& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)     \
        : FChangeBase(InKey, InSettings), Value(InValue)                                              \
    {                                                                                                 \
        VRS_NON_SHIPPING(ValueType = &typeid(TYPE);)                                                  \
    }                                                                                                 \
                                                                                                      \
    [[nodiscard]] FORCEINLINE bool Flush(TYPE& BaseValue) const                                       \
    {                                                                                                 \
        switch (Settings.Operation)                                                                   \
        {                                                                                             \
        case EValueOperation::Set: BaseValue  = Value; return true;                                   \
        case EValueOperation::Add: BaseValue += Value; return true;                                   \
        case EValueOperation::Sub: BaseValue -= Value; return true;                                   \
        case EValueOperation::Mul: BaseValue *= Value; return true;                                   \
        default: return false;                                                                        \
        }                                                                                             \
    }                                                                                                 \
                                                                                                      \
    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const TYPE& Value)                      \
    {                                                                                                 \
        return Value.ToString();                                                                      \
    }                                                                                                 \
                                                                                                      \
    TYPE Value;                                                                                       \
};

VRS_INT_VECTOR_SPECIALISATION(FIntVector)
VRS_INT_VECTOR_SPECIALISATION(FIntVector2)
VRS_INT_VECTOR_SPECIALISATION(FIntVector4)
VRS_INT_VECTOR_SPECIALISATION(FIntPoint)

#undef VRS_INT_VECTOR_SPECIALISATION

// ===========================================================================
// FRotator — Set/Add/Sub work, Mul/Div by scalar via component-wise
// ===========================================================================

template <>
struct TChangeTyped<FRotator> : FChangeBase
{
    TChangeTyped(const FRotator& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FRotator);
#endif
    }

    /** Set/Add/Sub are component-wise. Mul/Div are not defined for FRotator and rejected. */
    [[nodiscard]] FORCEINLINE bool Flush(FRotator& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        case EValueOperation::Sub: BaseValue -= Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FRotator& Value)
    {
        return Value.ToString();
    }

    FRotator Value;
};

// ===========================================================================
// FQuat — Set / Mul (composition)
// ===========================================================================

template <>
struct TChangeTyped<FQuat> : FChangeBase
{
    TChangeTyped(const FQuat& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FQuat);
#endif
    }

    /** Mul performs quaternion composition (BaseValue * Value). */
    [[nodiscard]] FORCEINLINE bool Flush(FQuat& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue = Value;             return true;
        case EValueOperation::Mul: BaseValue = BaseValue * Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FQuat& Value)
    {
        return Value.ToString();
    }

    FQuat Value;
};

// ===========================================================================
// FTransform — Set / Mul (composition)
// ===========================================================================

template <>
struct TChangeTyped<FTransform> : FChangeBase
{
    TChangeTyped(const FTransform& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FTransform);
#endif
    }

    /** Mul performs transform composition (BaseValue * Value). */
    [[nodiscard]] FORCEINLINE bool Flush(FTransform& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue = Value;             return true;
        case EValueOperation::Mul: BaseValue = BaseValue * Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FTransform& Value)
    {
        return Value.ToHumanReadableString();
    }

    FTransform Value;
};

// ===========================================================================
// FMatrix — Set / Add / Mul
// ===========================================================================

template <>
struct TChangeTyped<FMatrix> : FChangeBase
{
    TChangeTyped(const FMatrix& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FMatrix);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FMatrix& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue = Value;             return true;
        case EValueOperation::Add: BaseValue = BaseValue + Value; return true;
        case EValueOperation::Mul: BaseValue = BaseValue * Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FMatrix& Value)
    {
        return Value.ToString();
    }

    FMatrix Value;
};

// ===========================================================================
// FPlane — same algebra as FVector4
// ===========================================================================

template <>
struct TChangeTyped<FPlane> : FChangeBase
{
    TChangeTyped(const FPlane& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FPlane);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FPlane& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        case EValueOperation::Sub: BaseValue -= Value; return true;
        case EValueOperation::Mul: BaseValue *= Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FPlane& Value)
    {
        return Value.ToString();
    }

    FPlane Value;
};

// ===========================================================================
// FColor — 8-bit per channel. Component-wise add/sub/mul; div left out.
// ===========================================================================

template <>
struct TChangeTyped<FColor> : FChangeBase
{
    TChangeTyped(const FColor InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FColor);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FColor& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set:
            BaseValue = Value;
            return true;
        case EValueOperation::Add:
            BaseValue.R = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.R + Value.R, 0, 255));
            BaseValue.G = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.G + Value.G, 0, 255));
            BaseValue.B = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.B + Value.B, 0, 255));
            BaseValue.A = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.A + Value.A, 0, 255));
            return true;
        case EValueOperation::Sub:
            BaseValue.R = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.R - Value.R, 0, 255));
            BaseValue.G = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.G - Value.G, 0, 255));
            BaseValue.B = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.B - Value.B, 0, 255));
            BaseValue.A = static_cast<uint8>(FMath::Clamp<int32>(BaseValue.A - Value.A, 0, 255));
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FColor Value)
    {
        return Value.ToString();
    }

    FColor Value;
};

// ===========================================================================
// FLinearColor — full algebra
// ===========================================================================

template <>
struct TChangeTyped<FLinearColor> : FChangeBase
{
    TChangeTyped(const FLinearColor& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FLinearColor);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FLinearColor& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        case EValueOperation::Sub: BaseValue -= Value; return true;
        case EValueOperation::Mul: BaseValue *= Value; return true;
        case EValueOperation::Div: BaseValue /= Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FLinearColor& Value)
    {
        return Value.ToString();
    }

    FLinearColor Value;
};

// ===========================================================================
// FDateTime — only Set (Add/Sub require FTimespan, a different type)
// ===========================================================================

template <>
struct TChangeTyped<FDateTime> : FChangeBase
{
    TChangeTyped(const FDateTime InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FDateTime);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FDateTime& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FDateTime Value)
    {
        return Value.ToString();
    }

    FDateTime Value;
};

// ===========================================================================
// FTimespan — Set / Add / Sub
// ===========================================================================

template <>
struct TChangeTyped<FTimespan> : FChangeBase
{
    TChangeTyped(const FTimespan InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FTimespan);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FTimespan& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set: BaseValue  = Value; return true;
        case EValueOperation::Add: BaseValue += Value; return true;
        case EValueOperation::Sub: BaseValue -= Value; return true;
        default: return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FTimespan Value)
    {
        return Value.ToString();
    }

    FTimespan Value;
};

// ===========================================================================
// FGuid — only Set
// ===========================================================================

template <>
struct TChangeTyped<FGuid> : FChangeBase
{
    TChangeTyped(const FGuid& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FGuid);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FGuid& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FGuid& Value)
    {
        return Value.ToString();
    }

    FGuid Value;
};

// ===========================================================================
// FGameplayTag — only Set
// ===========================================================================

template <>
struct TChangeTyped<FGameplayTag> : FChangeBase
{
    TChangeTyped(const FGameplayTag& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FGameplayTag);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(FGameplayTag& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FGameplayTag& Value)
    {
        return Value.ToString();
    }

    FGameplayTag Value;
};

// ===========================================================================
// FGameplayTagContainer — Set / Add (AppendTags) / Sub (RemoveTags)
// ===========================================================================

template <>
struct TChangeTyped<FGameplayTagContainer> : FChangeBase
{
    TChangeTyped(const FGameplayTagContainer& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(FGameplayTagContainer);
#endif
    }

    /** Add appends every tag from Value; Sub removes every tag from Value that is present. */
    [[nodiscard]] FORCEINLINE bool Flush(FGameplayTagContainer& BaseValue) const
    {
        switch (Settings.Operation)
        {
        case EValueOperation::Set:
            BaseValue = Value;
            return true;
        case EValueOperation::Add:
            BaseValue.AppendTags(Value);
            return true;
        case EValueOperation::Sub:
            BaseValue.RemoveTags(Value);
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const FGameplayTagContainer& Value)
    {
        return Value.ToString();
    }

    FGameplayTagContainer Value;
};

// ===========================================================================
// TObjectPtr<UObject> — only Set (object refs are not arithmetic)
// ===========================================================================

template <>
struct TChangeTyped<TObjectPtr<UObject>> : FChangeBase
{
    TChangeTyped(const TObjectPtr<UObject>& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(TObjectPtr<UObject>);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(TObjectPtr<UObject>& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const TObjectPtr<UObject>& Value)
    {
        return GetNameSafe(Value.Get());
    }

    TObjectPtr<UObject> Value;
};

// ===========================================================================
// TWeakObjectPtr<UObject> — only Set (object refs are not arithmetic)
// ===========================================================================

template <>
struct TChangeTyped<TWeakObjectPtr<>> : FChangeBase
{
    TChangeTyped(const TWeakObjectPtr<>& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(TWeakObjectPtr<>);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(TWeakObjectPtr<>& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const TWeakObjectPtr<>& Value)
    {
        return GetNameSafe(Value.Get());
    }

    TWeakObjectPtr<> Value;
};

// ===========================================================================
// TStrongObjectPtr<UObject> — only Set (object refs are not arithmetic)
// ===========================================================================

template <>
struct TChangeTyped<TStrongObjectPtr<UObject>> : FChangeBase
{
    TChangeTyped(const TStrongObjectPtr<UObject>& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(TStrongObjectPtr<UObject>);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(TStrongObjectPtr<UObject>& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const TStrongObjectPtr<UObject>& Value)
    {
        return GetNameSafe(Value.Get());
    }

    TStrongObjectPtr<UObject> Value;
};

// ===========================================================================
// TSoftObjectPtr<UObject> — only Set
// ===========================================================================

template <>
struct TChangeTyped<TSoftObjectPtr<>> : FChangeBase
{
    TChangeTyped(const TSoftObjectPtr<>& InValue, const FChangeKey& InKey, const FChangeSettings& InSettings)
        : FChangeBase(InKey, InSettings), Value(InValue)
    {
#if !UE_BUILD_SHIPPING
        ValueType = &typeid(TSoftObjectPtr<>);
#endif
    }

    [[nodiscard]] FORCEINLINE bool Flush(TSoftObjectPtr<>& BaseValue) const
    {
        if (Settings.Operation == EValueOperation::Set)
        {
            BaseValue = Value;
            return true;
        }
        return false;
    }

    [[nodiscard]] static FORCEINLINE FString GetValueAsString(const TSoftObjectPtr<>& Value)
    {
        return Value.ToString();
    }

    TSoftObjectPtr<> Value;
};