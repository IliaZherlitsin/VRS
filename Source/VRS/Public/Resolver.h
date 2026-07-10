// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Change.h"
#include "ValueKey.h"
#include "TargetData.h"
#include "TargetKey.h"
#include "ValueSettings.h"
#include "ValueAggregator.h"
#include "ValueType.h"

struct FValueBatch;
struct FChangeKey;

struct FChangeFastData
{
	uint8 Layer = 0;
	FValueType ValueType;
};

/**
 * Central change-management system of the VRS plugin.
 *
 * The resolver owns a set of registered objects and value aggregators, accepts pending
 * changes, and applies them on demand via Flush. Each registered value becomes a typed
 * aggregator that stacks changes across priority layers and produces a final value.
 *
 * Typical usage flow:
 *   1. Construct with an owning UObject — its name identifies the resolver in debug logs.
 *   2. Call RegisterObject() for any UObject you want to mutate.
 *   3. Call RegisterValue() to expose a member (or nested member) for modification.
 *   4. Call AddChange() / AddBatch() to enqueue changes (or use MakeKey / FValueKey).
 *   5. Call Flush() to apply pending changes to the underlying values.
 *
 * The resolver periodically purges invalid objects and dead aggregators every ClearInterval seconds.
 *
 * @note Non-copyable. One resolver instance is expected per logical subsystem (e.g. one
 *       per game mode, one per player).
 */
class VRS_API FResolver
{
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVRSCleanupTest;
#endif
	
	template <typename T>
	T* FindObject(const FTargetKey& Key);
	
	UObject* FindObject(const FTargetKey& Key);

	void HandleEndFrame();
	void ClearResolver();

	double InClearInterval = 30.f;
	double AccumulatedTime = 0.0;
	FDelegateHandle EndFrameHandle;

	/** Display name used by the debug system — built as "Resolver" + the owning UObject's name. */
	FString ResolverName;

	/** Weak reference to the UObject that owns this resolver and anchors its lifetime. */
	TWeakObjectPtr<> Owner;

	/** Objects whose values this resolver manages. Candidates for RegisterValue lookups. */
	TMap<FTargetKey, FTargetData> Targets;

	/** Live internal value keys handed out via MakeKey, one per registered value. */
	TMap<FValueType, TSharedPtr<IValueKey>> ValueKeys;

	/** Fast lookup data (target layer + value type) for every queued, not-yet-flushed change. */
	TMap<FChangeKey, FChangeFastData> ChangesFastData;

	/** Live aggregators for every value this resolver manages, keyed by value type. */
	TMap<FValueType, TUniquePtr<IValueAggregator>> Aggregators;
public:
	FResolver(const FResolver&) = delete;
	FResolver& operator=(const FResolver&) = delete;

	FResolver() = default;
	
	/**
	 * Construct a resolver bound to an owning UObject with a custom periodic-cleanup interval.
	 *
	 * The owner anchors the resolver's lifetime and supplies the resolver name used by the
	 * debug system to tag every recorded change with its source.
	 *
	 * @param  InOwner          UObject that owns this resolver. Stored as a weak pointer; its
	 *                          name is used as the resolver identifier in debug output.
	 * @param  InClearInterval  Seconds between automatic purges of invalid objects/aggregators.
	 *                          Values <= 0 fall back to the default of 30 seconds.
	 */
	explicit FResolver(UObject& InOwner, const double InClearInterval);
	virtual ~FResolver();
	
	/**
	 * Registers a direct member of a UObject as a mutable value.
	 *
	 * Creates a typed aggregator backed by the given advert and the previously registered
	 * owner object. Build an FValueType from the same advert / target key pair to address
	 * the value in AddChange / RemoveChange / Flush.
	 *
	 * @tparam  TOwner       UObject-derived type that owns the value.
	 * @tparam  TValue       Type of the value being modified (int32, float, FVector, ...).
	 * @tparam  Settings     Value layer settings. (Number of changes per layer, number of layers)
	 *
	 * @param  Advert       Compile-time descriptor of the member to modify.
	 * @param  Key          Target key from RegisterObject. Default {} resolves the first
	 *                      registered object of matching class.
	 * @return              True if the value is registered (including when it already was).
	 *                      False if the owner object cannot be found.
	 *
	 * @see RegisterObject, AddChange
	 */
	template<typename TOwner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
	bool RegisterValue(const TValueAdvert<TOwner, TValue>& Advert, const FTargetKey& Key);
	
	
	/**
	 * Registers a direct member value from a pre-built FValueType.
	 *
	 * Useful when the FValueType is declared once (e.g. as an inline constant in a header)
	 * and shared across multiple systems so that every caller refers to the exact same key.
	 *
	 * @tparam  TOwner       UObject-derived type that owns the value.
	 * @tparam  TValue       Type of the value being modified.
	* @tparam   Settings     Value layer settings. (Number of changes per layer, number of layers)
	 *
	 * @param  ValueType  Pre-built key produced from the same advert / target key pair.
	 * @return            True if the value is registered (including when it already was).
	 *                    False if the value type is invalid or its owner cannot be found.
	 */
	template<typename TOwner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
	bool RegisterValue(const FValueType& ValueType);
	
	
	/**
	 * Registers a nested (member-of-member) value of a UObject as a mutable value.
	 *
	 * Same as the overload above, but follows two member pointers: TOwner -> TInner -> TValue.
	 * Useful when the value lives inside an embedded struct of the owner.
	 *
	 * @param  Advert       Compile-time descriptor of the nested member to modify.
	 * @param  Key          Target key from RegisterObject. Default {} resolves the first
	 *                      registered object of matching class.
	 * @return              True if the value is registered (including when it already was).
	 *                      False if the owner object cannot be found.
	 */
	template<typename TOwner, typename TInner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
	bool RegisterValue(const TInnerValueAdvert<TOwner, TInner, TValue>& Advert, const FTargetKey& Key);
	
	
	/**
	 * Registers a nested (member-of-member) value from a pre-built FValueType.
	 *
	 * Same as the direct overload above, but follows two member pointers:
	 * TOwner -> TInner -> TValue.
	 *
	 * @tparam  TOwner       UObject-derived type that owns the outer struct member.
	 * @tparam  TInner       Type of the intermediate struct.
	 * @tparam  TValue       Type of the target value inside TInner.
	* @tparam   Settings     Value layer settings. (Number of changes per layer, number of layers)
	 *
	 * @param  ValueType  Pre-built key produced from the same advert / target key pair.
	 * @return            True if the value is registered (including when it already was).
	 *                    False if the value type is invalid or its owner cannot be found.
	 */
	template<typename TOwner, typename TInner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
	bool RegisterValue(const FValueType& ValueType);

	
	
	/**
	 * Registers a UObject as a candidate owner for subsequent RegisterValue calls.
	 *
	 * Returns a unique target key identifying this object. Pass it to RegisterValue /
	 * FValueType to address this specific object, even when several objects of the same
	 * class are registered at once.
	 *
	 * @param  Object  The UObject to track. Stored as a weak pointer.
	 * @param  Name    Optional human-readable label for debug logs (stripped in shipping).
	 * @return         Unique key for addressing this object. Store it if you register
	 *                 more than one object of the same class.
	 */
	FTargetKey RegisterObject(UObject* Object, const FString& Name = {});
	
	
	/**
	 * Enqueues a pre-built change for the given registered value.
	 *
	 * The change is not applied immediately; it is queued in the appropriate layer and
	 * merged into the underlying value during the next Flush.
	 *
	 * @param  ValueType      Key returned from RegisterValue.
	 * @param  Change         Owning pointer to the change. Ownership is transferred.
	 * @param  ChangeContext  Source descriptor (changer name + target layer).
	 * @param  OutKey         Optional. Receives the change key of the queued change for later RemoveChange.
	 * @return                True if the change was accepted; false if input is invalid or the layer is full.
	 *
	 * @see AddBatch, RemoveChange, Flush
	 */
	bool AddChange(const FValueType& ValueType, TUniquePtr<FChangeBase> Change, const FChangeContext& ChangeContext, FChangeKey* OutKey = nullptr);

	/**
	 * Convenience overload that builds a TChangeTyped<T> from a value and settings.
	 *
	 * @tparam T              Value type. Must have a TChangeTyped specialisation.
	 * @param  ValueType      Key returned from RegisterValue.
	 * @param  NewValue       Operand of the operation (e.g. amount to add).
	 * @param  Settings       Priority, behaviour flags and operation describing how to combine the value.
	 * @param  ChangeContext  Source descriptor (changer name + target layer).
	 * @param  OutKey         Optional. Receives the change key of the queued change for later RemoveChange.
	 * @return                True if the change was accepted; false otherwise.
	 */
	template<typename T>
	bool AddChange(const FValueType& ValueType, const T& NewValue, const FChangeSettings& Settings, const FChangeContext& ChangeContext, FChangeKey* OutKey = nullptr);
	
	/**
	 * Applies every change accumulated in the batch atomically and empties it.
	 *
	 * Each entry of the batch is routed to its registered aggregator. Entries whose
	 * value type is not registered are silently skipped.
	 *
	 * @param  Batch  Batch of changes. Always emptied on return, even on partial failure.
	 * @return        Number of changes that were successfully accepted by their aggregators.
	 *
	 * @see FChangeBatch
	 */
	int32 AddBatch(FChangeBatch& Batch);
	
	int32 AddValueBatch(FValueBatch& Batch);
	
	/**
	 * Returns the resolver-owned internal key for a registered value, creating it on first use.
	 *
	 * Keys are cached in ValueKeys — repeated calls for the same value return the same
	 * instance. In non-shipping builds the advert's recorded value type is checked
	 * against TValue and a mismatch returns null with an error.
	 *
	 * @tparam  TValue     Type of the underlying value.
	 * @param   ValueType  Key returned from / used with RegisterValue.
	 * @return             Shared internal key, or null if the value is not registered,
	 *                     the type does not match, or the owner object cannot be found.
	 *
	 * @see FValueKey::Bind
	 */
	template<typename TValue>
	TSharedPtr<TInternalValueKey<TValue>> MakeInternalKey(const FValueType& ValueType);

	/**
	 * Creates a ready-bound user-facing key for a registered value.
	 *
	 * Convenience wrapper around the internal-key overload: resolves the internal key
	 * and constructs a TKey bound to it with the given priority and context.
	 *
	 * @tparam  TValue     Type of the underlying value.
	 * @tparam  TKey       User-facing key type. Must derive from FValueKeyBase<TValue>
	 *                     and be constructible from (internal key, priority, context).
	 * @param   ValueType  Key returned from / used with RegisterValue.
	 * @param   Priority   Priority applied to every change queued via the key's operators.
	 * @param   Context    Change context (changer + layer) applied to every queued change.
	 * @return             Shared user-facing key, or null on failure.
	 */
	template<typename TValue, typename TKey = FValueKey<TValue>> requires std::is_base_of_v<FValueKeyBase<TValue>, TKey>
	TSharedPtr<TKey> MakeKey(const FValueType& ValueType, const uint8 Priority = 0, const FChangeContext& Context = {});
	
	/**
	 * Removes a previously queued (but not yet flushed) change by key.
	 *
	 * @param  Key  change key returned via the OutKey parameter of AddChange.
	 * @return      True if a matching change was found and removed.
	 */
	bool RemoveChange(const FChangeKey& Key);
	
	/**
	 * Applies all pending changes across every registered value.
	 *
	 * @param  bWithForce  If true, also flushes static (persistent) changes and clears them
	 *                     from their layers. If false, static changes remain in place.
	 * @return             True if at least one change was flushed.
	 */
	bool Flush(const bool bWithForce);

	/**
	 * Applies all pending changes for a single registered value.
	 *
	 * @param  ValueType   Key returned from RegisterValue.
	 * @param  bWithForce  See Flush(bool).
	 * @return             True if a matching aggregator was found.
	 */
	bool Flush(const FValueType& ValueType, const bool bWithForce);
	

	/**
	 * Reads the current value of a registered value on its live target object.
	 *
	 * Does not require a bound key — looks the target up by the value type's stored
	 * FTargetKey on every call. In non-shipping builds the advert's recorded value
	 * type is checked against TValue, and the resolved object's class is checked
	 * against the advert's owner class.
	 *
	 * @tparam  TValue     Type of the underlying value.
	 * @param   ValueType  Key returned from / used with RegisterValue.
	 * @return             Pointer to the live value, or null if the value type is
	 *                     invalid, the type does not match, or the target cannot be found.
	 */
	template<typename TValue>
	[[nodiscard]] const TValue* GetValue(const FValueType& ValueType);

	/**
	 * Reads the value of a registered value on its owning class's CDO (the default,
	 * pre-modification value), regardless of any live target instance.
	 *
	 * @tparam  TValue     Type of the underlying value.
	 * @param   ValueType  Key returned from / used with RegisterValue.
	 * @return             Pointer to the value on the CDO, or null if the value type is
	 *                     invalid, the type does not match, or the CDO cannot be found.
	 */
	template<typename TValue>
	[[nodiscard]] static const TValue* GetDefaultValue(const FValueType& ValueType);

	/**
	 * Reads the current value of a direct member through its advert and a target key,
	 * without requiring the value to be registered first.
	 *
	 * @tparam  TOwner  UObject-derived type that owns the value.
	 * @tparam  TValue  Type of the value being read.
	 * @param   Advert  Compile-time descriptor of the member to read.
	 * @param   Key     Target key from RegisterObject. Default {} resolves the first
	 *                  registered object of matching class.
	 * @return          Pointer to the live value, or null if the advert is invalid or
	 *                  the target cannot be found.
	 */
	template<typename TOwner, typename TValue>
	[[nodiscard]] FORCEINLINE const TValue* GetValue(const TValueAdvert<TOwner, TValue>& Advert, const FTargetKey& Key)
	{
		if (!Advert.IsValid()) return nullptr;
		if (auto Object = FindObject<TOwner>(Key))
			return Advert.GetValue(Object);
		return nullptr;
	}

	/**
	 * Reads a direct member's value on its owning class's CDO through the advert alone,
	 * without needing a target key or a registered value.
	 *
	 * @tparam  TOwner  UObject-derived type that owns the value.
	 * @tparam  TValue  Type of the value being read.
	 * @param   Advert  Compile-time descriptor of the member to read.
	 * @return          Pointer to the value on the CDO, or null if the advert is invalid
	 *                  or the CDO cannot be found.
	 */
	template<typename TOwner, typename TValue>
	[[nodiscard]] FORCEINLINE const TValue* GetDefaultValue(const TValueAdvert<TOwner, TValue>& Advert)
	{
		if (!Advert.IsValid()) return nullptr;
		if (const auto Class = Advert.GetObjectClass())
		{
			if (const auto CDO = Class->template GetDefaultObject<UObject>())
				return Advert.GetValue(static_cast<TOwner*>(CDO));
		}
		return nullptr;
	}

	/**
	 * Reads the current value of a nested (member-of-member) value through its advert
	 * and a target key, without requiring the value to be registered first.
	 *
	 * @tparam  TOwner  UObject-derived type that owns the outer struct member.
	 * @tparam  TInner  Type of the intermediate struct.
	 * @tparam  TValue  Type of the target value inside TInner.
	 * @param   Advert  Compile-time descriptor of the nested member to read.
	 * @param   Key     Target key from RegisterObject. Default {} resolves the first
	 *                  registered object of matching class.
	 * @return          Pointer to the live value, or null if the advert is invalid or
	 *                  the target cannot be found.
	 */
	template<typename TOwner, typename TInner, typename TValue>
	[[nodiscard]] FORCEINLINE const TValue* GetValue(const TInnerValueAdvert<TOwner, TInner, TValue>& Advert, const FTargetKey& Key)
	{
		if (!Advert.IsValid()) return nullptr;
		if (auto Object = FindObject<TOwner>(Key))
			return Advert.GetValue(Object);
		return nullptr;
	}

	/**
	 * Reads a nested (member-of-member) value on its owning class's CDO through the
	 * advert alone, without needing a target key or a registered value.
	 *
	 * @tparam  TOwner  UObject-derived type that owns the outer struct member.
	 * @tparam  TInner  Type of the intermediate struct.
	 * @tparam  TValue  Type of the target value inside TInner.
	 * @param   Advert  Compile-time descriptor of the nested member to read.
	 * @return          Pointer to the value on the CDO, or null if the advert is invalid
	 *                  or the CDO cannot be found.
	 */
	template<typename TOwner, typename TInner, typename TValue>
	[[nodiscard]] FORCEINLINE const TValue* GetDefaultValue(const TInnerValueAdvert<TOwner, TInner, TValue>& Advert)
	{
		if (!Advert.IsValid()) return nullptr;
		if (const auto Class = Advert.GetObjectClass())
		{
			if (const auto CDO = Class->template GetDefaultObject<UObject>())
				return Advert.GetValue(static_cast<TOwner*>(CDO));
		}
		return nullptr;
	}

	/** @return  True if any registered value has at least one pending change. */
	[[nodiscard]] bool HasChanges() const;

	/** @return  True if the given registered value has at least one pending change. */
	[[nodiscard]] bool HasChanges(const FValueType& ValueType) const;

	/** @return  True if the given registered value has at least one pending change on the given layer. */
	[[nodiscard]] bool HasChanges(const FValueType& ValueType, const uint8 Layer) const;
	
	/** @return  True if the change key corresponds to a known, not-yet-flushed change. */
	[[nodiscard]] FORCEINLINE bool IsKeyValid(const FChangeKey& Key) const
	{
		return ChangesFastData.Contains(Key);
	}

	/** @return  True if the given value type has a live aggregator in this resolver. */
	[[nodiscard]] FORCEINLINE bool IsRegistered(const FValueType& ValueType) const
	{
		return Aggregators.Contains(ValueType);
	}
};

#include "Resolver.inl"
#include "ValueKey.inl"