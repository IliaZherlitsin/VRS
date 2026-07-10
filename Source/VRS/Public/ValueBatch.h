// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Resolver.h"
#include "ValueAdverts.h"
#include "ValueType.h"

/**
 * One deferred value registration inside an FValueBatch.
 *
 * Stores the value key together with a type-erased function pointer that performs
 * the actual RegisterValue call. The function pointer captures the template
 * arguments (owner / value types and settings) at Add time, so the batch itself
 * stays non-templated.
 */
struct FValueData
{
	/** Key identifying the value to register. */
	FValueType Type;

	/** Registration thunk — invokes the correctly templated RegisterValue on the resolver. */
	bool (*ApplyFn)(FResolver&, const FValueType&);
};

/**
 * Accumulates value registrations for deferred, grouped submission.
 *
 * Instead of calling RegisterValue on the resolver one value at a time, callers
 * describe every value up front and submit them together via FResolver::AddValueBatch.
 * Each Add captures the required template arguments in a function pointer, so the
 * batch can be built where the types are known and applied later where they are not.
 *
 * @code
 * FValueBatch Batch;
 * Batch.Add<AMyCharacter, float, FValueSettings{10, 5}>(MaxHealthAdvert);
 * Batch.Add<AMyCharacter, FStats, float, FValueSettings{10, 5}>(SpeedAdvert);
 * Resolver->AddValueBatch(Batch);
 * @endcode
 *
 * @see FResolver::AddValueBatch, FChangeBatch
 */
struct FValueBatch
{
	friend class FResolver;

	/**
	 * Queues a direct member value for registration.
	 *
	 * @tparam  TOwner    UObject-derived type that owns the value.
	 * @tparam  TValue    Type of the value being modified.
	 * @tparam  Settings  Storage bounds for the value (changes per layer, layer count).
	 *
	 * @param  Advert  Compile-time descriptor of the member to register.
	 * @param  Key     Target key from RegisterObject. Default {} resolves the first
	 *                 registered object of matching class.
	 * @return         Key that will identify the value once the batch is applied.
	 */
	template<typename TOwner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
	FValueType Add(const TValueAdvert<TOwner, TValue>& Advert, const FTargetKey& Key)
	{
		const FValueType ValueType(&Advert, Key);
		auto Fun = [](FResolver& Resolver, const FValueType& Val)
		{
			return Resolver.RegisterValue<TOwner, TValue, Settings>(Val);
		};
		Values.Add(FValueData{ValueType, Fun});
		return ValueType;
	}

	/**
	 * Queues a nested (member-of-member) value for registration.
	 *
	 * @tparam  TOwner    UObject-derived type that owns the outer struct member.
	 * @tparam  TInner    Type of the intermediate struct.
	 * @tparam  TValue    Type of the target value inside TInner.
	 * @tparam  Settings  Storage bounds for the value (changes per layer, layer count).
	 *
	 * @param  Advert  Compile-time descriptor of the nested member to register.
	 * @param  Key     Target key from RegisterObject. Default {} resolves the first
	 *                 registered object of matching class.
	 * @return         Key that will identify the value once the batch is applied.
	 */
	template<typename TOwner, typename TInner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
	FValueType Add(const TInnerValueAdvert<TOwner, TInner, TValue>& Advert, const FTargetKey& Key)
	{
		const FValueType ValueType(&Advert, Key);
		auto Fun = [](FResolver& Resolver, const FValueType& Val)
		{
			return Resolver.RegisterValue<TOwner, TInner, TValue, Settings>(Val);
		};
		Values.Add(FValueData{ValueType, Fun});
		return ValueType;
	}

	/**
	 * Queues a nested value registration from a pre-built FValueType.
	 *
	 * Useful when the key is declared once (e.g. as an inline constant in a header)
	 * and shared across systems.
	 *
	 * @tparam  TOwner    UObject-derived type that owns the outer struct member.
	 * @tparam  TInner    Type of the intermediate struct.
	 * @tparam  TValue    Type of the target value inside TInner.
	 * @tparam  Settings  Storage bounds for the value (changes per layer, layer count).
	 *
	 * @param  ValueType  Pre-built key produced from the same advert / target key pair.
	 */
	template<typename TOwner, typename TInner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
	void Add(const FValueType& ValueType)
	{
		auto Fun = [](FResolver& Resolver, const FValueType& Val)
		{
			return Resolver.RegisterValue<TOwner, TInner, TValue, Settings>(Val);
		};
		Values.Add(FValueData{ValueType, Fun});
	}

	/**
	 * Removes a queued registration by key.
	 *
	 * @param  InValue  Key returned from Add.
	 * @return          True if a matching entry was found and removed.
	 */
	FORCEINLINE bool Remove(const FValueType& InValue)
	{
		for (auto It = Values.CreateIterator(); It; ++It)
		{
			if (It->Type != InValue) continue;
			It.RemoveCurrent();
			return true;
		}
		return false;
	}

	/** Discards every queued registration. */
	FORCEINLINE void Empty()
	{
		Values.Empty();
	}

	/** @return  Number of queued registrations. */
	[[nodiscard]] FORCEINLINE int32 Num() const
	{
		return Values.Num();
	}

	/** @return  True if a registration with the given key is queued. */
	[[nodiscard]] FORCEINLINE bool Contains(const FValueType& InType) const
	{
		for (const auto& [Type, ApplyFn] : Values)
			if (Type == InType) return true;
		return false;
	}

	/** @return  True if the batch holds no registrations. */
	[[nodiscard]] FORCEINLINE bool IsEmpty() const
	{
		return Values.IsEmpty();
	}

private:
	/** Queued registrations in submission order. */
	TArray<FValueData> Values;
};
