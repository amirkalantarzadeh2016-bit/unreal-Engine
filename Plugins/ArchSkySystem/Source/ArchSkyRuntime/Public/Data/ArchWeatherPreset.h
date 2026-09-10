// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#if WITH_EDITOR
// Brings in EDataValidationResult and FDataValidationContext. Editor-only, so the
// runtime module still compiles clean in a packaged build.
#include "Misc/DataValidation.h"
#endif

#include "ArchWeatherPreset.generated.h"

class USoundBase;
class UTexture2D;

/** Kind of precipitation a preset produces. Not a lerpable quantity. */
UENUM(BlueprintType)
enum class EArchPrecipType : uint8
{
	None	UMETA(DisplayName = "None"),
	Rain	UMETA(DisplayName = "Rain"),
	Snow	UMETA(DisplayName = "Snow"),
	Hail	UMETA(DisplayName = "Hail"),
	/** Airborne dust. The defining condition of a Yazd or Ahvaz summer. */
	Dust	UMETA(DisplayName = "Dust")
};

/**
 * The continuously-valued part of a weather preset: everything that can be linearly
 * interpolated between two presets during a transition.
 *
 * ARCH NOTE: this is a plain USTRUCT rather than the data asset itself so that the runtime
 * blend result is a cheap value type. The Director holds one of these, the subsystem
 * produces it, and no per-frame code ever touches a UObject to read weather.
 */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchWeatherParams
{
	GENERATED_BODY()

	// --- Clouds -----------------------------------------------------------------------

	/** Fraction of sky covered. 0 = clear, 1 = solid overcast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CloudCoverage = 0.1f;

	/** Optical density of the cloud medium. Higher reads as darker, heavier cloud. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CloudDensity = 0.1f;

	/** Height of the cloud layer base above the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds",
		meta = (ClampMin = "0.1", ClampMax = "20.0", UIMin = "0.5", UIMax = "12.0", Units = "Kilometers"))
	float CloudAltitudeKm = 5.f;

	/** Vertical extent of the cloud layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds",
		meta = (ClampMin = "0.1", ClampMax = "20.0", UIMin = "0.5", UIMax = "15.0", Units = "Kilometers"))
	float CloudLayerThicknessKm = 3.f;

	/** Detail-noise strength that eats into the cloud silhouette. 0 = smooth blobs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CloudErosion = 0.4f;

	/** Speed the cloud layer drifts at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds",
		meta = (ClampMin = "0.0", ClampMax = "200.0", UIMin = "0.0", UIMax = "60.0", Units = "MetersPerSecond"))
	float CloudWindSpeed = 5.f;

	/**
	 * Compass bearing the cloud layer drifts TOWARDS, in the same convention as the sun
	 * (0 = true north, clockwise). Corrected by the Director's north offset before use.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds",
		meta = (ClampMin = "0.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0", Units = "Degrees"))
	float CloudWindDirectionDeg = 270.f;

	// --- Atmosphere -------------------------------------------------------------------

	/** Multiplier on Rayleigh (molecular) scattering. 1 = Earth standard. Drives sky blueness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere",
		meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "3.0"))
	float RayleighScatteringScale = 1.f;

	/** Multiplier on Mie (aerosol) scattering. Raises the haze around the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere",
		meta = (ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "10.0"))
	float MieScatteringScale = 1.f;

	/** Forward-scattering bias of the Mie phase function. 0 = isotropic, 0.9 = strongly forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere",
		meta = (ClampMin = "0.0", ClampMax = "0.99", UIMin = "0.0", UIMax = "0.95"))
	float MieAnisotropy = 0.8f;

	/**
	 * Linke turbidity. 2 = pristine mountain air, 4-6 = typical urban, 10+ = dust storm.
	 * This is the single parameter that makes an Iranian summer sky read correctly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere",
		meta = (ClampMin = "1.0", ClampMax = "20.0", UIMin = "2.0", UIMax = "12.0"))
	float AerosolTurbidity = 3.f;

	/** Multiplicative tint applied to sky luminance. White = neutral. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	FLinearColor SkyLuminanceTint = FLinearColor::White;

	// --- Fog ---------------------------------------------------------------------------

	/** Exponential height-fog density at the fog actor's height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog",
		meta = (ClampMin = "0.0", ClampMax = "0.2", UIMin = "0.0", UIMax = "0.05"))
	float FogDensity = 0.02f;

	/** How fast fog thins with altitude. Larger = tighter to the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog",
		meta = (ClampMin = "0.001", ClampMax = "2.0", UIMin = "0.05", UIMax = "1.0"))
	float FogHeightFalloff = 0.2f;

	/** Colour of light scattered into the view by the fog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog")
	FLinearColor FogInscatteringColor = FLinearColor(0.447f, 0.638f, 1.f, 1.f);

	/** Distance from the camera at which fog begins. Keeps interiors readable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog",
		meta = (ClampMin = "0.0", ClampMax = "100000.0", UIMin = "0.0", UIMax = "20000.0", Units = "Centimeters"))
	float FogStartDistance = 0.f;

	/** Scales volumetric-fog extinction, i.e. how much the fog eats light shafts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog",
		meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "4.0"))
	float VolumetricFogExtinctionScale = 1.f;

	// --- Light response ----------------------------------------------------------------

	/** Multiplier on the physically derived sun illuminance. Overcast should be well below 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light Response",
		meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.5"))
	float SunIntensityMultiplier = 1.f;

	/** How far towards the weather tint the sun's colour-temperature colour is pushed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light Response",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SunLightColorTintStrength = 0.f;

	/** Multiplier on sky-light intensity. Overcast skies are brighter than clear ones here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light Response",
		meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "3.0"))
	float SkyLightIntensityMultiplier = 1.f;

	/**
	 * Scales the sun's angular diameter, and therefore its shadow penumbra.
	 * 1 = the true 0.545 degree disc. Overcast presets raise this to soften shadows the
	 * way a diffusing cloud deck does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light Response",
		meta = (ClampMin = "1.0", ClampMax = "40.0", UIMin = "1.0", UIMax = "20.0"))
	float ShadowSoftnessMultiplier = 1.f;

	/**
	 * Share of total illuminance arriving as diffuse sky rather than direct sun.
	 * 0.15 on a clear day, ~1.0 under solid overcast. The Director uses this to trade
	 * sun intensity against sky-light intensity so total scene illuminance stays sane.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light Response",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float DiffuseToDirectRatio = 0.15f;

	// --- Precipitation ------------------------------------------------------------------

	/** Rate of precipitation, 0 = none. Written to the MPC for particle and material use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PrecipIntensity = 0.f;

	/** How wet horizontal surfaces look. Lags precipitation in the subsystem, deliberately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PuddleWetness = 0.f;

	/** How much lying snow covers up-facing surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SurfaceSnowCoverage = 0.f;

	/** Ground-level wind speed driving foliage and cloth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation",
		meta = (ClampMin = "0.0", ClampMax = "60.0", UIMin = "0.0", UIMax = "35.0", Units = "MetersPerSecond"))
	float WindStrength = 2.f;

	/** Gustiness. 0 = steady flow, 1 = violently gusty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float WindTurbulence = 0.2f;

	// --- Audio -------------------------------------------------------------------------

	/** Linear gain applied to the ambient loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio",
		meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.0"))
	float AmbientVolume = 0.5f;

	/** Expected thunder strikes per minute. 0 disables thunder entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio",
		meta = (ClampMin = "0.0", ClampMax = "60.0", UIMin = "0.0", UIMax = "20.0"))
	float ThunderFrequencyPerMinute = 0.f;

	// --- Post process --------------------------------------------------------------------

	/** Exposure bias in stops. Overcast presets usually want a small negative value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process",
		meta = (ClampMin = "-5.0", ClampMax = "5.0", UIMin = "-3.0", UIMax = "3.0", Units = "Stops"))
	float ExposureCompensation = 0.f;

	/** Multiplier on bloom intensity. Hazy and dusty presets raise it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process",
		meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "2.0"))
	float BloomMultiplier = 1.f;

	// --- Discrete fields: chosen, never interpolated -------------------------------------

	/** Kind of precipitation. Picked from whichever side of the blend dominates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Precipitation")
	EArchPrecipType PrecipType = EArchPrecipType::None;

	/** Ambient loop for this weather. Soft pointer: never hard-loads the whole preset library. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TSoftObjectPtr<USoundBase> AmbientLoop;

	/** One-shot thunder cue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TSoftObjectPtr<USoundBase> ThunderCue;

	/**
	 * Linearly interpolates every continuous field and hard-selects every discrete one.
	 *
	 * ARCH NOTE: enums and object pointers are picked by (Alpha < 0.5), NOT interpolated.
	 * Lerping an enum's underlying integer would step through unrelated values - blending
	 * Rain (1) to Snow (2) would pass through nothing meaningful, and blending Rain to
	 * Dust (4) would pass through Snow and Hail. The visible cost is that the precipitation
	 * type snaps at the midpoint of a transition; that is masked by PrecipIntensity, which
	 * DOES interpolate and is near its minimum around the midpoint of a sensible transition.
	 *
	 * @param A      Source parameters (returned when Alpha is 0).
	 * @param B      Destination parameters (returned when Alpha is 1).
	 * @param Alpha  Blend position, clamped to [0, 1].
	 */
	static FArchWeatherParams Blend(const FArchWeatherParams& A, const FArchWeatherParams& B, float Alpha);

	/** Equality within a small tolerance, used to skip redundant Director work. */
	bool IsNearlyEqual(const FArchWeatherParams& Other, float Tolerance = 1.e-3f) const;

	/**
	 * Largest absolute difference across the fields that force an expensive re-apply
	 * (atmosphere, fog, sky-light). Used by the Director's dirty-flag throttling.
	 */
	float GetSignificantDelta(const FArchWeatherParams& Other) const;
};

/**
 * A named, authorable weather condition.
 *
 * UPrimaryDataAsset rather than a plain UDataAsset so the Asset Manager can discover the
 * whole library by primary type without a content scan, and so presets can be cooked into
 * a chunk and streamed. See UArchSkySettings::WeatherPresetScanPaths.
 */
UCLASS(BlueprintType, meta = (DisplayName = "ArchSky Weather Preset"))
class ARCHSKYRUNTIME_API UArchWeatherPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UArchWeatherPreset();

	/** Stable identifier used by Blueprint, the console and save games. Must be unique. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName PresetId = NAME_None;

	/** Localised name shown on the weather tile in the UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/** Optional tile thumbnail. Soft so an unopened weather panel costs nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	/** Sort order within the weather panel. Lower values come first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity",
		meta = (ClampMin = "0", ClampMax = "1000"))
	int32 SortPriority = 100;

	/** The parameters this preset applies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ShowOnlyInnerProperties))
	FArchWeatherParams Params;

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

#if WITH_EDITOR
	//~ Begin UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	//~ End UObject
#endif

	/** The primary asset type all weather presets register under. */
	static const FPrimaryAssetType PrimaryAssetType;
};

/** Blueprint access to weather blending. */
UCLASS(meta = (ScriptName = "ArchWeather"))
class ARCHSKYRUNTIME_API UArchWeatherLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Interpolates two weather parameter sets. See FArchWeatherParams::Blend. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather", meta = (DisplayName = "Blend Weather Params"))
	static FArchWeatherParams BlendWeatherParams(const FArchWeatherParams& A, const FArchWeatherParams& B, float Alpha);

	/** Localised name for a precipitation type. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather", meta = (DisplayName = "Get Precipitation Type Name"))
	static FText GetPrecipTypeDisplayName(EArchPrecipType PrecipType);

	/**
	 * The built-in parameter set for one of the twelve shipped preset names.
	 * Used as the fallback when no weather preset assets are present in the project, so a
	 * freshly installed plugin still has a full weather library.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather", meta = (DisplayName = "Get Built In Weather Params"))
	static FArchWeatherParams GetBuiltInWeatherParams(FName PresetId, bool& bOutFound);

	/** Names of every built-in preset, in display order. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Weather", meta = (DisplayName = "Get Built In Weather Preset Ids"))
	static TArray<FName> GetBuiltInWeatherPresetIds();
};
