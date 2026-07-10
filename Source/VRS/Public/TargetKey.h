// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TargetKey.generated.h"

USTRUCT()
struct VRS_API FTargetKey
{
	GENERATED_BODY()

	FTargetKey() = default;

#if !UE_BUILD_SHIPPING
	/** Creates a new unique key tagged with a human-readable name for debug output. */
	[[nodiscard]] FORCEINLINE static FTargetKey New(const FString& Name)
	{
		return FTargetKey(FGuid::NewGuid(), Name);
	}
#else
	/** Creates a new unique key. The debug name is stripped in shipping. */
	[[nodiscard]] FORCEINLINE static FTargetKey New(const FString& Name = {})
	{
		return FTargetKey(FGuid::NewGuid());
	}
#endif

	/**
	 * @return  Human-readable identifier for logs. The debug name in non-shipping
	 *          builds, the raw GUID string in shipping (where the name is stripped).
	 */
	[[nodiscard]] FORCEINLINE FString ToString() const
	{
#if !UE_BUILD_SHIPPING
		return Name;
#else
		return Key.ToString();
#endif
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
	
	friend FORCEINLINE uint32 GetTypeHash(const FTargetKey& K)
	{
		return GetTypeHash(K.Key);
	}

	FORCEINLINE bool operator==(const FTargetKey& Other) const
	{
		return Key == Other.Key;
	}
protected:
	explicit FTargetKey(const FGuid& InKey, const FString& InName)
		: Key(InKey) 
		, Name(InName) {}
	
	explicit FTargetKey(const FGuid& InKey)
		: Key(InKey) {}

	FGuid Key;
	
#if !UE_BUILD_SHIPPING
	FString Name;
#endif	
};
