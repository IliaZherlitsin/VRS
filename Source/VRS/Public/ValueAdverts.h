// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Type-erased base for compile-time value descriptors.
 *
 * An advert is a statically-allocated object that knows how to reach a specific
 * member (or nested member) of a UObject without storing a live object pointer.
 * The resolver uses it to locate the actual value at runtime and to produce a
 * unique key (FValueType) that identifies the value across the system.
 */
struct IValueAdvert
{
	constexpr explicit IValueAdvert(const TCHAR* InAdvertName)
		: AdvertName(InAdvertName) {}

	virtual ~IValueAdvert() = default;

	/** @return  True if all required member pointers are bound. */
	[[nodiscard]] virtual bool IsValid() const = 0;

	/** Returns a pointer to the value inside Obj, or null when Obj is null. Obj must be an instance of the owner type. */
	virtual void* GetValueVoid(void* Obj) const = 0;

	/** @return  UClass of the owner type — used to find the owner object and its CDO. */
	virtual UClass* GetObjectClass() const = 0;

	/** Display name shown in logs and the debug output (e.g. "MaxHealth"). */
	const TCHAR* AdvertName;

#if !UE_BUILD_SHIPPING
	/** Runtime type of the value — used to detect type mismatches in development builds. */
	const std::type_info* ValueType = nullptr;
#endif
};

/**
 * Typed intermediate base — adds the typed GetValue accessor.
 *
 * Not instantiated directly; use TValueAdvert or TInnerValueAdvert.
 */
template <typename TOwner, typename TValue>
struct TValueAdvertBase : IValueAdvert
{
	constexpr explicit TValueAdvertBase(const TCHAR* InAdvertName)
		: IValueAdvert(InAdvertName) {}

	/** @return  Typed pointer to the value inside Obj, or null if not overridden. */
	[[nodiscard]] virtual TValue* GetValue(TOwner* Obj) const { return nullptr; }

	/** Type-erased bridge — forwards to the typed GetValue after restoring the owner type. */
	virtual void* GetValueVoid(void* Obj) const override
	{
		return GetValue(static_cast<TOwner*>(Obj));
	}

	/** @return  UClass of TOwner. */
	virtual UClass* GetObjectClass() const override
	{
		return TOwner::StaticClass();
	}
};

/**
 * Advert for a direct member variable of a UObject subclass.
 *
 * Declare one statically per value you want to register with the resolver:
 *
 * @code
 * // In a header:
 * inline constexpr TValueAdvert<AMyCharacter, float> MaxHealthAdvert
 *     { TEXT("MaxHealth"), &AMyCharacter::MaxHealth };
 *
 * // Then register:
 * Resolver->RegisterValue<AMyCharacter, float, FValueSettings{10, 5}>(MaxHealthAdvert);
 * @endcode
 *
 * @tparam  TOwner  UObject-derived class that owns the member.
 * @tparam  TValue  Type of the member (int32, float, FVector, ...).
 */
template <typename TOwner, typename TValue> requires std::is_base_of_v<UObject, TOwner>
struct TValueAdvert : TValueAdvertBase<TOwner, TValue>
{
	constexpr TValueAdvert() = default;

	/**
	 * @param  InAdvertName  Human-readable name shown in logs and debug output.
	 * @param  InMember      Pointer-to-member for the value to expose.
	 */
	constexpr TValueAdvert(const TCHAR* InAdvertName, TValue TOwner::* InMember)
		: TValueAdvertBase<TOwner, TValue>(InAdvertName)
		, Member(InMember)
	{
#if !UE_BUILD_SHIPPING
		this->ValueType = &typeid(TValue);
#endif
	}

	/** @return  True if the member pointer is bound. */
	FORCEINLINE virtual bool IsValid() const override { return Member != nullptr; }

	/** @return  Pointer to the member on Obj, or null when Obj is null. */
	FORCEINLINE virtual TValue* GetValue(TOwner* Obj) const override { return Obj ? &(Obj->*Member) : nullptr; }
private:
	TValue TOwner::* Member{ nullptr };
};

/**
 * Advert for a value nested inside an embedded struct member of a UObject.
 *
 * Follows two member pointers: TOwner::Outer (of type TInner) and TInner::Inner (of type TValue).
 *
 * @code
 * // In a header:
 * inline constexpr TInnerValueAdvert<AMyCharacter, FStats, float> SpeedAdvert
 *     { TEXT("MoveSpeed"), &AMyCharacter::Stats, &FStats::MoveSpeed };
 * @endcode
 *
 * @tparam  TOwner  UObject-derived class that owns the outer struct member.
 * @tparam  TInner  Type of the intermediate struct (e.g. FStats).
 * @tparam  TValue  Type of the target value inside TInner.
 */
template <typename TOwner, typename TInner, typename TValue> requires std::is_base_of_v<UObject, TOwner>
struct TInnerValueAdvert : TValueAdvertBase<TOwner, TValue>
{
	constexpr TInnerValueAdvert() = default;

	/**
	 * @param  InAdvertName  Human-readable name shown in logs and debug output.
	 * @param  InOuter       Pointer-to-member for the struct field on TOwner.
	 * @param  InInner       Pointer-to-member for the value field on TInner.
	 */
	constexpr TInnerValueAdvert(const TCHAR* InAdvertName, TInner TOwner::* InOuter, TValue TInner::* InInner)
		: TValueAdvertBase<TOwner, TValue>(InAdvertName)
		, Inner(InInner)
		, Outer(InOuter)
	{
#if !UE_BUILD_SHIPPING
		this->ValueType = &typeid(TValue);
#endif
	}

	/** @return  True if both member pointers are bound. */
	FORCEINLINE virtual bool IsValid() const override { return Inner && Outer; }

	/** @return  Pointer to the nested value on Obj, or null when Obj is null. */
	FORCEINLINE virtual TValue* GetValue(TOwner* Obj) const override { return Obj ? &((Obj->*Outer).*Inner) : nullptr; }
private:
	TValue  TInner::* Inner{ nullptr };
	TInner TOwner::* Outer{ nullptr };
};
