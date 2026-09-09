// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TourTypes.h"
#include "UObject/SoftObjectPath.h"

#include "TourPathPreset.generated.h"

/**
 * Reusable camera path geometry.
 *
 * Deliberately holds nothing but geometry: a preset is the shape of a move, while
 * UTourSequencePreset is the tour that uses it. Keeping them apart is what lets one authored
 * spline serve several steps at different sub-ranges and speeds.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tour Path Preset"))
class ARCHVIZTOURRUNTIME_API UTourPathPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UTourPathPreset();

	/**
	 * Schema version this build of the plugin writes.
	 *
	 * History:
	 *   1 - initial release.
	 *   2 - DefaultSpeed reinterpreted from cm/frame to cm/s; per-point Speed added.
	 *   3 - explicit rotation opt-in added (FTourPoint::bUseExplicitRotation).
	 */
	static constexpr int32 CurrentSchemaVersion = 3;

	/** The path geometry itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path")
	FTourPathData PathData;

	/** Name shown in pickers and the preset manager. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path")
	FText DisplayName;

	/** Optional authored thumbnail. When unset the editor draws a top-down sketch of the path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Path", meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	FSoftObjectPath ThumbnailPath;

	/**
	 * Stable identity that survives renames, duplication into another project, and JSON
	 * round-trips. Assigned once on creation and never rewritten.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour Path|Identity")
	FGuid PresetId;

	/** Schema version this asset was last saved with. Upgraded in place by PostLoad. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour Path|Identity")
	int32 SchemaVersion = CurrentSchemaVersion;

	// --- UObject ----------------------------------------------------------
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

	// --- UPrimaryDataAsset ------------------------------------------------
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/**
	 * Upgrade a path from an older schema to CurrentSchemaVersion, in place.
	 *
	 * Split out of PostLoad so the migration is a pure function of its arguments and can be
	 * unit-tested without cooking or loading an asset.
	 *
	 * @param InOutPathData  Data to upgrade.
	 * @param FromVersion    Version the data was written with.
	 * @return true when anything was changed.
	 */
	static bool MigratePathData(FTourPathData& InOutPathData, int32 FromVersion);

	/**
	 * Serialise this preset to a UTF-8 JSON file.
	 * @param AbsolutePath  Destination file path. Parent directories are created as needed.
	 * @return true on success; failures are logged to LogArchVizTour.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Serialization")
	bool ExportToJson(const FString& AbsolutePath) const;

	/**
	 * Replace this preset's contents from a JSON file written by ExportToJson.
	 * Applies schema migration, so an older export loads correctly.
	 * @param AbsolutePath  Source file path.
	 * @return true on success; failures are logged and leave the asset untouched.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Serialization")
	bool ImportFromJson(const FString& AbsolutePath);

	/** Serialise to a JSON string without touching the filesystem. Used by the tests. */
	bool ToJsonString(FString& OutJson) const;

	/** Deserialise from a JSON string. Applies schema migration. */
	bool FromJsonString(const FString& InJson);
};
