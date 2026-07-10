// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LayerPackage.h"
#include "Change.h"
#include "ValueSettings.h"

/**
 * Type-erased interface used by FResolver to address every registered value uniformly.
 *
 * Concrete implementations live in TBaseValueAggregator and its specialisations.
 * The resolver stores aggregators as TUniquePtr<IValueAggregator> in a single map
 * regardless of the underlying TOwner / TValue types.
 */
struct IValueAggregator
{
	virtual ~IValueAggregator() = default;

	/** @return  True if both the owner object and the value advert are still valid. */
	FORCEINLINE [[nodiscard]] virtual bool IsValid() const = 0;

	/** @return  True if any layer holds at least one queued change. */
	FORCEINLINE virtual bool HasChanges() const = 0;

	/** @return  True if the given layer holds at least one queued change. */
	FORCEINLINE virtual bool HasChanges(const uint8) const = 0;

	/**
	 * Applies every pending change across all layers to the underlying value.
	 *
	 * @param  OutFlushed  Receives the keys of every change that left its layer, so the
	 *                     resolver can retire their bookkeeping. Keys of surviving static
	 *                     changes are not reported.
	 * @param  bWithForce  If true, also flushes static changes and clears layers entirely.
	 * @return             True if at least one change was applied.
	 */
	FORCEINLINE virtual bool Flush(TArray<FChangeKey>& OutFlushed, const bool bWithForce) = 0;

	/**
	 * Routes a change to the layer specified by the context.
	 *
	 * @return  True if the change was queued. False if the context is invalid, the
	 *          target layer is full, or (in non-shipping) the change value type does
	 *          not match the aggregator value type.
	 */
	FORCEINLINE virtual bool GrantChange(TUniquePtr<FChangeBase>, const FChangeContext&) = 0;

	/**
	 * Removes a previously queued (but not yet flushed) change from the given layer.
	 *
	 * @return  True if a matching change was found and removed.
	 */
	FORCEINLINE virtual bool RemoveChange(const FChangeKey&, const uint8) = 0;
};

/**
 * Common implementation shared by direct and nested value aggregators.
 *
 * Holds a weak pointer to the owner UObject and a fixed-size array of TLayerPackage,
 * one per priority layer. Subclasses provide the actual GetValue() that points at the
 * correct member of the owner.
 *
 * @tparam  TOwner       UObject-derived owner of the value.
 * @tparam  TValue       Type of the value being modified.
 * @tparam  Settings     Value layer settings. (Number of changes per layer, number of layers)
 */
template<typename TOwner, typename TValue, FValueSettings Settings>
struct TBaseValueAggregator : IValueAggregator
{
	friend class FResolver;

#if !UE_BUILD_SHIPPING
	/**
	 * Non-shipping constructor — captures target / resolver / advert names so each
	 * recorded change can be traced back to its source in the debug system.
	 *
	 * @param  Target          UObject that owns the underlying value.
	 * @param  TargetName      Display name of the target (from its FTargetKey) used in logs.
	 * @param  InResolverName  Display name of the resolver this aggregator belongs to.
	 * @param  InAdvertName    Static advert name used as a stable identifier in logs.
	 */
	explicit TBaseValueAggregator(TOwner& Target, const FString& TargetName, const FString& InResolverName, const TCHAR* InAdvertName)
		: TargetName(TargetName)
		, AdvertName(InAdvertName)
		, Target(&Target)
	{
		for (auto Index = 0; Index < Settings.MaxLayers; ++Index)
		{
			auto& Layer = Layers[Index];
			Layer.ResolverName = InResolverName;
			Layer.AdvertName = AdvertName;
			Layer.TargetName = TargetName;
			Layer.Layer = Index;
		}
	}
#else
	/**
	 * Shipping constructor — only the owner is needed; debug fields are stripped.
	 *
	 * @param  Owner  UObject that owns the underlying value.
	 */
	explicit TBaseValueAggregator(TOwner& Owner)
		: Target(&Owner)
	{
		for (auto Index = 0; Index < Settings.MaxLayers; ++Index)
			Layers[Index].Layer = Index;
	}
#endif

	virtual bool GrantChange(TUniquePtr<FChangeBase> Change, const FChangeContext& Context) override;
	virtual bool RemoveChange(const FChangeKey& Key, const uint8 Layer) override;

	virtual bool Flush(TArray<FChangeKey>& OutFlushed, const bool bWithForce) override;

	FORCEINLINE virtual bool HasChanges() const override
	{
		for (auto& Layer : Layers)
			if (Layer.HasChanges()) return true;
		return false;
	}
	FORCEINLINE virtual bool HasChanges(const uint8 Layer) const override
	{
		if (Settings.MaxLayers <= Layer) return false;
		return Layers[Layer].HasChanges();
	}
protected:
	/** @return  Pointer to the underlying value on the owner object, or null if unavailable. */
	[[nodiscard]] virtual TValue* GetValue() const = 0;

#if !UE_BUILD_SHIPPING
	/** Display name of the owner — captured at construction. Stripped in shipping. */
	FString TargetName;

	/** Static advert name used as a stable identifier in logs. Stripped in shipping. */
	const TCHAR* AdvertName;
#endif

	/** Weak pointer to the UObject that owns the underlying value. */
	TWeakObjectPtr<TOwner> Target;

	/** Per-layer storage for queued changes. Size is fixed at compile time. */
	TStaticArray<TLayerPackage<TValue, Settings.MaxChanges>, Settings.MaxLayers> Layers;
};


/**
 * Aggregator for a direct member of a UObject.
 *
 * Resolves the underlying value via a TValueAdvert that points at a single member
 * pointer (TOwner::*). Created by FResolver::RegisterValue().
 *
 * @tparam  TOwner       UObject-derived owner of the value.
 * @tparam  TValue       Type of the member being modified.
 * @tparam  Settings     Value layer settings. (Number of changes per layer, number of layers)
 */
template<typename TOwner, typename TValue, FValueSettings Settings>
struct TValueAggregator : TBaseValueAggregator<TOwner, TValue, Settings>
{
	friend class FResolver;

#if !UE_BUILD_SHIPPING
	/**
	 * Non-shipping constructor — forwards owner / advert / resolver name to the base.
	 *
	 * @param  Owner          UObject that owns the underlying value.
	 * @param  InValueAdvert  Compile-time descriptor of the member to modify.
	 * @param  ResolverName   Display name of the resolver this aggregator belongs to.
	 */
	TValueAggregator(TOwner& Owner, const TValueAdvert<TOwner, TValue>& InValueAdvert, const FString& TargetName, const FString& ResolverName)
		: TBaseValueAggregator<TOwner, TValue, Settings>(Owner, TargetName, ResolverName, InValueAdvert.AdvertName)
		, ValueAdvert(InValueAdvert) {}
#else
	/**
	 * Shipping constructor — debug fields are stripped, ResolverName is ignored.
	 *
	 * @param  Owner          UObject that owns the underlying value.
	 * @param  InValueAdvert  Compile-time descriptor of the member to modify.
	 * @param  ResolverName   Unused in shipping. Present for API parity.
	 */
	TValueAggregator(TOwner& Owner, const TValueAdvert<TOwner, TValue>& InValueAdvert, const FString& TargetName, const FString& ResolverName)
		: TBaseValueAggregator<TOwner, TValue, Settings>(Owner)
		, ValueAdvert(InValueAdvert) {}
#endif

	virtual bool IsValid() const override { return this->Target.IsValid() && ValueAdvert.IsValid(); }
	virtual TValue* GetValue() const override { return ValueAdvert.GetValue(this->Target.Get()); }
protected:
	/** Compile-time descriptor of the member being modified. */
	TValueAdvert<TOwner, TValue> ValueAdvert;
};

/**
 * Aggregator for a value nested inside an embedded struct member of a UObject.
 *
 * Resolves the underlying value via a TInnerValueAdvert that follows two member
 * pointers: TOwner -> TInner -> TValue. Created by FResolver::RegisterValue().
 *
 * @tparam  TOwner       UObject-derived owner of the outer struct member.
 * @tparam  TInner       Type of the intermediate struct.
 * @tparam  TValue       Type of the target value inside TInner.
 * @tparam  Settings     Value layer settings. (Number of changes per layer, number of layers)
 */
template<typename TOwner, typename TInner, typename TValue, FValueSettings Settings>
struct TInnerValueAggregator : TBaseValueAggregator<TOwner, TValue, Settings>
{
	friend class FResolver;

#if !UE_BUILD_SHIPPING
	/**
	 * Non-shipping constructor — forwards owner / advert / resolver name to the base.
	 *
	 * @param  Owner          UObject that owns the outer struct member.
	 * @param  InValueAdvert  Compile-time descriptor of the nested member to modify.
	 * @param  ResolverName   Display name of the resolver this aggregator belongs to.
	 */
	TInnerValueAggregator(TOwner& Owner, const TInnerValueAdvert<TOwner, TInner, TValue>& InValueAdvert, const FString& TargetName, const FString& ResolverName)
		: TBaseValueAggregator<TOwner, TValue, Settings>(Owner, TargetName, ResolverName, InValueAdvert.AdvertName)
		, ValueAdvert(InValueAdvert) {}
#else
	/**
	 * Shipping constructor — debug fields are stripped, ResolverName is ignored.
	 *
	 * @param  Owner          UObject that owns the outer struct member.
	 * @param  InValueAdvert  Compile-time descriptor of the nested member to modify.
	 * @param  ResolverName   Unused in shipping. Present for API parity.
	 */
	TInnerValueAggregator(TOwner& Owner, const TInnerValueAdvert<TOwner, TInner, TValue>& InValueAdvert, const FString& TargetName, const FString& ResolverName)
		: TBaseValueAggregator<TOwner, TValue, Settings>(Owner)
		, ValueAdvert(InValueAdvert) {}
#endif

	virtual bool IsValid() const override { return this->Target.IsValid() && ValueAdvert.IsValid(); }
	virtual TValue* GetValue() const override { return ValueAdvert.GetValue(this->Target.Get()); }
protected:
	/** Compile-time descriptor of the nested member being modified. */
	TInnerValueAdvert<TOwner, TInner, TValue> ValueAdvert;
};

#include "ValueAggregator.inl"
