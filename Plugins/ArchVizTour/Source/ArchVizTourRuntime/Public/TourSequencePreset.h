// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TourTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "TourSequencePreset.generated.h"

class ULevelSequence;
class UTourPathPreset;

/**
 * A playable tour: the ordered step list plus everything needed to resolve it.
 *
 * This is what the user presses Play on. UTourPathPreset only supplies geometry.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tour Sequence Preset"))
class ARCHVIZTOURRUNTIME_API UTourSequencePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UTourSequencePreset();

	/**
	 * Schema version this build writes.
	 *
	 * History:
	 *   1 - initial release.
	 *   2 - BlendTime / BlendFunction / BlendExp added; steps written before this had an
	 *       implicit hard cut, which is preserved by migration rather than silently changed.
	 */
	static constexpr int32 CurrentSchemaVersion = 2;

	/** Ordered steps. Next / Previous in the playback UI move between these. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour")
	TArray<FTourStep> Steps;

	/**
	 * Path presets this tour needs.
	 *
	 * Soft references keep an unplayed tour out of memory: UTourSubsystem::LoadTour streams
	 * them in asynchronously and only then reports the preset as loaded.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour")
	TArray<TSoftObjectPtr<UTourPathPreset>> ReferencedPaths;

	/** Restart from step 0 instead of finishing when the last step completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour")
	bool bLoopTour = false;

	/** Multiplies every step duration. 1 is authored speed; 2 runs the tour twice as fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0"))
	float GlobalTimeScale = 1.0f;

	/** Title shown in the playback UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour")
	FText TourTitle;

	/**
	 * Sequence produced by the editor's bake action.
	 *
	 * Required by ETourPlaybackBackend::Sequencer; ignored by the procedural backend. Soft so a
	 * packaged client that never uses the Sequencer backend does not pay for the reference.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour|Sequencer")
	TSoftObjectPtr<ULevelSequence> BakedSequence;

	/** Backend used when this tour is played. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour|Sequencer")
	ETourPlaybackBackend PlaybackBackend = ETourPlaybackBackend::Procedural;

	/** Stable identity, assigned once on creation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour|Identity")
	FGuid PresetId;

	/** Schema version this asset was last saved with. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour|Identity")
	int32 SchemaVersion = CurrentSchemaVersion;

	// --- UObject ----------------------------------------------------------
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

	// --- UPrimaryDataAsset ------------------------------------------------
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** Upgrade a step list from an older schema, in place. Pure, so it is directly testable. */
	static bool MigrateSteps(TArray<FTourStep>& InOutSteps, int32 FromVersion);

	/**
	 * Total authored length of the tour.
	 * @return Seconds, after GlobalTimeScale. Steps with a non-positive duration contribute 0.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	float GetTotalDuration() const;

	/** True when the tour has at least one step. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	bool IsPlayable() const { return Steps.Num() > 0; }

	/** @see UTourPathPreset::ExportToJson */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Serialization")
	bool ExportToJson(const FString& AbsolutePath) const;

	/** @see UTourPathPreset::ImportFromJson */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Serialization")
	bool ImportFromJson(const FString& AbsolutePath);

	/** Serialise to a JSON string without touching the filesystem. Used by the tests. */
	bool ToJsonString(FString& OutJson) const;

	/** Deserialise from a JSON string. Applies schema migration. */
	bool FromJsonString(const FString& InJson);
};
