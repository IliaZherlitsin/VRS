// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "Change.h"
#include "ChangeEntry.h"
#include "ResolverDebug.h"
#include "VRS.h"

template <typename TValue, uint8 MaxChanges>
bool TLayerPackage<TValue, MaxChanges>::GrantChange(TUniquePtr<FChangeBase> Change)
{
	if (IsFull()) return false;
	Changes.Add(MoveTemp(Change));
	bNeedsSorting = true;
	return true;
}

template <typename TValue, uint8 MaxChanges>
bool TLayerPackage<TValue, MaxChanges>::RemoveChange(const FChangeKey& Key)
{
	if (!Key.IsValid()) return false;
	for (auto Index = 0; Index < Changes.Num(); Index++)
	{
		if (Changes[Index]->Key == Key)
		{
			Changes.RemoveAt(Index);
			return true;
		}
	}
	return false;
}

template <typename TValue, uint8 MaxChanges>
bool TLayerPackage<TValue, MaxChanges>::Flush(TValue& BaseValue, TArray<FChangeKey>& OutFlushed, const bool bWithForce)
{
	if (!HasChanges()) return false;
	SortChanges();
	for (auto i = 0; i < Changes.Num(); i++)
	{
		if (auto& CurrentChange = Changes[i]; CurrentChange.IsValid())
		{
			// A change leaves the layer when the flush is forced or it is not Static.
			// Must mirror the ClearChanges criterion below — only keys of changes that
			// are actually removed may be reported, otherwise the resolver's
			// ChangesFastData bookkeeping goes stale.
			const bool bWillRemove = bWithForce || !EnumHasAnyFlags(CurrentChange->Settings.Flags, EChangeFlags::Static);

			// Consume changes are applied at most once; everything else re-applies on every flush.
			const bool bApply = !EnumHasAnyFlags(CurrentChange->Settings.Flags, EChangeFlags::Consume) || !CurrentChange->bIsApplied;
#if !UE_BUILD_SHIPPING
			
			if (TChangeTyped<TValue>* TypedChange = static_cast<TChangeTyped<TValue>*>(CurrentChange.Get()))
			{
				if (bWillRemove) OutFlushed.Add(TypedChange->Key);
				if (bApply)
				{
					const auto OldValue = TypedChange->GetValueAsString(BaseValue);
					if (TypedChange->Flush(BaseValue))
					{
						TypedChange->bIsApplied = true;
						const auto NewValue = TypedChange->GetValueAsString(BaseValue);
						RecordChange(TypedChange, OldValue, NewValue, bWillRemove);
					}
					else UE_LOG(LogVRS, Error, TEXT("Failed to flush change of advert %s by %s in %s"),
						AdvertName, *TypedChange->DebugData.ChangerName, *TargetName)
				}
				else if (bWillRemove)
				{
					// The change leaves the layer without touching the value (an already
					// consumed change swept out by a forced flush). Record the removal so
					// the debug trail explains where the change went — identical old/new
					// values mark a pure removal.
					const auto Value = TypedChange->GetValueAsString(BaseValue);
					RecordChange(TypedChange, Value, Value, true);
				}
			}
#else
			if (TChangeTyped<TValue>* TypedChange = static_cast<TChangeTyped<TValue>*>(CurrentChange.Get()))
			{
				if (bApply && TypedChange->Flush(BaseValue)) 
					TypedChange->bIsApplied = true;
				if (bWillRemove) OutFlushed.Add(TypedChange->Key);
			}
#endif
		}
		
	}
	if (bWithForce) Changes.Empty();
	else ClearChanges();
	return true;
}

template <typename TValue, uint8 MaxChanges>
void TLayerPackage<TValue, MaxChanges>::SortChanges()
{
	if (!bNeedsSorting || Changes.Num() < 2) return;
	Changes.Sort([](const TUniquePtr<FChangeBase>& A, const TUniquePtr<FChangeBase>& B)
		{
			if (!A.IsValid() || !B.IsValid()) return A.IsValid();

			const auto APri = A->Settings.Priority;
			const auto BPri = B->Settings.Priority;
			if (APri != BPri) return APri < BPri;
			return A->Settings.Operation < B->Settings.Operation;
		}
	);
	bNeedsSorting = false;
}

template <typename TValue, uint8 MaxChanges>
void TLayerPackage<TValue, MaxChanges>::ClearChanges()
{
	for (int32 i = 0; i < Changes.Num();)
	{
		if (!EnumHasAnyFlags(Changes[i]->Settings.Flags, EChangeFlags::Static)) Changes.RemoveAt(i);
		else ++i;
	}
}

template <typename TValue, uint8 MaxChanges>
void TLayerPackage<TValue, MaxChanges>::RecordChange(const TChangeTyped<TValue>* Change, const FString& OldValue,
	const FString& NewValue, const bool bRemoved)
{
	if (!Change) return;
	auto& DebugSystem = FResolverDebug::Get();
	const FChangeEntry Entry(AdvertName, ResolverName, TargetName, 
		Change->DebugData.ChangerName, GetOperationAsString(Change->Settings.Operation),
		Change->DebugData.ChangeTime, FDateTime::Now(),
		OldValue, NewValue, Layer, bRemoved);
	DebugSystem.RecordChange(Entry);
}
