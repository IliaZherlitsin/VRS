// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

template <typename T>
bool TInternalValueKey<T>::AddChange(TUniquePtr<FChangeBase> Change, const FChangeContext& ChangeContext, FChangeKey* OutKey) const
{
	if (!IsValid()) return false;
	return Resolver->AddChange(ValueType, MoveTemp(Change), ChangeContext, OutKey);
}

template <typename T>
bool TInternalValueKey<T>::AddChange(const T& NewValue, const FChangeSettings& Settings, const FChangeContext& ChangeContext, FChangeKey* OutKey) const
{
	if (!IsValid()) return false;
	return Resolver->AddChange<T>(ValueType, NewValue, Settings, ChangeContext, OutKey);
}

template <typename T>
bool TInternalValueKey<T>::RemoveChange(const FChangeKey& Key) const
{
	if (!IsValid()) return false;
	return Resolver->RemoveChange(Key);
}

template <typename T>
const T* TInternalValueKey<T>::GetValue() const
{
	// IsValid also covers a dead target — the advert would otherwise be handed
	// a null object pointer.
	if (!IsValid()) return nullptr;
	return static_cast<const T*>(ValueType.ValueAdvert->GetValueVoid(Target.Get()));
}

template <typename T>
const T* TInternalValueKey<T>::GetDefaultValue() const
{
	if (!ValueType.IsValid()) return nullptr;

	const auto* ValueAdvert = ValueType.ValueAdvert;
	if (const auto Class = ValueAdvert->GetObjectClass())
	{
		if (const auto CDO = Class->GetDefaultObject<UObject>())
			return static_cast<const T*>(ValueAdvert->GetValueVoid(CDO));
	}
	return nullptr;
}

template <typename T>
bool TInternalValueKey<T>::Flush(const bool bWithForce) const
{
	if (!IsValid()) return false;
	return Resolver->Flush(ValueType, bWithForce);
}

template <typename T>
bool TInternalValueKey<T>::HasChanges() const
{
	if (!IsValid()) return false;
	return Resolver->HasChanges(ValueType);
}
