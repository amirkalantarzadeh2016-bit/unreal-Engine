// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/WeakObjectPtr.h"

class UArchOpeningComponent;
class UWorld;

/**
 * Drives editor preview for every opening that is currently previewing.
 *
 * Two things this deliberately does NOT do:
 *
 *  - It does not rely on the gameplay world tick. Preview is advanced from the core ticker, so it
 *    works in a plain editor world with no PIE session running.
 *  - It does not transact. Preview only writes transient component transforms, so scrubbing a door
 *    for a minute does not fill the undo buffer with a transaction per frame, and does not dirty
 *    the map. Persistent configuration changes are transacted by the tools that make them.
 *
 * The manager also owns the safety rules around preview: it restores every previewing opening when
 * PIE starts, when a map is saved, when a map changes, and on shutdown, so a preview pose can never
 * be mistaken for authored data or accidentally saved into a level.
 */
class FArchOpeningPreviewManager : public TSharedFromThis<FArchOpeningPreviewManager>
{
public:
	void Initialize();
	void Shutdown();

	/** Adds an opening to the preview set. Safe to call repeatedly. */
	void Track(UArchOpeningComponent* Opening);

	/** Restores the opening's pre-preview pose and stops tracking it. */
	void RestoreAndStopTracking(UArchOpeningComponent* Opening);

	/** Stops preview animation but leaves the pose alone (the artist may still want to scrub). */
	void StopPreviewing(UArchOpeningComponent* Opening);

	/** Restores every tracked opening. Used before PIE, before save, and on shutdown. */
	void RestoreAll();

	bool IsTracking(const UArchOpeningComponent* Opening) const;

private:
	bool Tick(float DeltaSeconds);

	void HandlePreBeginPIE(bool bIsSimulating);
	void HandlePreSaveWorld(class UWorld* World, class FObjectPreSaveContext Context);

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle PreBeginPIEHandle;
	FDelegateHandle PreSaveWorldHandle;
	FDelegateHandle MapChangeHandle;

	TArray<TWeakObjectPtr<UArchOpeningComponent>> Tracked;
};
