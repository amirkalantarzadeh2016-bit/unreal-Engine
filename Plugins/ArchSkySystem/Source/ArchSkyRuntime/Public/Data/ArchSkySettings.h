// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DeveloperSettings.h"
#include "Math/ArchSolarTypes.h"

#include "ArchSkySettings.generated.h"

class UArchLocationPreset;
class UArchWeatherPreset;
class UCurveFloat;
class UMaterialParameterCollection;

/** How clouds are rendered. See MATERIALS.md for the two paths. */
UENUM(BlueprintType)
enum class EArchCloudMode : uint8
{
	/** UVolumetricCloudComponent. Highest fidelity, most expensive. Desktop default. */
	Volumetric		UMETA(DisplayName = "Volumetric Clouds (Path A)"),
	/** Panoramic sky-sphere material driven entirely through the MPC. VR and low-end. */
	SkySphere		UMETA(DisplayName = "Sky Sphere Material (Path B)"),
	/** No cloud rendering at all; the atmosphere still responds to coverage. */
	Disabled		UMETA(DisplayName = "Disabled")
};

/**
 * Project-wide defaults for the ArchSky System.
 * Project Settings -> Plugins -> ArchSky.
 *
 * config = Game + defaultconfig means edits are written to DefaultGame.ini and travel
 * with the project rather than living in a per-user file.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "ArchSky"))
class ARCHSKYRUNTIME_API UArchSkySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UArchSkySettings();

	/** Convenience accessor. Never returns nullptr after module startup. */
	static const UArchSkySettings* Get();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings

	// --- Defaults -----------------------------------------------------------------------

	/** City selected when a level starts with no saved state. Must exist in the library. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	FName DefaultLocationId = TEXT("Tehran");

	/** Used when DefaultLocationId is not found. Guarantees a valid sky on any project. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ShowOnlyInnerProperties))
	FArchGeoLocation FallbackLocation;

	/** Weather preset selected on level start. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	FName DefaultWeatherPresetId = TEXT("Clear");

	/** Calendar the date readout uses until the user toggles it. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	EArchCalendarType DefaultCalendarType = EArchCalendarType::Gregorian;

	/** Time of day, in local decimal hours, that a level starts at. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults",
		meta = (ClampMin = "0.0", ClampMax = "24.0", UIMin = "0.0", UIMax = "24.0", Units = "Hours"))
	float DefaultTimeOfDayHours = 10.f;

	/** Day of year a level starts at. 172 is the June solstice in a non-leap year. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults",
		meta = (ClampMin = "1", ClampMax = "366", UIMin = "1", UIMax = "366"))
	int32 DefaultDayOfYear = 172;

	/** Calendar year a level starts at. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults",
		meta = (ClampMin = "1900", ClampMax = "2200"))
	int32 DefaultYear = 2026;

	/** Clock display default: true for 24-hour, false for AM/PM. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	bool bDefaultTo24HourClock = true;

	// --- Rendering ----------------------------------------------------------------------

	/** Which of the two cloud paths the Director drives. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering")
	EArchCloudMode CloudMode = EArchCloudMode::Volumetric;

	/**
	 * Angular diameter of the sun's disc. The true value is 0.545 degrees and produces
	 * the correct shadow penumbra; raising it is an artistic softening, not a physical one.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (ClampMin = "0.01", ClampMax = "20.0", UIMin = "0.1", UIMax = "5.0", Units = "Degrees"))
	float SunLightSourceAngleDegrees = 0.545f;

	/** Angular diameter of the moon's disc. Coincidentally almost identical to the sun's. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (ClampMin = "0.01", ClampMax = "20.0", UIMin = "0.1", UIMax = "5.0", Units = "Degrees"))
	float MoonLightSourceAngleDegrees = 0.545f;

	/** Peak illuminance of a full moon at the zenith. The physical value is ~0.25 lux. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (ClampMin = "0.0", ClampMax = "50.0", UIMin = "0.0", UIMax = "5.0", Units = "Lux"))
	float MoonPeakIlluminanceLux = 0.4f;

	/**
	 * Sun altitude at which the moon light starts fading out, and the altitude at which it
	 * is fully off. Defaults span civil twilight so neither light ever pops.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (ClampMin = "-20.0", ClampMax = "20.0", UIMin = "-12.0", UIMax = "6.0", Units = "Degrees"))
	float MoonFadeOutStartSunAltitude = -6.f;

	/** Sun altitude above which the moon light is disabled entirely. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (ClampMin = "-20.0", ClampMax = "20.0", UIMin = "-12.0", UIMax = "6.0", Units = "Degrees"))
	float MoonFadeOutEndSunAltitude = 0.f;

	/** Optional curve asset mapping sun altitude (degrees) to illuminance (lux). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (AllowedClasses = "/Script/Engine.CurveFloat"))
	TSoftObjectPtr<UCurveFloat> DefaultSunIntensityCurve;

	/** Optional curve asset mapping sun altitude (degrees) to colour temperature (Kelvin). */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (AllowedClasses = "/Script/Engine.CurveFloat"))
	TSoftObjectPtr<UCurveFloat> DefaultSunColorTemperatureCurve;

	/** The parameter collection the Director writes every frame. See MATERIALS.md. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Rendering",
		meta = (AllowedClasses = "/Script/Engine.MaterialParameterCollection"))
	TSoftObjectPtr<UMaterialParameterCollection> SkyParameterCollection;

	// --- Performance ---------------------------------------------------------------------

	/**
	 * Sun altitude change, in degrees, that forces a sky-light recapture.
	 * Lower means smoother indirect lighting and more GPU cost. See PERFORMANCE.md.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "0.05", ClampMax = "45.0", UIMin = "0.25", UIMax = "10.0", Units = "Degrees"))
	float SkyRecaptureAltitudeThreshold = 1.f;

	/** Normalised weather-parameter change that forces a sky-light recapture. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.02", UIMax = "0.5"))
	float SkyRecaptureWeatherThreshold = 0.05f;

	/** Hard floor on how often RecaptureSky() may run, in frames. Never exceeded. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "1", ClampMax = "600", UIMin = "1", UIMax = "120"))
	int32 MinFramesBetweenSkyRecaptures = 30;

	/** Seconds between cloud-material parameter updates. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "1.0", Units = "Seconds"))
	float CloudUpdateInterval = 0.1f;

	/** Seconds between full solar/lunar recomputations while time is flowing. 0 = every frame. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance",
		meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "0.5", Units = "Seconds"))
	float SolarUpdateInterval = 0.f;

	/** When false the Director never touches the material parameter collection. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bWriteMaterialParameterCollection = true;

	/** When false the moon light component is never created, saving one shadowed light. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bEnableMoonLight = true;

	// --- Networking -----------------------------------------------------------------------

	/**
	 * Master switch for the replication path. When false the Director does not replicate
	 * and every code path behaves exactly as it does in single player.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Networking")
	bool bEnableReplication = false;

	/**
	 * When false, a client calling a setter gets a warning and nothing else - the server
	 * owns the presentation. Turn it on only for collaborative review sessions.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Networking")
	bool bAllowClientTimeControl = false;

	/** How often the Director pushes replicated sky state, in Hz. Clients interpolate between. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Networking",
		meta = (ClampMin = "0.5", ClampMax = "30.0", UIMin = "1.0", UIMax = "10.0", Units = "Hz"))
	float ReplicationUpdateFrequencyHz = 2.f;

	// --- Content ---------------------------------------------------------------------------

	/** Directories scanned by the Asset Manager for weather presets and location libraries. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Content", meta = (RelativeToGameContentDir, LongPackageName))
	TArray<FDirectoryPath> PresetScanPaths;

	/** Optional curated city library. When unset the compiled-in list is used. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Content",
		meta = (AllowedClasses = "/Script/ArchSkyRuntime.ArchLocationPreset"))
	TSoftObjectPtr<UArchLocationPreset> LocationLibrary;

	// --- Debug ---------------------------------------------------------------------------

	/** Draw the on-screen state HUD from level start. Equivalent to ArchSky.Debug.ShowState 1. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bShowStateHudByDefault = false;

	/** Warn when more than one AArchSkyDirector exists in a level. */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bWarnOnDuplicateDirectors = true;
};
