// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Math/ArchSolarTypes.h"
#include "ArchTimeCalendar.generated.h"

/**
 * Gregorian calendar and clock helpers shared by the subsystem, the UI and the console.
 *
 * ARCH NOTE: the sky state stores a YEAR and a DAY-OF-YEAR rather than a month and a day.
 * Day-of-year is what the solar math actually wants, it is a single scrubbable axis for a
 * UI slider, and it makes "advance to tomorrow" a increment rather than a calendar walk.
 * Month/day is a presentation concern and is converted at the boundary, here.
 *
 * THREAD SAFETY: pure functions, no state, callable from any thread. The FText-returning
 * formatters touch the localisation manager and should be treated as game-thread only.
 */
namespace ArchTimeCalendar
{
	/** Proleptic Gregorian leap-year rule. */
	ARCHSKYRUNTIME_API bool IsLeapYear(int32 Year);

	/** 366 in a leap year, otherwise 365. */
	ARCHSKYRUNTIME_API int32 DaysInYear(int32 Year);

	/** Converts a 1-based day of the year into a month and day. Clamps out-of-range input. */
	ARCHSKYRUNTIME_API void DayOfYearToMonthDay(int32 Year, int32 DayOfYear, int32& OutMonth, int32& OutDay);

	/** Converts a month and day into a 1-based day of the year. Clamps out-of-range input. */
	ARCHSKYRUNTIME_API int32 MonthDayToDayOfYear(int32 Year, int32 Month, int32 Day);

	/** Builds an FDateTime from a year, a 1-based day of year and local decimal hours. */
	ARCHSKYRUNTIME_API FDateTime MakeDateTime(int32 Year, int32 DayOfYear, float TimeOfDayHours);

	/** Wraps decimal hours into [0, 24). */
	ARCHSKYRUNTIME_API float WrapHours(float Hours);

	/**
	 * Day of year of a named seasonal instant.
	 *
	 * ARCH NOTE: we use fixed nominal dates (Mar 20 / Jun 21 / Sep 22 / Dec 21, shifted by
	 * the leap day) rather than solving for the true instant of the equinox. The true
	 * solstice wanders about +/- 18 hours across the leap cycle, which moves the noon
	 * altitude by under 0.01 degrees - invisible - while a fixed date is what the client
	 * presentation slide says and what the architect expects the button to select.
	 */
	ARCHSKYRUNTIME_API int32 GetSolsticeDayOfYear(int32 Year, EArchSolsticePreset Preset);

	/** Season for a day of year, flipped for southern-hemisphere observers. */
	ARCHSKYRUNTIME_API EArchSeason GetSeason(int32 Year, int32 DayOfYear, bool bSouthernHemisphere);

	/**
	 * Continuous 0..1 blend position through the year, phase-locked to the seasons, for
	 * driving foliage or snow-line material parameters. 0.0 = midwinter, 0.5 = midsummer.
	 */
	ARCHSKYRUNTIME_API float GetSeasonBlend01(int32 Year, int32 DayOfYear, bool bSouthernHemisphere);

	/** Localised month name, 1-based. */
	ARCHSKYRUNTIME_API FText GetGregorianMonthName(int32 Month);

	/** Localised season name. */
	ARCHSKYRUNTIME_API FText GetSeasonDisplayName(EArchSeason Season);

	/** Localised time-phase name, e.g. "Golden Hour (Morning)". */
	ARCHSKYRUNTIME_API FText GetTimePhaseDisplayName(EArchTimePhase Phase);

	/** Formats decimal hours as "14:37" or "2:37 PM". */
	ARCHSKYRUNTIME_API FText FormatTimeOfDay(float Hours, bool b24Hour);

	/** Formats a duration in decimal hours as "11h 32m". */
	ARCHSKYRUNTIME_API FText FormatDuration(float Hours);
}

/** Blueprint mirror of ArchTimeCalendar. */
UCLASS(meta = (ScriptName = "ArchTimeCalendar"))
class ARCHSKYRUNTIME_API UArchTimeCalendarLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Proleptic Gregorian leap-year rule. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Is Leap Year"))
	static bool IsLeapYear(int32 Year);

	/** Converts a 1-based day of the year into a month and day. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Day Of Year To Month Day"))
	static void DayOfYearToMonthDay(int32 Year, int32 DayOfYear, int32& OutMonth, int32& OutDay);

	/** Converts a month and day into a 1-based day of the year. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Month Day To Day Of Year"))
	static int32 MonthDayToDayOfYear(int32 Year, int32 Month, int32 Day);

	/** Day of year of a named solstice or equinox. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Get Solstice Day Of Year"))
	static int32 GetSolsticeDayOfYear(int32 Year, EArchSolsticePreset Preset);

	/** Formats decimal hours as a clock string. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Format Time Of Day"))
	static FText FormatTimeOfDay(float Hours, bool b24Hour = true);

	/** Formats a duration in decimal hours as "11h 32m". */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Format Duration"))
	static FText FormatDuration(float Hours);

	/** Localised season name. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Get Season Display Name"))
	static FText GetSeasonDisplayName(EArchSeason Season);

	/** Localised time-phase name. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Get Time Phase Display Name"))
	static FText GetTimePhaseDisplayName(EArchTimePhase Phase);
};
