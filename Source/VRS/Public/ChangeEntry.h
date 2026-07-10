// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Immutable snapshot of a single value mutation recorded by the debug system.
 *
 * Created inside TLayerPackage::RecordChange every time a change is flushed in a
 * non-shipping build and forwarded to FResolverDebug::RecordChange for buffering and
 * eventual serialisation to a JSON Lines file.
 *
 * All string fields are pre-formatted at the point of capture so that the debug system
 * can serialise them cheaply without knowing the concrete value type.
 *
 * @note This struct is non-shipping only — it is never instantiated in shipping builds.
 *       FResolverDebug has a zero-cost stub that discards all RecordChange calls.
 */
struct VRS_API FChangeEntry
{
	friend class FResolverDebug;
	
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVRSDebugFlushIntegrationTest;
#endif

	/**
	 * Constructs a fully populated entry.
	 *
	 * All parameters correspond directly to the fields below.
	 * Called by TLayerPackage<TValue>::RecordChange.
	 */
	FChangeEntry(const TCHAR* InAdvertName, const FString& InResolverName, const FString& InTargetName,
		const FString& InChangerName, const FString& InOperationName, const FDateTime& InChangeTime,
		const FDateTime& InFlushTime, const FString& InOldValue, const FString& InNewValue,
		const uint8 InLayer, const bool bInChangeRemoved)
			: AdvertName(InAdvertName)
			, ResolverName(InResolverName)
			, TargetName(InTargetName)
			, ChangerName(InChangerName)
			, OperationName(InOperationName)
			, ChangeTime(InChangeTime)
			, FlushTime(InFlushTime)
			, OldValue(InOldValue)
			, NewValue(InNewValue)
			, Layer(InLayer)
			, bChangeRemoved(bInChangeRemoved) {}

	/**
	 * @return  True if the entry carries the minimum data required for serialisation
	 *          (a non-null advert name and non-empty before/after values).
	 */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return AdvertName != nullptr &&
			   !OldValue.IsEmpty() &&
			   !NewValue.IsEmpty();
	}

protected:
	/** Name of the registered value advert (e.g. "MaxHealth", "MoveSpeed"). */
	const TCHAR* AdvertName;

	/** Display name of the FResolver instance that owns the changed value. */
	FString ResolverName;

	/** Name of the UObject that owns the underlying value member. */
	FString TargetName;

	/** Name of the system / component that submitted the change. */
	FString ChangerName;

	/** String form of the EValueOperation (e.g. "Add", "Set"). */
	FString OperationName;

	/** When AddChange was called (change was enqueued). */
	FDateTime ChangeTime;

	/** When Flush was called (change was applied to the value). */
	FDateTime FlushTime;

	/** String representation of the value before the change was applied. */
	FString OldValue;

	/** String representation of the value after the change was applied. */
	FString NewValue;

	/** Priority layer index on which the change resided. */
	uint8 Layer = 0;

	/**
	 * True if the change was removed from its layer after this flush
	 * (i.e. it was non-static, or the flush was forced).
	 */
	bool bChangeRemoved = false;
};
