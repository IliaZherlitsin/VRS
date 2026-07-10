// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#include "Resolver.h"
#include "ChangeBatch.h"
#include "ValueBatch.h"

FResolver::FResolver(UObject& InOwner, const double InClearInterval)
	: InClearInterval(InClearInterval > 0.0 ? InClearInterval : 30.0)
	, Owner(&InOwner)
{
	EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FResolver::HandleEndFrame);
#if !UE_BUILD_SHIPPING
	if (Owner.IsValid()) ResolverName = FString("Resolver ") + Owner->GetName();
#endif
}

FResolver::~FResolver()
{
	if (EndFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle.Reset();
	}
}

UObject* FResolver::FindObject(const FTargetKey& Key)
{
	if (const auto Target = Targets.Find(Key)) 
		return Target->GetTarget();
	return nullptr;
}

void FResolver::HandleEndFrame()
{
	AccumulatedTime += FApp::GetDeltaTime(); 
	if (AccumulatedTime < InClearInterval) return;
		
	AccumulatedTime -= InClearInterval; 
	ClearResolver(); 
}

void FResolver::ClearResolver()
{
	for (auto It = Targets.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid()) It.RemoveCurrent();
	}

	TArray<FValueType, TInlineAllocator<8>> DeadValueTypes;
	for (auto It = Aggregators.CreateIterator(); It; ++It)
	{
		if (!It.Value() || !It.Value()->IsValid())
		{
			DeadValueTypes.Add(It.Key());
			ValueKeys.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	// Purge pending-change bookkeeping that pointed at the removed aggregators.
	if (!DeadValueTypes.IsEmpty())
	{
		for (auto It = ChangesFastData.CreateIterator(); It; ++It)
		{
			if (DeadValueTypes.Contains(It.Value().ValueType)) It.RemoveCurrent();
		}
	}
}

int32 FResolver::AddBatch(FChangeBatch& Batch)
{
	if (Batch.IsEmpty()) return 0;
	auto AddedAmount = 0;
	for (auto& ChangeData : Batch.Changes)
	{
		const auto Key = ChangeData.Key;
		auto& Data = ChangeData.Value;
		auto& Change = Data.Change;
		if (const auto Aggregator = Aggregators.Find(Data.ValueType))
		{
			Change->Key = Key;
#if !UE_BUILD_SHIPPING
			Change->DebugData = FChangeDebugData(Data.Context.ChangerName, FDateTime::Now());
#endif
			if ((*Aggregator)->GrantChange(MoveTemp(Change), Data.Context))
			{
				ChangesFastData.Add(Key, FChangeFastData(Data.Context.Layer, Data.ValueType));
				AddedAmount++;
			}
		}
	}
	Batch.Empty();
	return AddedAmount;
}

int32 FResolver::AddValueBatch(FValueBatch& Batch)
{
	if (Batch.IsEmpty()) return 0;
	auto AddedAmount = 0;
	for (auto& [Value, ApplyFn] : Batch.Values)
	{
		if (ApplyFn(*this, Value)) AddedAmount++;
	}
	Batch.Empty();
	return AddedAmount;
}

bool FResolver::RemoveChange(const FChangeKey& Key)
{
	if (!Key.IsValid()) return false;
	if (const auto ChangeData = ChangesFastData.Find(Key))
	{
		if (const auto Aggregator = Aggregators.Find(ChangeData->ValueType))
		{
			if ((*Aggregator)->RemoveChange(Key, ChangeData->Layer))
			{
				ChangesFastData.Remove(Key);
				return true;
			}
		}
	}
	return false;
}

FTargetKey FResolver::RegisterObject(UObject* Object, const FString& Name)
{
#if !UE_BUILD_SHIPPING
	const auto NewKey = FTargetKey::New(Name);
#else
	const auto NewKey = FTargetKey::New();
#endif	
	Targets.Add(NewKey, FTargetData(Object));
	return NewKey;
}

bool FResolver::AddChange(const FValueType& ValueType, TUniquePtr<FChangeBase> Change, const FChangeContext& ChangeContext,
                          FChangeKey* OutKey)
{
	if (!Change || !ValueType.IsValid() || !Change->IsValid() || !ChangeContext.IsValid()) return false;
	if (const auto Aggregator = Aggregators.Find(ValueType))
	{
		const auto NewKey = FChangeKey::New();
		Change->Key = NewKey;
#if !UE_BUILD_SHIPPING
		Change->DebugData = FChangeDebugData(ChangeContext.ChangerName, FDateTime::Now());
#endif
		
		if ((*Aggregator)->GrantChange(MoveTemp(Change), ChangeContext))
		{
			ChangesFastData.Add(NewKey, FChangeFastData(ChangeContext.Layer, ValueType));
			if (OutKey) *OutKey = NewKey;
			return true;
		}
	}
	return false;
}

bool FResolver::Flush(const bool bWithForce)
{
	bool Flushed = false;
	TArray<FChangeKey> FlushedChanges;
	
	for (const auto& AggregatorData : Aggregators)
		Flushed |= AggregatorData.Value->Flush(FlushedChanges, bWithForce);
	
	for (const FChangeKey& Key : FlushedChanges)
		ChangesFastData.Remove(Key);
	return Flushed;
}

bool FResolver::Flush(const FValueType& ValueType, const bool bWithForce)
{
	if (!ValueType.IsValid()) return false;
	if (const auto Aggregator = Aggregators.Find(ValueType))
	{
		TArray<FChangeKey> FlushedChanges;
		(*Aggregator)->Flush(FlushedChanges, bWithForce);
		
		for (const FChangeKey& Key : FlushedChanges)
			ChangesFastData.Remove(Key);
		return true;
	}
	return false;	
}

bool FResolver::HasChanges() const
{
	bool HasChanges = false;
	for (const auto& AggregatorData : Aggregators)
		HasChanges |= AggregatorData.Value->HasChanges();
	return HasChanges;
}

bool FResolver::HasChanges(const FValueType& ValueType) const
{
	if (!ValueType.IsValid()) return false;
	if (const auto Aggregator = Aggregators.Find(ValueType))
		return (*Aggregator)->HasChanges();
	return false;
}

bool FResolver::HasChanges(const FValueType& ValueType, const uint8 Layer) const
{
	if (!ValueType.IsValid()) return false;
	if (const auto Aggregator = Aggregators.Find(ValueType))
		return (*Aggregator)->HasChanges(Layer);
	return false;
}