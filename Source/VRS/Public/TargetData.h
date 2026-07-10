// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FChangeContext;

/**
 * Lightweight record of a UObject registered with a resolver.
 *
 * The resolver stores one FTargetData per RegisterObject call, keyed by the unique
 * FTargetKey it hands back. All lookups go straight through that key.
 *
 * @note The object is never owned — if it is garbage collected the entry simply
 *       becomes invalid and is purged during the resolver's periodic cleanup.
 */
struct VRS_API FTargetData
{
	/** @param  InObject  The UObject to track. Stored as a weak pointer. */
	explicit FTargetData(UObject* InObject)
		: Object(InObject) {}

	/** @return  True if the tracked object has not been garbage collected. */
	FORCEINLINE [[nodiscard]] bool IsValid() const
	{
		return Object.IsValid();
	}

	/** @return  The tracked object, or null if it has been garbage collected. */
	FORCEINLINE [[nodiscard]] UObject* GetTarget() const
	{
		return Object.Get();
	}

private:
	/** Weak pointer to the registered object. Never owned. */
	TWeakObjectPtr<> Object;
};
