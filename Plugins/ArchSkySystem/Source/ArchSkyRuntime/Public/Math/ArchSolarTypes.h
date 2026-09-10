// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchSolarTypes.generated.h"

/**
 * Which named seasonal instant to jump to.
 * Dates are hemisphere-neutral names as architects use them in the northern
 * hemisphere; GetSeason() below is the hemisphere-aware version.
 */
UENUM(BlueprintType)
enum class EArchSolsticePreset : uint8
{
	/** ~21 March. Sun crosses the celestial equator northbound. */
	SpringEquinox		UMETA(DisplayName = "Spring Equinox (Mar 21)"),
	/** ~21 June. Longest day in the northern hemisphere. */
	SummerSolstice		UMETA(DisplayName = "Summer Solstice (Jun 21)"),
	/** ~23 September. Sun crosses the celestial equator southbound. */
	AutumnEquinox		UMETA(DisplayName = "Autumn Equinox (Sep 23)"),
	/** ~21 December. Shortest day in the northern hemisphere - the critical shadow-study date. */
	WinterSolstice		UMETA(DisplayName = "Winter Solstice (Dec 21)")
};

/** Astronomical season, derived from day-of-year and the hemisphere of the observer. */
UENUM(BlueprintType)
enum class EArchSeason : uint8
{
	Spring	UMETA(DisplayName = "Spring"),
	Summer	UMETA(DisplayName = "Summer"),
	Autumn	UMETA(DisplayName = "Autumn"),
	Winter	UMETA(DisplayName = "Winter")
};

/**
 * Photographic / architectural phase of the day, ordered from darkest to brightest
 * on the morning side. Derived purely from the sun's altitude, so it is correct at
 * every latitude including polar day and polar night.
 */
UENUM(BlueprintType)
enum class EArchTimePhase : uint8
{
	/** Sun below -18 deg. True darkness. */
	Night					UMETA(DisplayName = "Night"),
	/** -18 deg .. -12 deg. */
	AstronomicalTwilight	UMETA(DisplayName = "Astronomical Twilight"),
	/** -12 deg .. -6 deg. */
	NauticalTwilight		UMETA(DisplayName = "Nautical Twilight"),
	/** -6 deg .. -4 deg. The "blue hour" proper. */
	CivilTwilight			UMETA(DisplayName = "Civil Twilight"),
	/** -4 deg .. +0.833 deg while the sun is climbing. */
	Sunrise					UMETA(DisplayName = "Sunrise"),
	/** +0.833 deg .. +6 deg while climbing. */
	GoldenHourMorning		UMETA(DisplayName = "Golden Hour (Morning)"),
	/** Above +6 deg. */
	Day						UMETA(DisplayName = "Day"),
	/** +6 deg .. +0.833 deg while descending. */
	GoldenHourEvening		UMETA(DisplayName = "Golden Hour (Evening)"),
	/** +0.833 deg .. -4 deg while descending. */
	Sunset					UMETA(DisplayName = "Sunset")
};

/** Which calendar a date string should be formatted in. */
UENUM(BlueprintType)
enum class EArchCalendarType : uint8
{
	/** Proleptic Gregorian. */
	Gregorian	UMETA(DisplayName = "Gregorian"),
	/** Solar Hijri / Jalali, as observed in Iran and Afghanistan. */
	Jalali		UMETA(DisplayName = "Jalali (Solar Hijri)")
};

/**
 * A geographic observer position.
 *
 * ARCH NOTE: latitude/longitude are stored as double, not float. At 51.389 deg east a
 * float carries ~0.000004 deg of precision, which is fine, but the NOAA pipeline
 * multiplies longitude by 4 and adds it to a minutes-of-day accumulator; keeping the
 * whole chain in double removes any argument about where rounding crept in when an
 * architect compares our sunrise against the NOAA web calculator.
 */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchGeoLocation
{
	GENERATED_BODY()

	/** Degrees north of the equator. Positive = northern hemisphere. Range [-90, +90]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Location",
		meta = (ClampMin = "-90.0", ClampMax = "90.0", UIMin = "-90.0", UIMax = "90.0", Units = "Degrees"))
	double LatitudeDegrees = 35.6892;

	/** Degrees east of Greenwich. Positive = east. Range [-180, +180]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Location",
		meta = (ClampMin = "-180.0", ClampMax = "180.0", UIMin = "-180.0", UIMax = "180.0", Units = "Degrees"))
	double LongitudeDegrees = 51.3890;

	/** Standard (winter) UTC offset in hours. Iran = +3.5, India = +5.5, New York = -5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Location",
		meta = (ClampMin = "-12.0", ClampMax = "14.0", UIMin = "-12.0", UIMax = "14.0", Units = "Hours"))
	float TimezoneOffsetHours = 3.5f;

	/**
	 * When true, one hour is added to the standard offset for dates inside the
	 * northern-hemisphere DST window. Iran abolished DST in 2022, so this defaults off.
	 *
	 * ARCH NOTE: a full IANA tz database would be the correct answer here, but shipping
	 * (and updating) tzdata inside a plugin is a maintenance burden that buys an architect
	 * nothing - shadow studies are quoted in standard time precisely so they stay valid
	 * across DST rule changes. We offer a simple opt-in EU/US-style rule instead and say so.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Location")
	bool bObserveDST = false;

	/** Height above mean sea level. Only affects the horizon dip used for sunrise/sunset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Location",
		meta = (ClampMin = "-500.0", ClampMax = "9000.0", UIMin = "0.0", UIMax = "4000.0", Units = "Meters"))
	float ElevationMeters = 1200.f;

	FArchGeoLocation() = default;

	FArchGeoLocation(double InLatitude, double InLongitude, float InTimezoneHours, float InElevationMeters = 0.f)
		: LatitudeDegrees(InLatitude)
		, LongitudeDegrees(InLongitude)
		, TimezoneOffsetHours(InTimezoneHours)
		, ElevationMeters(InElevationMeters)
	{
	}

	/** True if the observer is south of the equator. Used to flip the seasons. */
	FORCEINLINE bool IsSouthernHemisphere() const { return LatitudeDegrees < 0.0; }

	bool operator==(const FArchGeoLocation& Other) const
	{
		return FMath::IsNearlyEqual(LatitudeDegrees, Other.LatitudeDegrees, 1.e-6)
			&& FMath::IsNearlyEqual(LongitudeDegrees, Other.LongitudeDegrees, 1.e-6)
			&& FMath::IsNearlyEqual(TimezoneOffsetHours, Other.TimezoneOffsetHours, 1.e-4f)
			&& bObserveDST == Other.bObserveDST
			&& FMath::IsNearlyEqual(ElevationMeters, Other.ElevationMeters, 0.1f);
	}

	bool operator!=(const FArchGeoLocation& Other) const { return !(*this == Other); }
};

/**
 * The sun's position for one instant, in the observer's horizontal (alt/az) frame.
 * All angles are degrees. Azimuth is measured from TRUE north, increasing clockwise
 * (north = 0, east = 90, south = 180, west = 270).
 */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchSolarPosition
{
	GENERATED_BODY()

	/** Compass bearing of the sun. 0 = true north, increasing clockwise. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	double AzimuthDegrees = 0.0;

	/** Apparent altitude above the horizon, INCLUDING atmospheric refraction. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	double AltitudeDegrees = 0.0;

	/**
	 * Geometric altitude with NO refraction applied. Degrees.
	 * ARCH NOTE: shadow geometry must use this one - refraction bends the light we see
	 * but a ray-traced shadow in the engine is cast by the geometric direction. The
	 * apparent altitude is what a person standing on site would measure with a clinometer.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	double TrueAltitudeDegrees = 0.0;

	/** 90 - AltitudeDegrees. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	double ZenithDegrees = 90.0;

	/** Solar declination for this instant, i.e. the sun's latitude on the celestial sphere. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	double DeclinationDegrees = 0.0;

	/** Apparent-minus-mean solar time. Minutes, roughly -14 .. +16 over a year. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Minutes"))
	double EquationOfTimeMinutes = 0.0;

	/** Angular distance of the sun from the local meridian. Negative = morning. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	double HourAngleDegrees = 0.0;

	/** True when the apparent altitude is above the -0.833 deg refracted horizon. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar")
	bool bIsAboveHorizon = false;

	/** AltitudeDegrees - TrueAltitudeDegrees. Degrees, always >= 0 near the horizon. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	double AtmosphericRefractionDegrees = 0.0;
};

/** Sunrise / sunset / twilight summary for one calendar day at one location. */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchSolarDayInfo
{
	GENERATED_BODY()

	/** Local decimal hours (13.5 = 13:30) of sunrise. Meaningless if bPolarDay/bPolarNight. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Hours"))
	float SunriseHours = 0.f;

	/** Local decimal hours of sunset. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Hours"))
	float SunsetHours = 0.f;

	/** Local decimal hours at which the sun crosses the meridian. Always valid. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Hours"))
	float SolarNoonHours = 0.f;

	/** SunsetHours - SunriseHours. 24 on a polar day, 0 on a polar night. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Hours"))
	float DayLengthHours = 0.f;

	/** Local decimal hours at which the sun reaches -6 deg on the morning side. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Hours"))
	float CivilTwilightStartHours = 0.f;

	/** Local decimal hours at which the sun drops past -6 deg on the evening side. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Hours"))
	float CivilTwilightEndHours = 0.f;

	/** Geometric altitude at solar noon. The number a shadow study is dimensioned from. Degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar", meta = (Units = "Degrees"))
	float MaxAltitudeDegrees = 0.f;

	/** True when the sun never sets on this date at this latitude. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar")
	bool bPolarDay = false;

	/** True when the sun never rises on this date at this latitude. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Solar")
	bool bPolarNight = false;
};
