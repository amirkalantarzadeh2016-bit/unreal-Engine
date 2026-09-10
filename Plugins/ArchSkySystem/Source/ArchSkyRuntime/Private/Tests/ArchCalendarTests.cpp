// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ArchTimeCalendar.h"
#include "Util/ArchJalaliCalendar.h"

#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Calendar tests.
 *
 * Run with:  Automation RunTests ArchSky.Calendar
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchJalaliRoundTripTest,
	"ArchSky.Calendar.Jalali.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchJalaliRoundTripTest::RunTest(const FString& Parameters)
{
	// Every single day from 1900 to 2100 must survive Gregorian -> Jalali -> Gregorian.
	// This is ~73000 iterations of pure integer arithmetic; it runs in a few milliseconds
	// and is the only way to be sure the breaks table has no seam defects.
	const int32 StartJdn = ArchJalaliCalendar::GregorianToJulianDayNumber(1900, 1, 1);
	const int32 EndJdn = ArchJalaliCalendar::GregorianToJulianDayNumber(2101, 1, 1);

	int32 FailureCount = 0;

	for (int32 Jdn = StartJdn; Jdn < EndJdn; ++Jdn)
	{
		const FArchJalaliDate JalaliDate = ArchJalaliCalendar::JulianDayNumberToJalali(Jdn);
		const int32 BackToJdn = ArchJalaliCalendar::JalaliToJulianDayNumber(JalaliDate.Year, JalaliDate.Month, JalaliDate.Day);

		if (BackToJdn != Jdn)
		{
			// Report only the first few so a systematic break does not produce 73000 lines.
			if (FailureCount < 5)
			{
				int32 Year = 0, Month = 0, Day = 0;
				ArchJalaliCalendar::JulianDayNumberToGregorian(Jdn, Year, Month, Day);
				AddError(FString::Printf(
					TEXT("Round trip failed for %04d-%02d-%02d: Jalali %d/%d/%d mapped back to JDN %d, expected %d."),
					Year, Month, Day, JalaliDate.Year, JalaliDate.Month, JalaliDate.Day, BackToJdn, Jdn));
			}
			++FailureCount;
		}
	}

	TestEqual(TEXT("Jalali round-trip failures across 1900-2100"), FailureCount, 0);

	// The Gregorian JDN helpers must themselves round-trip.
	for (int32 Jdn = StartJdn; Jdn < EndJdn; Jdn += 37)
	{
		int32 Year = 0, Month = 0, Day = 0;
		ArchJalaliCalendar::JulianDayNumberToGregorian(Jdn, Year, Month, Day);
		if (!TestEqual(TEXT("Gregorian JDN round trip"),
			ArchJalaliCalendar::GregorianToJulianDayNumber(Year, Month, Day), Jdn))
		{
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchJalaliAnchorTest,
	"ArchSky.Calendar.Jalali.KnownDates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchJalaliAnchorTest::RunTest(const FString& Parameters)
{
	// Nowruz - 1 Farvardin - as observed in Iran. These are the dates the calendar on the
	// wall in Tehran shows, and they are the reason we do not use Birashk's cycle.
	struct FNowruzAnchor
	{
		int32 JalaliYear;
		int32 GregorianYear;
		int32 GregorianMonth;
		int32 GregorianDay;
	};

	static const FNowruzAnchor Anchors[] =
	{
		{ 1398, 2019, 3, 21 },
		{ 1399, 2020, 3, 20 },
		{ 1400, 2021, 3, 21 },
		{ 1403, 2024, 3, 20 },
		{ 1404, 2025, 3, 21 },
		{ 1405, 2026, 3, 21 }
	};

	for (const FNowruzAnchor& Anchor : Anchors)
	{
		int32 Year = 0, Month = 0, Day = 0;
		const bool bConverted = ArchJalaliCalendar::JalaliToGregorian(
			FArchJalaliDate(Anchor.JalaliYear, 1, 1), Year, Month, Day);

		TestTrue(FString::Printf(TEXT("Nowruz %d converts"), Anchor.JalaliYear), bConverted);
		TestEqual(FString::Printf(TEXT("Nowruz %d Gregorian year"), Anchor.JalaliYear), Year, Anchor.GregorianYear);
		TestEqual(FString::Printf(TEXT("Nowruz %d Gregorian month"), Anchor.JalaliYear), Month, Anchor.GregorianMonth);
		TestEqual(FString::Printf(TEXT("Nowruz %d Gregorian day"), Anchor.JalaliYear), Day, Anchor.GregorianDay);
	}

	// And the reverse direction on a date well away from the year boundary.
	const FArchJalaliDate FromGregorian = ArchJalaliCalendar::GregorianToJalali(2026, 9, 10);
	TestEqual(TEXT("2026-09-10 Jalali year"), FromGregorian.Year, 1405);
	TestEqual(TEXT("2026-09-10 Jalali month"), FromGregorian.Month, 6);
	TestEqual(TEXT("2026-09-10 Jalali day"), FromGregorian.Day, 19);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchJalaliStructureTest,
	"ArchSky.Calendar.Jalali.YearStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchJalaliStructureTest::RunTest(const FString& Parameters)
{
	// Known leap years in the current era.
	TestTrue(TEXT("1399 is a Jalali leap year"), ArchJalaliCalendar::IsJalaliLeapYear(1399));
	TestTrue(TEXT("1403 is a Jalali leap year"), ArchJalaliCalendar::IsJalaliLeapYear(1403));
	TestFalse(TEXT("1404 is not a Jalali leap year"), ArchJalaliCalendar::IsJalaliLeapYear(1404));
	TestFalse(TEXT("1405 is not a Jalali leap year"), ArchJalaliCalendar::IsJalaliLeapYear(1405));

	// Month lengths: 31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 29-or-30.
	for (int32 Month = 1; Month <= 6; ++Month)
	{
		TestEqual(FString::Printf(TEXT("Month %d has 31 days"), Month),
			ArchJalaliCalendar::DaysInJalaliMonth(1405, Month), 31);
	}
	for (int32 Month = 7; Month <= 11; ++Month)
	{
		TestEqual(FString::Printf(TEXT("Month %d has 30 days"), Month),
			ArchJalaliCalendar::DaysInJalaliMonth(1405, Month), 30);
	}
	TestEqual(TEXT("Esfand 1405 has 29 days"), ArchJalaliCalendar::DaysInJalaliMonth(1405, 12), 29);
	TestEqual(TEXT("Esfand 1403 has 30 days"), ArchJalaliCalendar::DaysInJalaliMonth(1403, 12), 30);

	// A leap year must contain exactly 366 days between consecutive Nowruz instants.
	for (int32 JalaliYear = 1390; JalaliYear <= 1420; ++JalaliYear)
	{
		const int32 ThisNowruz = ArchJalaliCalendar::JalaliToJulianDayNumber(JalaliYear, 1, 1);
		const int32 NextNowruz = ArchJalaliCalendar::JalaliToJulianDayNumber(JalaliYear + 1, 1, 1);
		const int32 YearLength = NextNowruz - ThisNowruz;

		const int32 Expected = ArchJalaliCalendar::IsJalaliLeapYear(JalaliYear) ? 366 : 365;
		if (!TestEqual(FString::Printf(TEXT("Jalali year %d length"), JalaliYear), YearLength, Expected))
		{
			return false;
		}

		// Day-of-year of the last day must equal the year length.
		const FArchJalaliDate LastDay(JalaliYear, 12, ArchJalaliCalendar::DaysInJalaliMonth(JalaliYear, 12));
		if (!TestEqual(FString::Printf(TEXT("Jalali year %d final day-of-year"), JalaliYear),
			ArchJalaliCalendar::GetJalaliDayOfYear(LastDay), Expected))
		{
			return false;
		}
	}

	// Invalid dates must be rejected rather than silently producing a neighbouring day.
	TestFalse(TEXT("Month 13 is rejected"), ArchJalaliCalendar::IsValidJalaliDate(FArchJalaliDate(1405, 13, 1)));
	TestFalse(TEXT("Day 32 is rejected"), ArchJalaliCalendar::IsValidJalaliDate(FArchJalaliDate(1405, 1, 32)));
	TestFalse(TEXT("30 Esfand in a non-leap year is rejected"), ArchJalaliCalendar::IsValidJalaliDate(FArchJalaliDate(1405, 12, 30)));
	TestTrue(TEXT("30 Esfand in a leap year is accepted"), ArchJalaliCalendar::IsValidJalaliDate(FArchJalaliDate(1403, 12, 30)));

	// Both scripts must produce a name for all twelve months.
	for (int32 Month = 1; Month <= 12; ++Month)
	{
		TestFalse(FString::Printf(TEXT("Month %d has a Latin-script name"), Month),
			ArchJalaliCalendar::GetJalaliMonthName(Month).IsEmpty());
		TestFalse(FString::Printf(TEXT("Month %d has a Persian-script name"), Month),
			ArchJalaliCalendar::GetJalaliMonthNamePersian(Month).IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchGregorianCalendarTest,
	"ArchSky.Calendar.Gregorian.DayOfYear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchGregorianCalendarTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("2024 is a leap year"), ArchTimeCalendar::IsLeapYear(2024));
	TestFalse(TEXT("2026 is not a leap year"), ArchTimeCalendar::IsLeapYear(2026));
	TestFalse(TEXT("1900 is not a leap year"), ArchTimeCalendar::IsLeapYear(1900));
	TestTrue(TEXT("2000 is a leap year"), ArchTimeCalendar::IsLeapYear(2000));

	// Day-of-year must round-trip through month/day for every day of a leap and a
	// non-leap year - this is where an off-by-one on 29 February hides.
	for (const int32 Year : { 2024, 2026 })
	{
		const int32 TotalDays = ArchTimeCalendar::DaysInYear(Year);
		for (int32 DayOfYear = 1; DayOfYear <= TotalDays; ++DayOfYear)
		{
			int32 Month = 0;
			int32 Day = 0;
			ArchTimeCalendar::DayOfYearToMonthDay(Year, DayOfYear, Month, Day);

			if (!TestEqual(FString::Printf(TEXT("Day-of-year round trip for %d day %d"), Year, DayOfYear),
				ArchTimeCalendar::MonthDayToDayOfYear(Year, Month, Day), DayOfYear))
			{
				return false;
			}

			// And it must agree with the engine's own calendar.
			const FDateTime Engine(Year, Month, Day);
			if (!TestEqual(FString::Printf(TEXT("FDateTime agrees for %d day %d"), Year, DayOfYear),
				Engine.GetDayOfYear(), DayOfYear))
			{
				return false;
			}
		}
	}

	// Known day-of-year values.
	TestEqual(TEXT("21 June 2026 is day 172"), ArchTimeCalendar::MonthDayToDayOfYear(2026, 6, 21), 172);
	TestEqual(TEXT("21 June 2024 is day 173 (leap)"), ArchTimeCalendar::MonthDayToDayOfYear(2024, 6, 21), 173);
	TestEqual(TEXT("31 December 2026 is day 365"), ArchTimeCalendar::MonthDayToDayOfYear(2026, 12, 31), 365);
	TestEqual(TEXT("31 December 2024 is day 366"), ArchTimeCalendar::MonthDayToDayOfYear(2024, 12, 31), 366);

	// Solstice presets.
	TestEqual(TEXT("Summer solstice 2026 is day 172"),
		ArchTimeCalendar::GetSolsticeDayOfYear(2026, EArchSolsticePreset::SummerSolstice), 172);
	TestEqual(TEXT("Winter solstice 2026 is day 355"),
		ArchTimeCalendar::GetSolsticeDayOfYear(2026, EArchSolsticePreset::WinterSolstice), 355);

	// Seasons, northern and southern.
	TestEqual(TEXT("Midsummer in the north is Summer"),
		static_cast<int32>(ArchTimeCalendar::GetSeason(2026, 172, false)), static_cast<int32>(EArchSeason::Summer));
	TestEqual(TEXT("Midsummer in the south is Winter"),
		static_cast<int32>(ArchTimeCalendar::GetSeason(2026, 172, true)), static_cast<int32>(EArchSeason::Winter));
	TestEqual(TEXT("January in the north is Winter"),
		static_cast<int32>(ArchTimeCalendar::GetSeason(2026, 15, false)), static_cast<int32>(EArchSeason::Winter));

	// Season blend is continuous and correctly phased.
	TestEqual(TEXT("Winter solstice blends to 0"),
		ArchTimeCalendar::GetSeasonBlend01(2026, 355, false), 0.f, 0.01f);
	TestEqual(TEXT("Summer solstice blends to 1"),
		ArchTimeCalendar::GetSeasonBlend01(2026, 172, false), 1.f, 0.01f);
	TestEqual(TEXT("The southern hemisphere blend is inverted"),
		ArchTimeCalendar::GetSeasonBlend01(2026, 172, true), 0.f, 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchTimeFormattingTest,
	"ArchSky.Calendar.TimeFormatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchTimeFormattingTest::RunTest(const FString& Parameters)
{
	// The string comparisons below assert English formatting. Pin the culture for the
	// duration of the test so that running the suite in a Persian editor - which uses
	// Eastern Arabic numerals - does not produce spurious failures.
	FInternationalization& I18N = FInternationalization::Get();
	const FString PreviousCulture = I18N.GetCurrentCulture()->GetName();
	I18N.SetCurrentCulture(TEXT("en"));
	ON_SCOPE_EXIT
	{
		I18N.SetCurrentCulture(PreviousCulture);
	};

	// Hour wrapping.
	TestEqual(TEXT("24.0 wraps to 0"), ArchTimeCalendar::WrapHours(24.f), 0.f, 1.e-4f);
	TestEqual(TEXT("25.5 wraps to 1.5"), ArchTimeCalendar::WrapHours(25.5f), 1.5f, 1.e-4f);
	TestEqual(TEXT("-1.0 wraps to 23"), ArchTimeCalendar::WrapHours(-1.f), 23.f, 1.e-4f);
	TestEqual(TEXT("-0.25 wraps to 23.75"), ArchTimeCalendar::WrapHours(-0.25f), 23.75f, 1.e-4f);

	for (float Hours = -100.f; Hours < 100.f; Hours += 0.37f)
	{
		const float Wrapped = ArchTimeCalendar::WrapHours(Hours);
		if (!TestTrue(TEXT("Wrapped hours always land inside [0, 24)"), Wrapped >= 0.f && Wrapped < 24.f))
		{
			return false;
		}
	}

	// The rounding edge case: 13.999 h must read 14:00, never 13:60.
	TestEqual(TEXT("13.999 h formats as 14:00"),
		ArchTimeCalendar::FormatTimeOfDay(13.999f, true).ToString(), FString(TEXT("14:00")));
	TestEqual(TEXT("6.7 h formats as 06:42"),
		ArchTimeCalendar::FormatTimeOfDay(6.7f, true).ToString(), FString(TEXT("06:42")));
	TestEqual(TEXT("23.999 h wraps to 00:00"),
		ArchTimeCalendar::FormatTimeOfDay(23.999f, true).ToString(), FString(TEXT("00:00")));
	TestEqual(TEXT("0 h in 12-hour form is 12:00 AM"),
		ArchTimeCalendar::FormatTimeOfDay(0.f, false).ToString(), FString(TEXT("12:00 AM")));
	TestEqual(TEXT("12 h in 12-hour form is 12:00 PM"),
		ArchTimeCalendar::FormatTimeOfDay(12.f, false).ToString(), FString(TEXT("12:00 PM")));
	TestEqual(TEXT("14.5 h in 12-hour form is 2:30 PM"),
		ArchTimeCalendar::FormatTimeOfDay(14.5f, false).ToString(), FString(TEXT("2:30 PM")));

	// Durations.
	TestEqual(TEXT("11.533 h formats as 11h 32m"),
		ArchTimeCalendar::FormatDuration(11.5333f).ToString(), FString(TEXT("11h 32m")));
	TestEqual(TEXT("24 h formats as 24h 0m"),
		ArchTimeCalendar::FormatDuration(24.f).ToString(), FString(TEXT("24h 0m")));

	// MakeDateTime must be consistent with the day-of-year helpers.
	const FDateTime Made = ArchTimeCalendar::MakeDateTime(2026, 172, 13.5f);
	TestEqual(TEXT("MakeDateTime year"), Made.GetYear(), 2026);
	TestEqual(TEXT("MakeDateTime month"), Made.GetMonth(), 6);
	TestEqual(TEXT("MakeDateTime day"), Made.GetDay(), 21);
	TestEqual(TEXT("MakeDateTime hour"), Made.GetHour(), 13);
	TestEqual(TEXT("MakeDateTime minute"), Made.GetMinute(), 30);

	// Out-of-range day-of-year must clamp, not assert.
	const FDateTime Clamped = ArchTimeCalendar::MakeDateTime(2026, 999, 12.f);
	TestEqual(TEXT("Out-of-range day-of-year clamps to 31 December"), Clamped.GetMonth(), 12);
	TestEqual(TEXT("Out-of-range day-of-year clamps to day 31"), Clamped.GetDay(), 31);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
