// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ValueSettings.h"

/**
 * Describes who is applying a change and to which layer.
 *
 * Passed alongside every AddChange / AddBatch call. The resolver uses the Layer field
 * to route the change to the correct TLayerPackage. In non-shipping builds, ChangerName
 * is stored in the debug entry so every recorded change is traceable to its source.
 *
 * Construction is intentionally restricted to two explicit forms (below) to prevent
 * accidentally creating a context with an empty changer name.
 *
 * @note A default-constructed context has an empty changer name and is rejected by
 *       AddChange in non-shipping builds. Always provide a changer and a layer.
 */
struct VRS_API FChangeContext
{
	friend class FResolver;
	friend struct FChangeBatch;

	template<typename TOwner, typename TValue, FValueSettings Settings>
	friend struct TBaseValueAggregator;

	FChangeContext() = default;

#if !UE_BUILD_SHIPPING
	/**
	 * Construct from a live UObject — its name is used as the changer identifier.
	 *
	 * @param  Changer  The object applying the change (e.g. a buff component). May be null;
	 *                  a null pointer results in an empty changer name and IsValid() == false.
	 * @param  Layer    Target priority layer (0 = lowest, MaxLayers-1 = highest).
	 */
	explicit FChangeContext(const UObject* Changer, const uint8 Layer)
		: Layer(Layer)
	{
		if (!Changer) return;
		ChangerName = *Changer->GetName();
	}

	/**
	 * Construct from an explicit string identifier.
	 *
	 * Useful when the changer is not a UObject (e.g. a subsystem, data table row).
	 *
	 * @param  InChangerName  Human-readable name of the system applying the change.
	 * @param  Layer          Target priority layer.
	 */
	explicit FChangeContext(const FString& InChangerName, const uint8 Layer)
		: ChangerName(InChangerName)
		, Layer(Layer) {}
#else
	explicit FChangeContext(const UObject* Changer, const uint8 Layer)
		: Layer(Layer) {}
	
	explicit FChangeContext(const FString& InChangerName, const uint8 Layer)
		: Layer(Layer) {}
#endif

#if !UE_BUILD_SHIPPING
	/**
	 * @return  True if ChangerName is non-empty.
	 *          A default-constructed or null-changer context is invalid and will be rejected by AddChange.
	 */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return !ChangerName.IsEmpty();
	}
#else
	/** @return  Always true in shipping — changer validation is a development-only concern. */
	[[nodiscard]] FORCEINLINE bool IsValid() const
	{
		return true;
	}
#endif

private:
#if !UE_BUILD_SHIPPING
	/** Display name of the system applying the change — recorded by the debug system. */
	FString ChangerName;
#endif

	/** Index of the priority layer this change is routed to (0 = applied first). */
	uint8 Layer = 0;
};
