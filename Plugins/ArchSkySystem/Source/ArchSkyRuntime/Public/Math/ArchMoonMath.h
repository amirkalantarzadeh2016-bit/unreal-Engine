// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/ArchSolarTypes.h"
#include "ArchMoonMath.generated.h"

/** The eight conventionally named lunar phases. */
UENUM(BlueprintType)
enum class EArchMoonPhase : uint8
{
	NewMoon			UMETA(DisplayName = "New Moon"),
	WaxingCrescent	UMETA(DisplayName = "Waxing Crescent"),
	FirstQuarter	UMETA(DisplayName = "First Quarter"),
	WaxingGibbous	UMETA(DisplayName = "Waxing Gibbous"),
	FullMoon		UMETA(DisplayName = "Full Moon"),
	WaningGibbous	UMETA(DisplayName = "Waning Gibbous"),
	LastQuarter		UMETA(DisplayName = "Last Quarter"),
	WaningCrescent	UMETA(DisplayName = "Waning Crescent")
};

/**
 * The moon's position and appearance for one instant.
 * Angles are degrees; azimuth uses the same convention as the sun (0 = true north,
 * increasing clockwise).
 */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchLunarPosition
{
	GENERATED_BODY()

	/** Compass bearing of the moon. 0 = true north, increasing clockwise. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Degrees"))
	double AzimuthDegrees = 0.0;

	/** Apparent altitude above the horizon, including refraction. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Degrees"))
	double AltitudeDegrees = 0.0;

	/** Geometric altitude with no refraction. Use this one to aim the moon light. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Degrees"))
	double TrueAltitudeDegrees = 0.0;

	/**
	 * Fraction of the visible disc that is sunlit. 0 = new, 1 = full.
	 * This is the term that must drive moon light intensity - a waxing crescent puts out
	 * roughly 1/50th the illuminance of a full moon, and a constant "night light" is the
	 * single most obvious tell of a fake sky system.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double IlluminatedFraction = 0.0;

	/** Sun-Moon-Earth angle. 0 = full moon, 180 = new moon. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Degrees"))
	double PhaseAngleDegrees = 180.0;

	/** Days elapsed since the last new moon, 0 .. ~29.53. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Days"))
	double AgeDays = 0.0;

	/** The named phase bucket AgeDays falls into. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar")
	EArchMoonPhase Phase = EArchMoonPhase::NewMoon;

	/** Earth-centre to Moon-centre distance. ~356500 (perigee) .. ~406700 (apogee) km. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Kilometers"))
	double DistanceKm = 384400.0;

	/**
	 * Apparent disc diameter relative to the mean, i.e. MeanDistance / DistanceKm.
	 * ~1.07 at a "supermoon" perigee full moon, ~0.94 at apogee. Feed this straight into
	 * a sky-sphere moon disc's UV scale.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar")
	double RelativeDiscScale = 1.0;

	/**
	 * Position angle of the bright limb, measured from celestial north through east.
	 * Rotate a moon-phase UI icon by this to make the crescent point the right way.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Degrees"))
	double BrightLimbAngleDegrees = 0.0;

	/** Apparent geocentric ecliptic longitude. Degrees, [0, 360). */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Degrees"))
	double EclipticLongitudeDegrees = 0.0;

	/** Apparent geocentric ecliptic latitude. Degrees, roughly [-5.3, +5.3]. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar", meta = (Units = "Degrees"))
	double EclipticLatitudeDegrees = 0.0;

	/** True when the moon's apparent altitude is above the refracted horizon. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Lunar")
	bool bIsAboveHorizon = false;
};

/**
 * Low-precision lunar theory, following Meeus "Astronomical Algorithms" chapter 47
 * truncated to the ~20 largest longitude terms, 12 latitude terms and 16 distance terms.
 *
 * ACCURACY: about 0.2 - 0.3 degrees in position and roughly 100 km in distance. That is
 * far below the moon's own 0.5 degree disc, so it is visually exact; it is NOT good enough
 * to predict an eclipse and this plugin does not attempt to.
 *
 * ARCH NOTE: the full ELP-2000/82 truncation Meeus prints has 60 longitude terms. We
 * rejected it: it triples the transcendental count for an error improvement (0.3 deg ->
 * 0.02 deg) that cannot be seen on screen, and the moon is evaluated every frame while
 * time is flowing. If someone later needs eclipse-grade accuracy the extra terms drop
 * into the same tables without changing this API.
 *
 * THREAD SAFETY: as with ArchSolarMath, every function here is pure and callable from
 * any thread.
 */
namespace ArchMoonMath
{
	/** Mean Earth-Moon distance in km, used as the reference for RelativeDiscScale. */
	inline constexpr double MeanDistanceKm = 385000.56;

	/** Full lunar position for a local wall-clock time at a location. */
	ARCHSKYRUNTIME_API FArchLunarPosition CalculateMoonPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime);

	/**
	 * Apparent geocentric ecliptic longitude / latitude / distance of the moon.
	 * Exposed separately so tests can check the series without the alt/az conversion.
	 *
	 * @param JulianDay          Julian Day of the instant (UTC-based).
	 * @param OutLongitudeDeg    Apparent ecliptic longitude, degrees [0, 360).
	 * @param OutLatitudeDeg     Apparent ecliptic latitude, degrees.
	 * @param OutDistanceKm      Earth-centre to Moon-centre distance, km.
	 */
	ARCHSKYRUNTIME_API void CalculateGeocentricEcliptic(double JulianDay,
		double& OutLongitudeDeg, double& OutLatitudeDeg, double& OutDistanceKm);

	/**
	 * Greenwich Mean Sidereal Time in degrees, [0, 360).
	 * Needed to turn a right ascension into a local hour angle.
	 */
	ARCHSKYRUNTIME_API double GreenwichMeanSiderealTimeDegrees(double JulianDay);

	/** Maps an age in days since new moon to the conventional eight-phase bucket. */
	ARCHSKYRUNTIME_API EArchMoonPhase ClassifyMoonPhase(double AgeDays);

	/** Human-readable phase name. Localised via the ArchSky namespace. */
	ARCHSKYRUNTIME_API FText GetMoonPhaseDisplayName(EArchMoonPhase Phase);
}

/** Blueprint mirror of ArchMoonMath. */
UCLASS(meta = (ScriptName = "ArchMoonMath"))
class ARCHSKYRUNTIME_API UArchMoonMathLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Full lunar position for a local wall-clock time at a location. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Lunar Math", meta = (DisplayName = "Calculate Moon Position"))
	static FArchLunarPosition CalculateMoonPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime);

	/** Localised display name for a moon phase, e.g. "Waxing Gibbous". */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Lunar Math", meta = (DisplayName = "Get Moon Phase Display Name"))
	static FText GetMoonPhaseDisplayName(EArchMoonPhase Phase);
};
