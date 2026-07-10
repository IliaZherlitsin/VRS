// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Change.h"

/**
 * Per-layer container for pending changes targeting a single registered value.
 *
 * An aggregator owns one TLayerPackage per priority layer. Changes added to a layer
 * stay there until Flush is called, at which point they are sorted (by priority and
 * operation), applied in order to the underlying value, and either removed (transient
 * changes) or kept (static changes).
 *
 * Storage uses a fixed allocator: the maximum number of simultaneously queued changes
 * per layer is a compile-time constant. New changes added beyond MaxChanges are
 * silently rejected by GrantChange.
 *
 * @tparam  TValue       Type of the underlying value (int32, float, FVector, ...).
 * @tparam  MaxChanges   Hard upper bound on simultaneously queued changes in this layer.
 *
 * @note Not intended for direct use. Always interact with the layer through its owning
 *       value aggregator.
 */
template<typename TValue, uint8 MaxChanges = 10>
struct TLayerPackage
{
	/**
	 * Adds a change to the layer.
	 *
	 * The change is not applied yet; it is queued and marked for re-sorting on the
	 * next Flush.
	 *
	 * @param  Change  Owning pointer to the change. Ownership is transferred.
	 * @return         True if the change was queued. False if the layer is full.
	 */
	bool GrantChange(TUniquePtr<FChangeBase> Change);

	/**
	 * Removes a previously queued (but not yet flushed) change by key.
	 *
	 * @param  Key  Identifier returned when the change was queued.
	 * @return      True if a matching change was found and removed.
	 */
	bool RemoveChange(const FChangeKey& Key);

	/**
	 * Applies every pending change to BaseValue in priority order.
	 *
	 * Transient (non-static) changes are removed after being applied; static changes
	 * remain unless bWithForce is true. Consume changes are applied at most once.
	 *
	 * @param  BaseValue   Underlying value of the aggregator. Mutated in place.
	 * @param  OutFlushed  Receives the keys of every change that left the layer, so the
	 *                     resolver can retire their bookkeeping. Keys of surviving static
	 *                     changes are not reported.
	 * @param  bWithForce  If true, also flushes static changes and clears the layer entirely.
	 * @return             True if at least one change was processed.
	 */
	bool Flush(TValue& BaseValue, TArray<FChangeKey>& OutFlushed, const bool bWithForce);


	/** @return  True if the layer has at least one queued change. */
	[[nodiscard]] FORCEINLINE bool HasChanges() const
	{
		return !Changes.IsEmpty();
	}

	/** @return  True if the layer has reached MaxChanges and cannot accept more. */
	[[nodiscard]] FORCEINLINE bool IsFull() const
	{
		return Changes.Num() == MaxChanges;
	}

#if !UE_BUILD_SHIPPING
	/** Display name of the owning object — populated by the aggregator. Stripped in shipping. */
	FString TargetName;

	/** Display name of the resolver this layer belongs to. Stripped in shipping. */
	FString ResolverName;

	/** Static advert name used as a stable identifier in logs. Stripped in shipping. */
	const TCHAR* AdvertName = nullptr;
#endif
	/** Index of this layer within the parent aggregator (0 = lowest priority). */
	uint8 Layer = 0;
private:
	void SortChanges();
	void ClearChanges();

#if !UE_BUILD_SHIPPING
	void RecordChange(const TChangeTyped<TValue>* Change, const FString& OldValue, const FString& NewValue, const bool bRemoved);
#endif

	bool bNeedsSorting = false;
	TArray<TUniquePtr<FChangeBase>, TFixedAllocator<MaxChanges>> Changes;
};

#include "LayerPackage.inl"
