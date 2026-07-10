// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChangeKey.generated.h"

/**
 * Opaque handle that uniquely identifies a queued change inside the resolver.
 *
 * Returned by FResolver::AddChange / FChangeBatch::Add via the OutKey parameter.
 * Pass it to FResolver::RemoveChange to withdraw a change before it is flushed.
 *
 * Constructed only through FChangeKey::New() — default-constructed keys are invalid.
 */
USTRUCT()
struct VRS_API FChangeKey
{
	GENERATED_BODY()

	FChangeKey() = default;

	/** Creates a new unique key. */
	[[nodiscard]] FORCEINLINE static FChangeKey New()
	{
		return FChangeKey(FGuid::NewGuid());
	}

	/** @return  True if the key was produced by New() and has not been default-constructed. */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return Key.IsValid();
	}
	
	[[nodiscard]] FORCEINLINE FGuid GetKey() const
	{
		return Key;
	}
	
	friend FORCEINLINE uint32 GetTypeHash(const FChangeKey& K)
	{
		return GetTypeHash(K.Key);
	}

	FORCEINLINE bool operator==(const FChangeKey& Other) const
	{
		return Key == Other.Key;
	}
protected:
	explicit FChangeKey(const FGuid& InKey)
		: Key(InKey) {}

	FGuid Key;
};
