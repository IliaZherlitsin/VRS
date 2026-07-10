// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

template <typename T>
T* FResolver::FindObject(const FTargetKey& Key)
{
	if (const auto Target = Targets.Find(Key))
		return Cast<T>(Target->GetTarget());
	return nullptr;
}

template <typename T>
bool FResolver::AddChange(const FValueType& ValueType, const T& NewValue, const FChangeSettings& Settings,
	const FChangeContext& ChangeContext, FChangeKey* OutKey)
{
	if (!ValueType.IsValid() || !Settings.IsValid() || !ChangeContext.IsValid()) return false;
	if (const auto Aggregator = Aggregators.Find(ValueType))
	{
		const auto NewKey = FChangeKey::New();
		auto Change = MakeUnique<TChangeTyped<T>>(NewValue, NewKey, Settings);
		
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

template <typename TOwner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
bool FResolver::RegisterValue(const TValueAdvert<TOwner, TValue>& Advert, const FTargetKey& Key)
{
	const FValueType ValueType(&Advert, Key);
	if (IsRegistered(ValueType)) return true;
	if (auto Object = FindObject<TOwner>(Key))
	{
		auto Aggregator = MakeUnique<TValueAggregator<TOwner, TValue, Settings>>(*Object, Advert, ValueType.TargetKey.ToString(), ResolverName);
		Aggregators.Add(ValueType, TUniquePtr<IValueAggregator>(Aggregator.Release()));
		return true;
	}

#if !UE_BUILD_SHIPPING
	const auto ClassName = TOwner::StaticClass()->GetName();
	UE_LOG(LogVRS, Error, TEXT("Failed to find a target object of class %s for key %s, advert %s"), *ClassName, *Key.ToString(), Advert.AdvertName);
#endif
	return false;
}

template <typename TOwner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject,TOwner>
bool FResolver::RegisterValue(const FValueType& ValueType)
{
	if (!ValueType.IsValid()) return false;
	if (IsRegistered(ValueType)) return true;
	if (auto Object = FindObject<TOwner>(ValueType.TargetKey))
	{
		const auto* Advert = static_cast<const TValueAdvert<TOwner, TValue>*>(ValueType.ValueAdvert);
		auto Aggregator = MakeUnique<TValueAggregator<TOwner, TValue, Settings>>(*Object, *Advert, ValueType.TargetKey.ToString(), ResolverName);
		Aggregators.Add(ValueType, TUniquePtr<IValueAggregator>(Aggregator.Release()));
		return true;
	}

#if !UE_BUILD_SHIPPING
	const auto ClassName = TOwner::StaticClass()->GetName();
	UE_LOG(LogVRS, Error, TEXT("Failed to find a target object of class %s for key %s, advert %s"), *ClassName, *ValueType.TargetKey.ToString(), ValueType.ValueAdvert->AdvertName);
#endif
	return false;
}

template <typename TOwner, typename TInner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
bool FResolver::RegisterValue(const TInnerValueAdvert<TOwner, TInner, TValue>& Advert, const FTargetKey& Key)
{
	const FValueType ValueType(&Advert, Key);
	if (IsRegistered(ValueType)) return true;
	if (auto Object = FindObject<TOwner>(Key))
	{
		auto Aggregator = MakeUnique<TInnerValueAggregator<TOwner, TInner, TValue, Settings>>(*Object, Advert, ValueType.TargetKey.ToString(), ResolverName);
		Aggregators.Add(ValueType, TUniquePtr<IValueAggregator>(Aggregator.Release()));
		return true;
	}

#if !UE_BUILD_SHIPPING
	const auto ClassName = TOwner::StaticClass()->GetName();
	UE_LOG(LogVRS, Error, TEXT("Failed to find a target object of class %s for key %s, advert %s"), *ClassName, *Key.ToString(), Advert.AdvertName);
#endif
	return false;
}

template <typename TOwner, typename TInner, typename TValue, FValueSettings Settings> requires std::is_base_of_v<UObject, TOwner>
bool FResolver::RegisterValue(const FValueType& ValueType)
{
	if (!ValueType.IsValid()) return false;

	if (IsRegistered(ValueType)) return true;
	if (auto Object = FindObject<TOwner>(ValueType.TargetKey))
	{
		const auto* Advert = static_cast<const TInnerValueAdvert<TOwner, TInner, TValue>*>(ValueType.ValueAdvert);
		auto Aggregator = MakeUnique<TInnerValueAggregator<TOwner, TInner, TValue, Settings>>(*Object, *Advert, ValueType.TargetKey.ToString(), ResolverName);
		Aggregators.Add(ValueType, TUniquePtr<IValueAggregator>(Aggregator.Release()));
		return true;
	}

#if !UE_BUILD_SHIPPING
	const auto ClassName = TOwner::StaticClass()->GetName();
	UE_LOG(LogVRS, Error, TEXT("Failed to find a target object of class %s for key %s, advert %s"), *ClassName, *ValueType.TargetKey.ToString(), ValueType.ValueAdvert->AdvertName);
#endif
	return false;
}

template <typename TValue>
TSharedPtr<TInternalValueKey<TValue>> FResolver::MakeInternalKey(const FValueType& ValueType)
{
	if (!IsRegistered(ValueType)) return nullptr;
#if !UE_BUILD_SHIPPING
	if (ValueType.ValueAdvert->ValueType != &typeid(TValue))
	{
		UE_LOG(LogVRS, Error, TEXT("MakeKey type mismatch for '%s'"), ValueType.ValueAdvert->AdvertName);
		return nullptr;
	}
#endif
	if (auto* Existing = ValueKeys.Find(ValueType))
		return StaticCastSharedPtr<TInternalValueKey<TValue>>(*Existing);

	if (auto* Object = FindObject(ValueType.TargetKey))
	{
#if !UE_BUILD_SHIPPING
		// The advert casts the object to its owner type unchecked — guard against
		// an advert / target key pair that names mismatched classes.
		if (!Object->IsA(ValueType.ValueAdvert->GetObjectClass()))
		{
			UE_LOG(LogVRS, Error, TEXT("MakeKey object class mismatch for '%s'"), ValueType.ValueAdvert->AdvertName);
			return nullptr;
		}
#endif
		TSharedPtr<TInternalValueKey<TValue>> NewKey = MakeShareable(new TInternalValueKey<TValue>(this, Object, ValueType));
		ValueKeys.Add(ValueType, NewKey);
		return NewKey;
	}
	return nullptr;
}

template<typename TValue, typename TKey> requires std::is_base_of_v<FValueKeyBase<TValue>, TKey>
TSharedPtr<TKey> FResolver::MakeKey(const FValueType& ValueType, const uint8 Priority, const FChangeContext& Context)
{
	if (auto Key = MakeInternalKey<TValue>(ValueType))
		return MakeShared<TKey>(Key, Priority, Context);
	return nullptr;
}

template <typename TValue>
const TValue* FResolver::GetValue(const FValueType& ValueType)
{
	if (!ValueType.IsValid()) return nullptr;
#if !UE_BUILD_SHIPPING
	if (ValueType.ValueAdvert->ValueType != &typeid(TValue))
	{
		UE_LOG(LogVRS, Error, TEXT("Type mismatch for '%s'"), ValueType.ValueAdvert->AdvertName);
		return nullptr;
	}
#endif
		
	if (auto* Object = FindObject(ValueType.TargetKey))
	{
#if !UE_BUILD_SHIPPING
		if (!Object->IsA(ValueType.ValueAdvert->GetObjectClass()))
		{
			UE_LOG(LogVRS, Error, TEXT("GetValue object class mismatch for '%s'"), ValueType.ValueAdvert->AdvertName);
			return nullptr;
		}
#endif
		return static_cast<const TValue*>(ValueType.ValueAdvert->GetValueVoid(Object));
	}
	return nullptr;
}

template <typename TValue>
const TValue* FResolver::GetDefaultValue(const FValueType& ValueType)
{
	if (!ValueType.IsValid()) return nullptr;
#if !UE_BUILD_SHIPPING
	if (ValueType.ValueAdvert->ValueType != &typeid(TValue))
	{
		UE_LOG(LogVRS, Error, TEXT("Type mismatch for '%s'"), ValueType.ValueAdvert->AdvertName);
		return nullptr;
	}
#endif
	if (const auto Class = ValueType.ValueAdvert->GetObjectClass())
	{
		if (auto CDO = Class->GetDefaultObject<UObject>())
			return static_cast<TValue*>(ValueType.ValueAdvert->GetValueVoid(CDO));
	}
	return nullptr;
}