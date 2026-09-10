// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/ArchSolarTypes.h"
#include "ArchSolarMath.generated.h"

/**
 * Pure implementation of the NOAA Solar Position Algorithm.
 *
 * THREAD SAFETY: every function in this namespace is a pure function of its arguments.
 * There is no global state, no UObject access and no engine subsystem access, so all of
 * it is safe to call from any thread, including inside a ParallelFor. This is deliberate:
 * the editor visualiser samples ~200 sun positions to draw an analemma and we do not want
 * that on the game thread.
 *
 * UNITS: every angle in this namespace is DEGREES unless the name ends in Radians.
 * Time arguments are UTC FDateTime unless the name says Local. "Hours" always means
 * local decimal hours (13.5 == 13:30).
 *
 * ACCURACY: NOAA quotes +/- 0.01 deg for years 1901-2099 and roughly +/- 0.1 deg outside
 * that window. That is one to two orders of magnitude better than the sun's own angular
 * diameter, so shadow studies produced from it are defensible in front of a client.
 */
namespace ArchSolarMath
{
	/** Julian Day of the J2000.0 epoch (2000-01-01 12:00 TT). */
	inline constexpr double JulianDayJ2000 = 2451545.0;

	/** Days in a Julian century. */
	inline constexpr double DaysPerJulianCentury = 36525.0;

	/**
	 * Zenith angle at which the sun's upper limb touches a sea-level horizon.
	 * 90 deg + 0.833 deg, where 0.833 = 0.267 (semi-diameter) + 0.566 (mean refraction).
	 */
	inline constexpr double SunriseZenithDegrees = 90.833;

	/** Zenith angle defining civil twilight (sun 6 deg below the horizon). */
	inline constexpr double CivilTwilightZenithDegrees = 96.0;

	/** Zenith angle defining nautical twilight (12 deg below). */
	inline constexpr double NauticalTwilightZenithDegrees = 102.0;

	/** Zenith angle defining astronomical twilight (18 deg below). */
	inline constexpr double AstronomicalTwilightZenithDegrees = 108.0;

	/** Mean length of the synodic (new moon to new moon) month, in days. */
	inline constexpr double SynodicMonthDays = 29.530588853;

	// ---------------------------------------------------------------------------------
	// Step 1 - time
	// ---------------------------------------------------------------------------------

	/**
	 * Converts a UTC instant to a Julian Day number (including fractional day).
	 * Uses the Gregorian branch of the Fliegel algorithm; valid for all dates after
	 * 1582-10-15 which comfortably brackets any architectural project.
	 *
	 * @param UTC  Instant in Coordinated Universal Time. NOT local time.
	 * @return     Julian Day, e.g. 2451545.0 for 2000-01-01T12:00:00Z.
	 */
	ARCHSKYRUNTIME_API double ToJulianDay(const FDateTime& UTC);

	/** Julian centuries elapsed since J2000.0. The independent variable of every series below. */
	ARCHSKYRUNTIME_API double ToJulianCentury(double JulianDay);

	// ---------------------------------------------------------------------------------
	// Step 2 - the sun's orbit. Each of these is individually testable on purpose.
	// ---------------------------------------------------------------------------------

	/** Geometric mean longitude of the sun, wrapped to [0, 360). Degrees. */
	ARCHSKYRUNTIME_API double GeomMeanLongitudeSun(double JulianCentury);

	/** Geometric mean anomaly of the sun. NOT wrapped - the series below want it unwrapped. Degrees. */
	ARCHSKYRUNTIME_API double GeomMeanAnomalySun(double JulianCentury);

	/** Eccentricity of Earth's orbit. Dimensionless, ~0.0167. */
	ARCHSKYRUNTIME_API double EccentricityEarthOrbit(double JulianCentury);

	/** Equation of centre: true anomaly minus mean anomaly. Degrees. */
	ARCHSKYRUNTIME_API double SunEquationOfCenter(double JulianCentury, double MeanAnomalyDegrees);

	/** Mean longitude plus equation of centre. Degrees. */
	ARCHSKYRUNTIME_API double SunTrueLongitude(double MeanLongitudeDegrees, double EquationOfCenterDegrees);

	/** True longitude corrected for nutation and aberration. Degrees. */
	ARCHSKYRUNTIME_API double SunApparentLongitude(double JulianCentury, double TrueLongitudeDegrees);

	/** Mean obliquity of the ecliptic (Earth's axial tilt), ~23.44 deg. Degrees. */
	ARCHSKYRUNTIME_API double MeanObliquityOfEcliptic(double JulianCentury);

	/** Mean obliquity corrected for nutation. Degrees. */
	ARCHSKYRUNTIME_API double ObliquityCorrection(double JulianCentury, double MeanObliquityDegrees);

	/** Solar declination - the sun's latitude on the celestial sphere. Degrees, range [-23.44, +23.44]. */
	ARCHSKYRUNTIME_API double SunDeclination(double ObliquityCorrectedDegrees, double ApparentLongitudeDegrees);

	/**
	 * Equation of time: apparent solar time minus mean solar time.
	 * Minutes, roughly -14.2 (Feb) .. +16.4 (Nov). This is the term that makes the
	 * analemma a figure eight rather than a straight line.
	 */
	ARCHSKYRUNTIME_API double EquationOfTime(double JulianCentury);

	/**
	 * Hour angle (degrees from the meridian) at which the sun reaches a given zenith angle.
	 *
	 * @param LatitudeDegrees   Observer latitude.
	 * @param SolarDeclDegrees  Solar declination for the instant of interest.
	 * @param ZenithDegrees     Target zenith, e.g. SunriseZenithDegrees.
	 * @param bOutIsPolarDay    Set when the sun never reaches the zenith going down (always up).
	 * @param bOutIsPolarNight  Set when the sun never reaches the zenith going up (always down).
	 * @return                  Positive hour angle in degrees, or 0 in a polar case.
	 */
	ARCHSKYRUNTIME_API double HourAngleAtZenith(double LatitudeDegrees, double SolarDeclDegrees,
		double ZenithDegrees, bool& bOutIsPolarDay, bool& bOutIsPolarNight);

	/** Convenience overload for the standard sunrise zenith. Degrees. */
	ARCHSKYRUNTIME_API double HourAngleSunrise(double LatitudeDegrees, double SolarDeclDegrees);

	/**
	 * NOAA's piecewise atmospheric-refraction approximation.
	 *
	 * @param TrueAltitudeDeg  Geometric (unrefracted) altitude in degrees.
	 * @return                 Degrees to ADD to the geometric altitude to get the apparent one.
	 *                         ~0.567 deg at the horizon, ~0 above 85 deg.
	 */
	ARCHSKYRUNTIME_API double ApproxAtmosphericRefraction(double TrueAltitudeDeg);

	/**
	 * Extra horizon dip from observer elevation, in degrees.
	 * dip = acos(R / (R + h)). 1200 m (Tehran) gives ~1.11 deg, which moves sunrise
	 * about four minutes earlier - visible in a readout, so we model it.
	 */
	ARCHSKYRUNTIME_API double HorizonDipDegrees(double ElevationMeters);

	// ---------------------------------------------------------------------------------
	// Step 3 - the two functions callers actually use.
	// ---------------------------------------------------------------------------------

	/**
	 * Full solar position for one instant.
	 *
	 * @param Location   Observer position and timezone.
	 * @param LocalTime  Wall-clock local time at that location (NOT UTC). The timezone
	 *                   offset in Location is what converts it.
	 */
	ARCHSKYRUNTIME_API FArchSolarPosition CalculateSolarPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime);

	/**
	 * Sunrise / sunset / twilight / solar noon for one local calendar day.
	 * The time-of-day component of LocalDate is ignored.
	 *
	 * ARCH NOTE: we run one refinement iteration - solar noon is solved first, then
	 * declination and equation-of-time are re-evaluated AT the first-guess sunrise and
	 * sunset instants rather than at noon. This costs two extra series evaluations and
	 * buys about a minute of accuracy at high latitudes where declination moves fastest.
	 * The alternative (single pass at noon) is what most game sky plugins do and is
	 * visibly wrong in Tromso in April.
	 */
	ARCHSKYRUNTIME_API FArchSolarDayInfo CalculateSolarDayInfo(const FArchGeoLocation& Location, const FDateTime& LocalDate);

	// ---------------------------------------------------------------------------------
	// Step 4 - the engine boundary. Read the derivation in the .cpp before touching this.
	// ---------------------------------------------------------------------------------

	/**
	 * Converts an astronomical azimuth/altitude pair into the world rotation of an
	 * Unreal directional light.
	 *
	 * Astronomical convention: Azimuth 0 = true North, increasing clockwise (E = 90).
	 * Scene convention:        +X = true North, +Y = East, +Z = Up, so Unreal yaw and
	 *                          compass azimuth are the same number.
	 *
	 * @param AzimuthDeg           Sun/moon compass bearing in degrees.
	 * @param AltitudeDeg          Sun/moon altitude above the horizon in degrees.
	 * @param NorthOffsetDegrees   Scene-space yaw at which TRUE north lies. 0 means the
	 *                             floor plan is already north-aligned to +X.
	 * @return                     Rotation whose FORWARD vector is the direction the light
	 *                             travels, i.e. from the body towards the scene.
	 */
	ARCHSKYRUNTIME_API FRotator SolarToUnrealLightRotation(double AzimuthDeg, double AltitudeDeg, float NorthOffsetDegrees);

	/**
	 * Unit vector pointing FROM the scene TOWARDS the celestial body, in scene space.
	 * This is the negation of the light's forward vector, and is what a material wants
	 * for a dot(N, L) term or a sky-sphere sun disc.
	 */
	ARCHSKYRUNTIME_API FVector SolarToUnrealDirectionToBody(double AzimuthDeg, double AltitudeDeg, float NorthOffsetDegrees);

	/**
	 * Ratio of a shadow's length to the height of the object casting it: cot(altitude).
	 * Clamped so a sun on the horizon does not return infinity.
	 *
	 * @param AltitudeDegrees  Geometric solar altitude.
	 * @param MaxMultiplier    Value returned once the sun is at or below the horizon.
	 */
	ARCHSKYRUNTIME_API double ShadowLengthMultiplier(double AltitudeDegrees, double MaxMultiplier = 100.0);

	/** Classifies an altitude (and its direction of travel) into a photographic phase. */
	ARCHSKYRUNTIME_API EArchTimePhase ClassifyTimePhase(double AltitudeDegrees, bool bIsRising);
}

/**
 * Blueprint-facing mirror of the ArchSolarMath namespace.
 * Everything here is BlueprintPure and side-effect free; the C++ namespace is the
 * canonical implementation and this class only forwards to it.
 */
UCLASS(meta = (ScriptName = "ArchSolarMath"))
class ARCHSKYRUNTIME_API UArchSolarMathLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Full solar position for a local wall-clock time at a location. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Solar Math", meta = (DisplayName = "Calculate Solar Position"))
	static FArchSolarPosition CalculateSolarPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime);

	/** Sunrise / sunset / twilight / solar noon for a local calendar day. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Solar Math", meta = (DisplayName = "Calculate Solar Day Info"))
	static FArchSolarDayInfo CalculateSolarDayInfo(const FArchGeoLocation& Location, const FDateTime& LocalDate);

	/** Directional-light rotation for a compass azimuth/altitude, corrected for plan north. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Solar Math", meta = (DisplayName = "Solar To Unreal Light Rotation"))
	static FRotator SolarToUnrealLightRotation(float AzimuthDegrees, float AltitudeDegrees, float NorthOffsetDegrees);

	/** Unit vector from the scene towards the sun/moon, corrected for plan north. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Solar Math", meta = (DisplayName = "Direction To Celestial Body"))
	static FVector SolarToUnrealDirectionToBody(float AzimuthDegrees, float AltitudeDegrees, float NorthOffsetDegrees);

	/** cot(altitude): how many times an object's height its shadow is. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Solar Math", meta = (DisplayName = "Shadow Length Multiplier"))
	static float ShadowLengthMultiplier(float AltitudeDegrees, float MaxMultiplier = 100.f);

	/** Equation of time in minutes for a UTC instant. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Solar Math", meta = (DisplayName = "Equation Of Time (Minutes)"))
	static double EquationOfTimeMinutes(const FDateTime& UTC);

	/**
	 * Julian Day for a UTC instant.
	 * Returned as double, not float: a Julian Day is ~2.46e6 and a float would quantise
	 * it to about six hours, which would silently destroy every downstream series.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Solar Math", meta = (DisplayName = "To Julian Day"))
	static double ToJulianDay(const FDateTime& UTC);
};
