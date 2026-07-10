// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Compile-time storage configuration for a registered value.
 *
 * Passed as a non-type template parameter to RegisterValue / FValueBatch::Add and
 * forwarded to the aggregator, which sizes its per-layer fixed storage from it:
 *
 * @code
 * Resolver->RegisterValue<AMyCharacter, float, FValueSettings{10, 5}>(MaxHealthAdvert);
 * @endcode
 *
 * Both bounds are hard limits — a layer that already holds MaxChanges queued changes
 * rejects new ones, and change contexts targeting a layer >= MaxLayers are invalid.
 */
struct FValueSettings
{
	/** Maximum number of simultaneously queued changes per layer (compile-time). */
	uint8 MaxChanges;

	/** Maximum number of priority layers (compile-time). */
	uint8 MaxLayers;
};
