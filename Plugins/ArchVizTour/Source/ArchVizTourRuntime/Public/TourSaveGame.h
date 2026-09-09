// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TourTypes.h"

#include "TourSaveGame.generated.h"

/**
 * Runtime-authored tour, stored in a save slot.
 *
 * Presets are cooked content and cannot be created in a packaged build, so an in-game
 * authoring session persists here instead. Holds only plain data - no UObject references -
 * so the payload survives being written by one build and read by another.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tour Save Game"))
class ARCHVIZTOURRUNTIME_API UTourSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Schema version of the saved payload; migrated on load by UTourPersistenceLibrary. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	int32 SchemaVersion = 0;

	/** Title shown in the slot list. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	FText TourTitle;

	/**
	 * Path geometry, indexed in the same order as PathNames.
	 * Stored by value: a packaged build has no way to create a UTourPathPreset asset.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	TArray<FTourPathData> Paths;

	/** Name each entry of Paths is published under, so FTourStep::SplinePathRef can resolve. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	TArray<FName> PathNames;

	/** The tour's ordered steps. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	TArray<FTourStep> Steps;

	/** Restart from step 0 when the last step completes. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	bool bLoopTour = false;

	/** Multiplies every step duration. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	float GlobalTimeScale = 1.0f;

	/** UTC timestamp of the last write, for sorting the slot list. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	FDateTime SavedAtUtc = FDateTime(0);

	/** Engine version string that wrote the slot, purely for diagnostics. */
	UPROPERTY(BlueprintReadWrite, Category = "Tour Save")
	FString SavedByEngineVersion;
};
