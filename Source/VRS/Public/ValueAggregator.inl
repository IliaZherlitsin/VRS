// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "ChangeContext.h"

template<typename TOwner, typename TValue, FValueSettings Settings>
bool TBaseValueAggregator<TOwner, TValue, Settings>::GrantChange(TUniquePtr<FChangeBase> Change,
	const FChangeContext& Context)
{
	if (!Change) return false;
	if (Settings.MaxLayers <= Context.Layer) return false;
	
#if !UE_BUILD_SHIPPING
	const auto* TypeA = Change->ValueType;
	if (const auto& TypeB = typeid(TValue); !TypeA || *TypeA != TypeB)
    {
		const auto NameA = TypeA ? FString(TypeA->name()) : FString(TEXT("null"));
		const auto NameB = FString(TypeB.name());
		UE_LOG(LogVRS, Error, TEXT("Failed to apply change of advert %s by %s in %s, value types are different A: %s B: %s"),
			AdvertName, *Context.ChangerName, *TargetName, *NameA, *NameB);
    	return false;
    }
#endif
	
	auto& Layer = Layers[Context.Layer];
	if (Layer.IsFull()) return false;
	return Layer.GrantChange(MoveTemp(Change));
}

template<typename TOwner, typename TValue, FValueSettings Settings>
bool TBaseValueAggregator<TOwner, TValue, Settings>::RemoveChange(const FChangeKey& Key, const uint8 Layer)
{
	if (!Key.IsValid()) return false;
	if (Settings.MaxLayers <= Layer) return false;
	return Layers[Layer].RemoveChange(Key);
}

template<typename TOwner, typename TValue, FValueSettings Settings>
bool TBaseValueAggregator<TOwner, TValue, Settings>::Flush(TArray<FChangeKey>& OutFlushed, const bool bWithForce)
{
	auto* MainValue = GetValue();
	if (!MainValue) return false;
	
	bool Flushed = false;
	for (auto& Layer : Layers)
	{
		Flushed |= Layer.Flush(*MainValue, OutFlushed, bWithForce);
	}
	return Flushed;
}
