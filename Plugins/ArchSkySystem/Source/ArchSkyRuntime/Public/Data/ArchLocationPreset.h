// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/ArchSolarTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include "ArchLocationPreset.generated.h"

/**
 * Broad climate family, used only to pick a sensible default weather preset when the user
 * switches city. It is a hint, not a simulation.
 */
UENUM(BlueprintType)
enum class EArchClimateHint : uint8
{
	/** Hot, dry, very high summer turbidity. Yazd, Dubai. */
	AridDesert			UMETA(DisplayName = "Arid / Desert"),
	/** Dry summers, wet mild winters. Shiraz, Istanbul. */
	Mediterranean		UMETA(DisplayName = "Mediterranean"),
	/** Cold winters, warm summers, dry. Tehran, Tabriz. */
	SemiAridContinental	UMETA(DisplayName = "Semi-Arid Continental"),
	/** High humidity and frequent cloud. Rasht, Singapore. */
	HumidSubtropical	UMETA(DisplayName = "Humid Subtropical"),
	/** Cool, cloudy, frequent rain. London. */
	TemperateOceanic	UMETA(DisplayName = "Temperate Oceanic"),
	/** Long cold winters with lying snow. */
	Continental			UMETA(DisplayName = "Continental"),
	/** Hot and wet year round. */
	Tropical			UMETA(DisplayName = "Tropical")
};

/** One city entry: a geographic position plus everything the UI needs to present it. */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchLocationEntry
{
	GENERATED_BODY()

	/** Stable identifier, e.g. "Tehran". Used by the console and by saved presets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName CityId = NAME_None;

	/** Localised city name shown in the dropdown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	/** Localised country name, shown as the dropdown's secondary line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText CountryName;

	/** Latitude, longitude, timezone and elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geography", meta = (ShowOnlyInnerProperties))
	FArchGeoLocation Location;

	/** Broad climate family; drives the default weather suggestion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geography")
	EArchClimateHint ClimateHint = EArchClimateHint::SemiAridContinental;

	/** Weather preset selected when the user switches to this city without naming one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geography")
	FName DefaultWeatherPresetId = TEXT("Clear");

	FArchLocationEntry() = default;
};

/**
 * A curated library of cities.
 *
 * ARCH NOTE: this is a UPrimaryDataAsset holding an array, not a UDataTable.
 *
 *   - A UDataTable row struct cannot hold an FText that the localisation gatherer will
 *     pick up without a per-row string-table dance; a UPROPERTY FText inside a
 *     USTRUCT inside a UPrimaryDataAsset is gathered automatically. City and country
 *     names must be translatable for the Persian UI, so that decides it.
 *   - The Asset Manager can discover this by primary type and async-load it, which a
 *     DataTable also supports, so we lose nothing there.
 *   - The cost is that adding a city means opening an asset rather than pasting a CSV
 *     row. For a list of this size (tens, not thousands) that is the right trade.
 *
 * A compiled-in fallback library ships alongside, so the plugin has a full city list with
 * no content at all; any asset entry with the same CityId overrides its built-in twin.
 */
UCLASS(BlueprintType, meta = (DisplayName = "ArchSky Location Preset Library"))
class ARCHSKYRUNTIME_API UArchLocationPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The cities in this library. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locations", meta = (TitleProperty = "CityId"))
	TArray<FArchLocationEntry> Locations;

	/** Finds a city by id. Returns nullptr when absent - callers must handle it. */
	const FArchLocationEntry* FindLocation(FName CityId) const;

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** The primary asset type all location libraries register under. */
	static const FPrimaryAssetType PrimaryAssetType;
};

/** Blueprint access to the built-in city library. */
UCLASS(meta = (ScriptName = "ArchLocations"))
class ARCHSKYRUNTIME_API UArchLocationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Looks a city up in the compiled-in library.
	 *
	 * @param CityId     Identifier, e.g. "Tehran". Case-sensitive FName comparison.
	 * @param bOutFound  False when the id is unknown; the returned entry is then a default.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Locations", meta = (DisplayName = "Get Built In Location"))
	static FArchLocationEntry GetBuiltInLocation(FName CityId, bool& bOutFound);

	/** Every built-in city, in the order the dropdown should show them. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Locations", meta = (DisplayName = "Get Built In Locations"))
	static TArray<FArchLocationEntry> GetBuiltInLocations();

	/** Localised climate-hint name. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Locations", meta = (DisplayName = "Get Climate Hint Name"))
	static FText GetClimateHintDisplayName(EArchClimateHint ClimateHint);

	/**
	 * Substring search over city and country names, for the searchable dropdown.
	 * Matching is case- and accent-insensitive and runs against the LOCALISED text, so a
	 * Persian user searching in Persian finds Persian names.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Locations", meta = (DisplayName = "Search Built In Locations"))
	static TArray<FArchLocationEntry> SearchBuiltInLocations(const FString& SearchText);
};
