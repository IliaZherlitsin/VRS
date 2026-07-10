// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TargetKey.h"
#include "ValueAdverts.h"
#include "ValueType.generated.h"

/**
 * Lightweight key that uniquely identifies a registered value inside a resolver.
 *
 * Built from the same advert / target key pair that was passed to RegisterValue,
 * and used as the first argument to AddChange, Flush, HasChanges, MakeKey, and IsRegistered.
 *
 * Identity is determined by the combination of the advert pointer (which is
 * statically unique per member-pointer descriptor) and an optional target key
 * (used to distinguish multiple objects of the same class registered simultaneously).
 *
 * A default-constructed FValueType is invalid (IsValid() == false).
 */
USTRUCT()
struct VRS_API FValueType
{
	GENERATED_BODY()

	FValueType() = default;

	/**
	 * Construct from an advert with no context disambiguation.
	 *
	 * @param  InValueAdvert  Pointer to the statically-allocated advert descriptor.
	 */
	explicit FValueType(const IValueAdvert* InValueAdvert)
		: ValueAdvert(InValueAdvert) {}
	/**
	 * Construct from an advert with a target key.
	 *
	 * @param  InValueAdvert  Pointer to the statically-allocated advert descriptor.
	 * @param  InTargetKey    Key used to pick among multiple registered objects of the same class.
	 */
	explicit FValueType(const IValueAdvert* InValueAdvert, const FTargetKey& InTargetKey)
		: ValueAdvert(InValueAdvert)
		, TargetKey(InTargetKey) {}

	/** @return  True if the advert pointer is non-null (i.e. RegisterValue succeeded). */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return ValueAdvert != nullptr;
	}
	
	/** Required for use as a TMap key. */
	friend FORCEINLINE uint32 GetTypeHash(const FValueType& K)
	{
		return HashCombine(GetTypeHash(K.ValueAdvert), GetTypeHash(K.TargetKey));
	}


	FORCEINLINE bool operator==(const FValueType& Other) const
	{
		return ValueAdvert == Other.ValueAdvert && TargetKey == Other.TargetKey;
	}

	/** The advert that describes which member of which class is being modified. */
	const IValueAdvert* ValueAdvert = nullptr;

	/** Optional key used to disambiguate among multiple registered owners of the same class. */
	FTargetKey TargetKey;
};
