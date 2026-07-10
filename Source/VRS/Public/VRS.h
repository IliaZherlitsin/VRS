// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

VRS_API DECLARE_LOG_CATEGORY_EXTERN(LogVRS, Log, All);

#if !UE_BUILD_SHIPPING
	#define VRS_NON_SHIPPING(X) X
#else
	#define VRS_NON_SHIPPING(X)
#endif