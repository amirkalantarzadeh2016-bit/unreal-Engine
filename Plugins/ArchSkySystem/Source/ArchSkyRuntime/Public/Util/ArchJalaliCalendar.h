// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArchJalaliCalendar.generated.h"

/** A date in the Jalali (Solar Hijri) calendar. */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchJalaliDate
{
	GENERATED_BODY()

	/** Solar Hijri year, e.g. 1405. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Calendar", meta = (ClampMin = "1", UIMin = "1300", UIMax = "1500"))
	int32 Year = 1405;

	/** Month, 1 (Farvardin) .. 12 (Esfand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Calendar", meta = (ClampMin = "1", ClampMax = "12", UIMin = "1", UIMax = "12"))
	int32 Month = 1;

	/** Day of month, 1 .. 31. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchSky|Calendar", meta = (ClampMin = "1", ClampMax = "31", UIMin = "1", UIMax = "31"))
	int32 Day = 1;

	FArchJalaliDate() = default;

	FArchJalaliDate(int32 InYear, int32 InMonth, int32 InDay)
		: Year(InYear), Month(InMonth), Day(InDay)
	{
	}

	bool operator==(const FArchJalaliDate& Other) const
	{
		return Year == Other.Year && Month == Other.Month && Day == Other.Day;
	}

	bool operator!=(const FArchJalaliDate& Other) const { return !(*this == Other); }
};

/**
 * Jalali (Solar Hijri) calendar conversion.
 *
 * ARCH NOTE: there are two competing arithmetic approximations of this calendar.
 *
 *   1. Birashk's 2820-year cycle. Compact, closed-form, and WRONG for a handful of years
 *      in the current era - it puts Nowruz 1404 on 20 March 2025 when Iran observed it on
 *      21 March.
 *   2. The "leap-year breaks" table (Borkowski 1996), which encodes the actual observed
 *      33/29/37-year leap cycles. Exact against the Iranian civil calendar for years
 *      1178-1633 AP, which spans 1799-2256 CE.
 *
 * We use (2). An architect in Tehran typing a Jalali date and getting a day-shifted sun
 * position would be an unacceptable defect, and the extra cost is one small static table
 * plus a loop of at most twenty iterations. Outside the supported range we clamp and warn
 * rather than returning silently wrong dates.
 *
 * THREAD SAFETY: pure functions, no state, callable from any thread.
 */
namespace ArchJalaliCalendar
{
	/** First Jalali year the breaks table can represent. */
	inline constexpr int32 MinSupportedJalaliYear = -61;

	/** One past the last Jalali year the breaks table can represent. */
	inline constexpr int32 MaxSupportedJalaliYearExclusive = 3178;

	/** Julian Day Number (integer, midnight-based) for a Gregorian calendar date. */
	ARCHSKYRUNTIME_API int32 GregorianToJulianDayNumber(int32 Year, int32 Month, int32 Day);

	/** Gregorian calendar date for a Julian Day Number. */
	ARCHSKYRUNTIME_API void JulianDayNumberToGregorian(int32 JulianDayNumber, int32& OutYear, int32& OutMonth, int32& OutDay);

	/** Julian Day Number for a Jalali date. Returns 0 and warns if the year is unsupported. */
	ARCHSKYRUNTIME_API int32 JalaliToJulianDayNumber(int32 JalaliYear, int32 JalaliMonth, int32 JalaliDay);

	/** Jalali date for a Julian Day Number. */
	ARCHSKYRUNTIME_API FArchJalaliDate JulianDayNumberToJalali(int32 JulianDayNumber);

	/** Converts a Gregorian date to Jalali. */
	ARCHSKYRUNTIME_API FArchJalaliDate GregorianToJalali(int32 Year, int32 Month, int32 Day);

	/** Converts a Jalali date to Gregorian. Returns false (leaving outputs untouched) if invalid. */
	ARCHSKYRUNTIME_API bool JalaliToGregorian(const FArchJalaliDate& JalaliDate, int32& OutYear, int32& OutMonth, int32& OutDay);

	/** True if the given Jalali year has 366 days (Esfand runs to 30). */
	ARCHSKYRUNTIME_API bool IsJalaliLeapYear(int32 JalaliYear);

	/** Number of days in a Jalali month: 31 for months 1-6, 30 for 7-11, 29 or 30 for Esfand. */
	ARCHSKYRUNTIME_API int32 DaysInJalaliMonth(int32 JalaliYear, int32 JalaliMonth);

	/** True if the year/month/day triple is a real date in the Jalali calendar. */
	ARCHSKYRUNTIME_API bool IsValidJalaliDate(const FArchJalaliDate& JalaliDate);

	/** Localised Jalali month name, e.g. "Farvardin". Month is 1-based. */
	ARCHSKYRUNTIME_API FText GetJalaliMonthName(int32 Month);

	/** Jalali month name in Persian script, e.g. U+0641... "Farvardin". Month is 1-based. */
	ARCHSKYRUNTIME_API FText GetJalaliMonthNamePersian(int32 Month);

	/** Day of the Jalali year, 1 .. 365/366. */
	ARCHSKYRUNTIME_API int32 GetJalaliDayOfYear(const FArchJalaliDate& JalaliDate);
}

/** Blueprint mirror of ArchJalaliCalendar. */
UCLASS(meta = (ScriptName = "ArchJalaliCalendar"))
class ARCHSKYRUNTIME_API UArchJalaliCalendarLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Converts a Gregorian year/month/day to a Jalali date. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Gregorian To Jalali"))
	static FArchJalaliDate GregorianToJalali(int32 Year, int32 Month, int32 Day);

	/** Converts a Jalali date to a Gregorian year/month/day. Returns false if the date is invalid. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Calendar", meta = (DisplayName = "Jalali To Gregorian"))
	static bool JalaliToGregorian(const FArchJalaliDate& JalaliDate, int32& OutYear, int32& OutMonth, int32& OutDay);

	/** Converts an FDateTime to a Jalali date, ignoring the time of day. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Date Time To Jalali"))
	static FArchJalaliDate DateTimeToJalali(const FDateTime& DateTime);

	/** True if the given Jalali year is a leap year. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Is Jalali Leap Year"))
	static bool IsJalaliLeapYear(int32 JalaliYear);

	/** Days in the given Jalali month. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Days In Jalali Month"))
	static int32 DaysInJalaliMonth(int32 JalaliYear, int32 JalaliMonth);

	/** Localised Jalali month name. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Get Jalali Month Name"))
	static FText GetJalaliMonthName(int32 Month, bool bPersianScript = false);

	/** Formats a Jalali date as "1405 Shahrivar 19" style text, respecting the locale. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Calendar", meta = (DisplayName = "Format Jalali Date"))
	static FText FormatJalaliDate(const FArchJalaliDate& JalaliDate, bool bPersianScript = false);
};
