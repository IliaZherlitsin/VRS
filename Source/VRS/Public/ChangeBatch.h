// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ValueType.h"
#include "Change.h"

/**
 * Internal storage entry for a single change inside an FChangeBatch.
 *
 * Groups the target value, the context (layer + changer), and the change itself.
 * Not intended for direct use — managed entirely by FChangeBatch.
 */
struct FChangeBatchData
{
	FChangeBatchData(const FValueType& InValueType, const FChangeContext& InContext, TUniquePtr<FChangeBase> InChange)
		: ValueType(InValueType)
		, Context(InContext)
		, Change(MoveTemp(InChange)) {}

	/** Identifies which registered value the change targets. */
	FValueType ValueType;

	/** Layer and changer information passed to the aggregator. */
	FChangeContext Context;

	/** The change to apply. Ownership is held here until FResolver::AddBatch moves it out. */
	TUniquePtr<FChangeBase> Change;
};

/**
 * Accumulates multiple changes that are submitted to the resolver as a single atomic unit.
 *
 * Build up the batch with Add(), then pass it to FResolver::AddBatch().
 * The resolver applies all accepted changes and empties the batch on return, so the same
 * instance can be reused across frames.
 *
 * Typical use:
 * @code
 * FChangeBatch Batch;
 * FChangeContext Ctx(this, 1);
 *
 * Batch.Add(HealthType, -25, FChangeSettings(10, EValueOperation::Add), Ctx);
 * Batch.Add(SpeedType,  0.8f, FChangeSettings(5, EValueOperation::Mul, EChangeFlags::Static), Ctx);
 *
 * int32 Applied = Resolver->AddBatch(Batch);
 * @endcode
 *
 * @note The batch holds TUniquePtr ownership of each change. After AddBatch() all
 *       internal entries are cleared, regardless of whether they were accepted.
 */
struct FChangeBatch
{
	friend class FResolver;

	/**
	 * Adds a pre-built change to the batch.
	 *
	 * @param  ValueType      Key returned from RegisterValue.
	 * @param  Change         Owning pointer to the change. Ownership is transferred.
	 * @param  ChangeContext  Layer and changer info.
	 * @param  OutKey         Optional. Receives the key assigned to this change.
	 * @return                True if all inputs are valid and the change was enqueued.
	 */
	bool Add(const FValueType& ValueType, TUniquePtr<FChangeBase> Change, const FChangeContext& ChangeContext, FChangeKey* OutKey = nullptr)
	{
		if (!Change || !ValueType.IsValid() || !Change->IsValid() || !ChangeContext.IsValid()) return false;
		const auto NewKey = FChangeKey::New();
		Changes.Add(NewKey, FChangeBatchData(ValueType, ChangeContext, MoveTemp(Change)));
		if (OutKey) *OutKey = NewKey;
		return true;
	}

	/**
	 * Convenience overload — constructs a TChangeTyped<T> internally.
	 *
	 * @tparam T              Value type (int32, float, FVector, ...).
	 * @param  ValueType      Key returned from RegisterValue.
	 * @param  NewValue       Operand of the operation.
	 * @param  Settings       Priority, behaviour flags and operation.
	 * @param  ChangeContext  Layer and changer info.
	 * @param  OutKey         Optional. Receives the key assigned to this change.
	 * @return                True if all inputs are valid and the change was enqueued.
	 */
	template<typename T>
	bool Add(const FValueType& ValueType, const T& NewValue, const FChangeSettings& Settings, const FChangeContext& ChangeContext, FChangeKey* OutKey = nullptr)
	{
		if (!ValueType.IsValid() || !Settings.IsValid() || !ChangeContext.IsValid()) return false;
		const auto NewKey = FChangeKey::New();
		auto Change = MakeUnique<TChangeTyped<T>>(NewValue, NewKey, Settings);
		Changes.Add(NewKey, FChangeBatchData(ValueType, ChangeContext, MoveTemp(Change)));
		if (OutKey) *OutKey = NewKey;
		return true;
	}

	/**
	 * Removes a change from the batch before it is submitted.
	 *
	 * @param  Key  Key received via OutKey during Add.
	 * @return      True if a matching entry was found and removed.
	 */
	FORCEINLINE bool Remove(const FChangeKey& Key)
	{
		return Changes.Remove(Key) != 0;
	}

	/** Removes all changes from the batch without submitting them. */
	FORCEINLINE void Empty()
	{
		Changes.Empty();
	}

	/** @return  Number of changes currently in the batch. */
	[[nodiscard]] FORCEINLINE int32 Num() const
	{
		return Changes.Num();
	}

	/** @return  True if the batch contains the change identified by Key. */
	[[nodiscard]] FORCEINLINE bool Contains(const FChangeKey& Key) const
	{
		return Changes.Contains(Key);
	}

	/** @return  True if the batch has no pending changes. */
	[[nodiscard]] FORCEINLINE bool IsEmpty() const
	{
		return Changes.IsEmpty();
	}

private:
	TMap<FChangeKey, FChangeBatchData> Changes;
};
