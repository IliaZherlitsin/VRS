// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChangeContext.h"
#include "ValueType.h"

class FResolver;
struct FChangeContext;

/**
 * Type-erased base for internal value keys.
 *
 * Lets FResolver store keys of every value type in a single
 * TMap<FValueType, TSharedPtr<IValueKey>> regardless of the underlying T.
 */
struct VRS_API IValueKey {};

/**
 * Resolver-owned handle to a single registered value.
 *
 * Bundles everything needed to talk to the resolver about one value — the resolver
 * pointer, the value type and the owner object — so callers no longer pass an
 * FValueType into every AddChange / Flush / GetValue call.
 *
 * Instances are created and owned by FResolver::MakeInternalKey (stored as TSharedPtr
 * in the resolver's ValueKeys map). User code never holds one directly — it holds an
 * FValueKey, which weakly references this object and dies gracefully when the
 * resolver or the value goes away.
 *
 * @tparam  T  Type of the underlying value (int32, float, FVector, ...).
 *
 * @see FValueKey, FResolver::MakeKey
 */
template<typename T>
struct VRS_API TInternalValueKey : IValueKey
{
	template<typename InT>
	friend struct FValueKeyBase;

	friend class FResolver;

	TInternalValueKey() = default;

	/**
	 * Enqueues a pre-built change for this value.
	 *
	 * @param  Change         Owning pointer to the change. Ownership is transferred.
	 * @param  ChangeContext  Source descriptor (changer name + target layer).
	 * @param  OutKey         Optional. Receives the change key for later RemoveChange.
	 * @return                True if the change was accepted.
	 */
	bool AddChange(TUniquePtr<FChangeBase> Change, const FChangeContext& ChangeContext, FChangeKey* OutKey = nullptr) const;

	/**
	 * Convenience overload that builds a TChangeTyped<T> from a value and settings.
	 *
	 * @param  NewValue       Operand of the operation (e.g. amount to add).
	 * @param  Settings       Priority, behaviour flags and operation describing how to combine the value.
	 * @param  ChangeContext  Source descriptor (changer name + target layer).
	 * @param  OutKey         Optional. Receives the change key for later RemoveChange.
	 * @return                True if the change was accepted.
	 */
	bool AddChange(const T& NewValue, const FChangeSettings& Settings, const FChangeContext& ChangeContext, FChangeKey* OutKey = nullptr) const;

	/**
	 * Removes a previously queued (but not yet flushed) change by key.
	 *
	 * @param  Key  Change key received via the OutKey parameter of AddChange.
	 * @return      True if a matching change was found and removed.
	 */
	bool RemoveChange(const FChangeKey& Key) const;

	/**
	 * Applies all pending changes for this value.
	 *
	 * @param  bWithForce  If true, also flushes static (persistent) changes and clears them.
	 * @return             True if the resolver found a matching aggregator.
	 */
	bool Flush(const bool bWithForce) const;

	/** @return  Pointer to the current value on the live owner object, or null if unavailable. */
	[[nodiscard]] const T* GetValue() const;

	/** @return  Pointer to the value on the owning class's CDO (the pre-modification default), or null. */
	[[nodiscard]] const T* GetDefaultValue() const;

	/** @return  True if this value has at least one pending change. */
	[[nodiscard]] bool HasChanges() const;

	/** @return  True if the resolver, the value type and the owner object are all still valid. */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return Resolver != nullptr && ValueType.IsValid() && Target.IsValid();
	}
protected:
	/**
	 * Constructed by FResolver::MakeInternalKey only.
	 *
	 * In non-shipping builds the advert's recorded value type is checked against T;
	 * on mismatch the key resets itself to the invalid default state and logs an error.
	 *
	 * @param  InResolver   Resolver that owns this key. Outlives it by construction.
	 * @param  InTarget     UObject that owns the underlying value.
	 * @param  InValueType  Key identifying the registered value.
	 */
	TInternalValueKey(FResolver* InResolver, UObject* InTarget, const FValueType& InValueType)
		: ValueType(InValueType)
		, Resolver(InResolver)
		, Target(InTarget)
	{
#if !UE_BUILD_SHIPPING
		if (ValueType.ValueAdvert->ValueType != &typeid(T))
		{
			UE_LOG(LogVRS, Error, TEXT("Value key type mismatch for '%s'"), ValueType.ValueAdvert->AdvertName);
			*this = TInternalValueKey();
		}
#endif
	}

	/** Key identifying the registered value inside the resolver. */
	FValueType ValueType;

	/** Owning resolver. Never owned — the resolver owns this key, not the other way round. */
	FResolver* Resolver = nullptr;

	/** Weak pointer to the UObject that owns the underlying value. */
	TWeakObjectPtr<> Target = nullptr;
};

/**
 * Non-owning base for user-facing value keys.
 *
 * Holds a weak pointer to the resolver-owned TInternalValueKey plus (in non-shipping)
 * cached owner / advert names for error messages. When the resolver or the value dies,
 * IsValid() turns false and every operation on the derived key becomes a logged no-op
 * instead of a crash.
 *
 * @tparam  T  Type of the underlying value.
 */
template<typename T>
struct VRS_API FValueKeyBase
{
	FValueKeyBase() = default;

#if !UE_BUILD_SHIPPING
	#define VALUE_KEY_LOG_INVALID() \
		UE_LOG(LogVRS, Error, TEXT("Failed to change value %s in %s, target is invalid"), this->AdvertName, *this->TargetName);
#else
	#define VALUE_KEY_LOG_INVALID()
#endif


#if UE_BUILD_SHIPPING
	explicit FValueKeyBase(const TSharedPtr<TInternalValueKey<T>>& InternalValueKey)
		: InternalKey(InternalValueKey) {}
#else
	explicit FValueKeyBase(const TSharedPtr<TInternalValueKey<T>>& InternalValueKey)
		: InternalKey(InternalValueKey)
		, TargetName(InternalValueKey && InternalValueKey->Target.IsValid() ? InternalValueKey->Target->GetName() : TEXT("Invalid"))
		, AdvertName(InternalValueKey && InternalValueKey->ValueType.IsValid() ? InternalValueKey->ValueType.ValueAdvert->AdvertName : TEXT("Invalid")) {}
#endif

	/** @return  True if the referenced internal key is still alive in its resolver. */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return InternalKey.IsValid();
	}

protected:
	/** Weak reference to the resolver-owned internal key. */
	TWeakPtr<TInternalValueKey<T>> InternalKey;

#if !UE_BUILD_SHIPPING
	/** Display name of the owner object — cached for error messages. Stripped in shipping. */
	FString TargetName = TEXT("Unbound");

	/** Static advert name — cached for error messages. Stripped in shipping. */
	const TCHAR* AdvertName = TEXT("Unbound");
#endif
};

/**
 * User-facing handle to a registered value with natural operator syntax.
 *
 * Wraps a fixed priority and change context so that mutations read like plain
 * assignments — each operator enqueues a transient change through the resolver:
 *
 * @code
 * // Ready-bound, shared — the usual way:
 * TSharedPtr<FValueKey<float>> Health = Resolver->MakeKey<float>(HealthType, 10, FChangeContext(this, 1));
 *
 * *Health += 25.f;             // queue Add
 * *Health *= 1.5f;             // queue Mul
 * Health->Flush(false);        // apply
 * if (const float* Current = Health->Get()) { ... }
 *
 * // Or bind a member-held key manually:
 * FValueKey<float> Key;
 * Key.Bind(Resolver->MakeInternalKey<float>(HealthType), 10, FChangeContext(this, 1));
 * @endcode
 *
 * All operations are safe on an unbound / expired key: they log an error and
 * return a neutral result instead of dereferencing dead memory.
 *
 * @tparam  T  Type of the underlying value.
 *
 * @note Non-copyable — use Bind() to share a target between keys, which also
 *       copies the priority and context.
 */
template<typename T>
struct VRS_API FValueKey : FValueKeyBase<T>
{
	FValueKey() = default;
	FValueKey(const FValueKey&) = delete;
	FValueKey& operator=(const FValueKey&) = delete;

	/**
	 * @param  InternalValueKey  Resolver-owned internal key to reference.
	 * @param  InPriority        Priority applied to every change queued via the operators.
	 * @param  InContext         Change context (changer + layer) applied to every queued change.
	 */
	FValueKey(const TSharedPtr<TInternalValueKey<T>>& InternalValueKey, const uint8 InPriority, const FChangeContext& InContext)
		: FValueKeyBase<T>(InternalValueKey)
		, Priority(InPriority)
		, Context(InContext) {}

	/**
	 * Rebinds this key to the same target, priority and context as another key.
	 *
	 * @return  This key, for chaining.
	 */
	FORCEINLINE FValueKey& Bind(const FValueKey& Other)
	{
		this->InternalKey = Other.InternalKey;
		Priority = Other.Priority;
		Context = Other.Context;
		return *this;
	}

	/**
	 * Rebinds this key to a new internal key with a new priority and context.
	 *
	 * @param  InternalValueKey  Resolver-owned internal key to reference.
	 * @param  InPriority        Priority applied to every change queued via the operators.
	 * @param  InContext         Change context (changer + layer) applied to every queued change.
	 * @return                   This key, for chaining.
	 */
	FORCEINLINE FValueKey& Bind(const TSharedPtr<TInternalValueKey<T>>& InternalValueKey,
		const uint8 InPriority, const FChangeContext& InContext)
	{
		this->InternalKey = InternalValueKey;
		Priority = InPriority;
		Context = InContext;
		return *this;
	}

/**
 * Declares a named method + compound operator pair that both enqueue a transient
 * change using the key's stored priority and context. The named form (Set, Add, ...)
 * reports success; the operator form returns *this for chaining.
 */
#define DECLARE_OPERATOR(Operator, OperatorName, OperatorType) \
	FORCEINLINE bool OperatorName(const T& Value) \
	{ \
		if (!this->IsValid()) \
		{ \
			VALUE_KEY_LOG_INVALID() \
			return false; \
		} \
		const FChangeSettings Settings(Priority, OperatorType); \
		return this->InternalKey.Pin()->AddChange(Value, Settings, Context); \
	} \
	FORCEINLINE FValueKey& operator Operator (const T& Value) \
	{ \
		if (!this->IsValid()) \
		{ \
			VALUE_KEY_LOG_INVALID() \
			return *this; \
		} \
		const FChangeSettings Settings(Priority, OperatorType); \
		this->InternalKey.Pin()->AddChange(Value, Settings, Context); \
		return *this; \
	}

	DECLARE_OPERATOR(=,  Set, EValueOperation::Set)
	DECLARE_OPERATOR(+=, Add, EValueOperation::Add)
	DECLARE_OPERATOR(-=, Sub, EValueOperation::Sub)
	DECLARE_OPERATOR(*=, Mul, EValueOperation::Mul)
	DECLARE_OPERATOR(/=, Div, EValueOperation::Div)

	/**
	 * Enqueues a change with explicit settings, using the key's stored context.
	 *
	 * @param  Value     Operand of the operation.
	 * @param  Settings  Priority, behaviour flags and operation for this specific change.
	 * @return           True if the change was accepted.
	 */
	FORCEINLINE bool operator()(const T& Value, const FChangeSettings& Settings) const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return false;
		}
		return this->InternalKey.Pin()->AddChange(Value, Settings, Context);
	}

	/**
	 * Enqueues a change with explicit settings and an explicit context override.
	 *
	 * @param  Value      Operand of the operation.
	 * @param  Settings   Priority, behaviour flags and operation for this specific change.
	 * @param  InContext  Context (changer + layer) used instead of the key's stored one.
	 * @return            True if the change was accepted.
	 */
	FORCEINLINE bool operator()(const T& Value, const FChangeSettings& Settings, const FChangeContext& InContext) const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return false;
		}
		return this->InternalKey.Pin()->AddChange(Value, Settings, InContext);
	}

	/** @return  Pointer to the current live value for member access, or null if the key is invalid. */
	FORCEINLINE [[nodiscard]] const T* operator->() const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return nullptr;
		}
		return this->InternalKey.Pin()->GetValue();
	}

	/** @return  Pointer to the current live value, or null if unavailable. */
	FORCEINLINE const T* Get() const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return nullptr;
		}
		if (const T* Value = this->InternalKey.Pin()->GetValue()) return Value;
		UE_LOG(LogVRS, Error, TEXT("Failed to get current value of %s in %s"), this->AdvertName, *this->TargetName);
		return nullptr;
	}

	/** @return  Pointer to the value on the owning class's CDO, or null if unavailable. */
	FORCEINLINE [[nodiscard]] const T* GetDefault() const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return nullptr;
		}
		if (const T* Value = this->InternalKey.Pin()->GetDefaultValue()) return Value;
		UE_LOG(LogVRS, Error, TEXT("Failed to get default value of %s in %s"), this->AdvertName, *this->TargetName);
		return nullptr;
	}

	/** @return  True if this value has at least one pending change. */
	FORCEINLINE [[nodiscard]] bool HasChanges() const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return false;
		}
		return this->InternalKey.Pin()->HasChanges();
	}

	/**
	 * Removes a previously queued (but not yet flushed) change by key.
	 *
	 * @param  Key  Change key received via AddChange's OutKey parameter.
	 * @return      True if a matching change was found and removed.
	 */
	FORCEINLINE bool RemoveChange(const FChangeKey& Key) const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return false;
		}
		return this->InternalKey.Pin()->RemoveChange(Key);
	}

	/**
	 * Applies all pending changes for this value.
	 *
	 * @param  bWithForce  If true, also flushes static (persistent) changes and clears them.
	 * @return             True if the resolver found a matching aggregator.
	 */
	FORCEINLINE bool Flush(const bool bWithForce) const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return false;
		}
		return this->InternalKey.Pin()->Flush(bWithForce);
	}

	/**
	 * Queues a Set change that resets the value to its CDO default.
	 *
	 * Uses the key's stored priority and context; the change still goes through the
	 * normal layer/priority pipeline, so higher-priority changes can override it.
	 *
	 * @return  True if the reset change was accepted.
	 */
	FORCEINLINE bool SetDefault() const
	{
		if (!this->IsValid())
		{
			VALUE_KEY_LOG_INVALID()
			return false;
		}
		if (auto Value = this->InternalKey.Pin()->GetDefaultValue())
		{
			const FChangeSettings Settings(Priority, EValueOperation::Set);
			return this->InternalKey.Pin()->AddChange(*Value, Settings, Context);
		}
		return false;
	}

protected:
	/** Priority applied to every change queued through the operators. */
	uint8 Priority = 0;

	/** Change context (changer + target layer) applied to every queued change. */
	FChangeContext Context;
};

#undef DECLARE_OPERATOR
#undef VALUE_KEY_LOG_INVALID
