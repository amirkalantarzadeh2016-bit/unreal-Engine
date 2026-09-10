// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Math/ArchSolarTypes.h"

#include "ArchSkyState.generated.h"

/**
 * The complete, authoritative description of the sky at one moment.
 *
 * This is the single source of truth. The subsystem owns exactly one of these; the
 * Director never stores a second copy, and the UI never stores one at all. Anything that
 * can be DERIVED from these fields - solar position, season, time phase, blended weather -
 * is computed on demand rather than stored, so the state can never be internally
 * inconsistent.
 */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchSkyState
{
	GENERATED_BODY()

	/** Observer position, timezone and elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State", meta = (ShowOnlyInnerProperties))
	FArchGeoLocation Location;

	/**
	 * Scene-space yaw, in degrees, at which TRUE north lies.
	 * 0 means the floor plan is modelled with north along +X. If the plan is rotated so
	 * that true north points 30 degrees clockwise of scene +X, set this to 30.
	 * Shadow studies are meaningless without it, because architectural plans are almost
	 * never modelled north-aligned.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State",
		meta = (ClampMin = "-360.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0", Units = "Degrees"))
	float NorthOffsetDegrees = 0.f;

	/** Gregorian calendar year. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State",
		meta = (ClampMin = "1900", ClampMax = "2200", UIMin = "2000", UIMax = "2100"))
	int32 Year = 2026;

	/** 1-based day of the year, 1 .. 365 or 366. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State",
		meta = (ClampMin = "1", ClampMax = "366", UIMin = "1", UIMax = "366"))
	int32 DayOfYear = 172;

	/** Local wall-clock time in decimal hours. 13.5 == 13:30. Always inside [0, 24). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State",
		meta = (ClampMin = "0.0", ClampMax = "24.0", UIMin = "0.0", UIMax = "24.0", Units = "Hours"))
	float TimeOfDayHours = 12.f;

	/**
	 * Simulated hours that pass per real-world second. 0 pauses time.
	 * 1.0 makes a full day take 24 real seconds; 0.0167 runs at real time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State",
		meta = (ClampMin = "-600.0", ClampMax = "600.0", UIMin = "0.0", UIMax = "60.0"))
	float TimeFlowRate = 0.f;

	/** Weather preset currently being blended FROM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State")
	FName WeatherPresetA = TEXT("Clear");

	/** Weather preset currently being blended TO. NAME_None when no transition is running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State")
	FName WeatherPresetB = NAME_None;

	/** Blend position between A and B. 0 = fully A, 1 = fully B. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float WeatherBlendAlpha = 0.f;

	/** When true, passing 24:00 rolls the date forward instead of looping the same day. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|State")
	bool bAutoAdvanceDate = true;

	/** Builds the local FDateTime this state describes. */
	FDateTime ToLocalDateTime() const;

	/** Clamps and wraps every field into its legal range. Called after every mutation. */
	void Sanitise();

	/** Field-by-field comparison with float tolerance, used to suppress redundant events. */
	bool IsNearlyEqual(const FArchSkyState& Other, float Tolerance = 1.e-4f) const;
};

/**
 * The compact, quantised form of the state that goes over the wire.
 *
 * ARCH NOTE: we replicate this rather than FArchSkyState because the full struct is ~60
 * bytes of doubles that never change during a presentation (the location, the north
 * offset), while the four fields that DO change every frame quantise into eight bytes.
 * Clients interpolate time locally between updates, so 2 Hz is enough; replicating the
 * full state every frame would burn bandwidth to transmit numbers the client can predict.
 */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchSkyReplicatedState
{
	GENERATED_BODY()

	/** Time of day quantised to 1/1000 of an hour (3.6 s), well below one frame of drift. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	uint16 QuantisedTimeOfDay = 0;

	/** 1-based day of the year. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	uint16 DayOfYear = 172;

	/** Gregorian year. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	uint16 Year = 2026;

	/** Simulated hours per real second, quantised to 1/100. Lets clients predict forward. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	int16 QuantisedTimeFlowRate = 0;

	/** Weather blend alpha quantised to 1/255. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	uint8 QuantisedWeatherAlpha = 0;

	/** Weather preset A. FName replication is name-table based, so this is cheap. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	FName WeatherPresetA = TEXT("Clear");

	/** Weather preset B. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	FName WeatherPresetB = NAME_None;

	/** Bumped by the server on every discontinuous change so clients snap instead of lerp. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Networking")
	uint8 DiscontinuityCounter = 0;

	/** Largest hour value the quantisation can carry. */
	static constexpr float TimeQuantisationScale = 1000.f;

	/** Scale applied to the flow rate before quantising. */
	static constexpr float FlowRateQuantisationScale = 100.f;

	/** Packs a full state down to the wire form. Location and north offset are NOT sent. */
	static FArchSkyReplicatedState FromSkyState(const FArchSkyState& State, uint8 InDiscontinuityCounter);

	/** Writes the replicated fields back over a local state, leaving the rest untouched. */
	void ApplyToSkyState(FArchSkyState& OutState) const;

	/** Dequantised time of day in local decimal hours. */
	float GetTimeOfDayHours() const;

	/** Dequantised simulated hours per real second. */
	float GetTimeFlowRate() const;

	bool operator==(const FArchSkyReplicatedState& Other) const;
	bool operator!=(const FArchSkyReplicatedState& Other) const { return !(*this == Other); }
};

/**
 * A saved, named sky configuration - "Client Review: Winter Morning".
 *
 * Edit-time presets are these assets. Runtime presets, created by the architect during a
 * presentation, are JSON inside a USaveGame instead; see UArchSkySaveGame. The two share
 * this struct so a runtime preset can be promoted to an asset by hand.
 */
UCLASS(BlueprintType, meta = (DisplayName = "ArchSky State Preset"))
class ARCHSKYRUNTIME_API UArchSkyStatePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Localised name shown in the preset list. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/** Free-text note, e.g. "worst-case overshadowing of the north courtyard". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = "true"))
	FText Description;

	/** The state this preset applies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State", meta = (ShowOnlyInnerProperties))
	FArchSkyState State;

	/** When false, applying the preset leaves the current location alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	bool bApplyLocation = true;

	/** When false, applying the preset leaves the current weather alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	bool bApplyWeather = true;

	/** When false, applying the preset leaves the plan's north offset alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State")
	bool bApplyNorthOffset = false;

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

	/** The primary asset type all state presets register under. */
	static const FPrimaryAssetType PrimaryAssetType;
};
