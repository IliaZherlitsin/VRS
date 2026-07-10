// Copyright (c) 2026 Ilia Zherlitsin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChangeEntry.h"

class FJsonObject;

#if !UE_BUILD_SHIPPING

/**
 * Global debug recorder for every change flushed by any FResolver.
 *
 * Aggregators forward their flushed changes to the singleton via RecordChange().
 * Entries are buffered in memory and periodically serialized to a JSON Lines file
 * under <ProjectSaved>/VRS/. One log file per session timestamp; a new file is
 * rolled over once MaxRecordAmount records have been written.
 *
 * The class is fully stripped in UE_BUILD_SHIPPING — only an empty stub remains
 * so call sites can keep referring to FResolverDebug::Get().RecordChange() without
 * #ifdefs.
 *
 * Console commands:
 *   - VRS.Debug.Enable           — start the periodic flush ticker
 *   - VRS.Debug.Disable          — stop the periodic flush ticker
 *   - VRS.Debug.Record           — flush the current buffer to disk immediately
 *   - VRS.Debug.SetRecordRate <s> — change the flush interval (in seconds)
 */
class VRS_API FResolverDebug
{
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVRSDebugFlushIntegrationTest;
#endif

	/** Default flush interval in seconds, used when the singleton is first created. */
	static constexpr float RecordRate = 5;

	/** Hard cap on records written to a single file before rolling over to a new one. */
	static constexpr int32 MaxRecordAmount = 10000;

	/** Serialises an entry to a JSON object suitable for one JSON Lines record. */
	static TSharedPtr<FJsonObject> CreateJSONObject(const FChangeEntry& Entry);

	/** Generates a fresh timestamped FilePath under <ProjectSaved>/VRS/. */
	void UpdateFilePath();

	/** Absolute path of the current JSON Lines file. */
	FString FilePath;

	/** Total records written to the current file — drives rollover. */
	int32 RecordCount = 0;

	/** In-memory queue of records waiting to be serialized on the next flush. */
	TArray<FChangeEntry> ChangeBuffer;

#if WITH_EDITOR
	/** Handle for the EndPIE delegate — flushes on every Play-In-Editor stop. */
	FDelegateHandle EndPIEHandle;
#endif

	/** Handle for the OnPreExit delegate — flushes once before engine shutdown. */
	FDelegateHandle PreExitHandle;

	/** Handle for the periodic flush ticker. Reset when Disable() is called. */
	FTSTicker::FDelegateHandle RecordTickerHandle;
public:
	FResolverDebug();
	~FResolverDebug();

	/** @return  Reference to the singleton instance. Created on first use. */
	FORCEINLINE static FResolverDebug& Get()
	{
		static FResolverDebug Instance;
		return Instance;
	}

	/**
	 * Buffers a change entry for the next flush.
	 *
	 * The entry is not written to disk immediately — only the periodic ticker
	 * (or RecordChanges()) actually performs I/O.
	 *
	 * @param  Entry  Snapshot of a flushed change. Must satisfy Entry.IsValid().
	 * @return        True if the entry was queued. False if it failed validation.
	 */
	FORCEINLINE bool RecordChange(const FChangeEntry& Entry)
	{
		if (!Entry.IsValid()) return false;
		ChangeBuffer.Add(Entry);
		RecordCount++;
		return true;
	}

	/**
	 * Starts the periodic flush ticker. Called automatically by the constructor.
	 *
	 * Idempotent — calling Enable() while a ticker is already registered is a no-op.
	 */
	FORCEINLINE void Enable()
	{
		if (RecordTickerHandle.IsValid()) return;
		RecordTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FResolverDebug::RecordChanges), RecordRate);
	}

	/**
	 * Replaces the current flush interval. Disables the previous ticker first.
	 *
	 * @param  NewRate  New interval in seconds between automatic flushes.
	 */
	FORCEINLINE void SetRecordRate(const float NewRate)
	{
		Disable();
		RecordTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FResolverDebug::RecordChanges), NewRate);
	}

	/** Stops the periodic flush ticker. Buffered records remain in memory until flushed manually. */
	FORCEINLINE void Disable()
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RecordTickerHandle);
		RecordTickerHandle.Reset();
	}

	/**
	 * Serialises every buffered record to disk as JSON Lines and clears the buffer.
	 *
	 * Writes are appended to the current file. When RecordCount exceeds MaxRecordAmount,
	 * a new timestamped file is created via UpdateFilePath() so individual files stay
	 * a manageable size.
	 *
	 * Bound to the periodic ticker, OnPreExit and (in editor) EndPIE.
	 *
	 * @param  Unused  Ticker delta time. Unused — present for FTickerDelegate compatibility.
	 * @return         Always true (kept around so the ticker is never auto-removed).
	 */
	bool RecordChanges(float Unused = 0.f);
};

#else

/**
 * Shipping-build stub. All public entry points compile to no-ops so call sites
 * remain unchanged between development and shipping configurations.
 */
class FResolverDebug
{
public:
	/** @return  Reference to the singleton stub. */
	FORCEINLINE static FResolverDebug& Get()
	{
		static FResolverDebug Instance;
		return Instance;
	}

	/** No-op in shipping. Always returns true. */
	FORCEINLINE bool RecordChange(const FChangeEntry& Entry)
	{
		return true;
	}
};

#endif