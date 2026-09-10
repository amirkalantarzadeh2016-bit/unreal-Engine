// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/ArchJalaliCalendar.h"

#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchJalaliCalendar"

namespace ArchJalaliCalendar
{
	namespace Detail
	{
		/**
		 * Borkowski's leap-year "breaks": the Jalali years at which the leap cycle length
		 * changes. Between two consecutive breaks the cycle is a regular 33-year pattern
		 * (with 29- and 37-year cycles appearing at the seams). This table is what makes
		 * the conversion agree with the Iranian civil calendar rather than approximating it.
		 */
		static const int32 LeapYearBreaks[] =
		{
			-61, 9, 38, 199, 426, 686, 756, 818, 1111, 1181,
			1210, 1635, 2060, 2097, 2192, 2262, 2324, 2394, 2456, 3178
		};

		static constexpr int32 NumBreaks = UE_ARRAY_COUNT(LeapYearBreaks);

		/**
		 * Integer division truncating TOWARDS ZERO, matching the reference implementation.
		 * C++ integer division already truncates towards zero, but naming it makes the
		 * intent explicit at every call site - the algorithm is wrong with floor division.
		 */
		FORCEINLINE int32 DivTrunc(int32 A, int32 B) { return A / B; }

		/** Remainder consistent with DivTrunc (keeps the sign of A). */
		FORCEINLINE int32 ModTrunc(int32 A, int32 B) { return A % B; }

		/**
		 * Solves the leap-year structure around a Jalali year.
		 *
		 * @param JalaliYear         Year to analyse.
		 * @param OutLeapOffset      0 when JalaliYear is a leap year; 1..3 otherwise (years since the last leap).
		 * @param OutGregorianYear   The Gregorian year that Farvardin 1 of JalaliYear falls in.
		 * @param OutMarchDay        Day of March on which Farvardin 1 falls in that Gregorian year.
		 * @return                   False if JalaliYear is outside the supported range.
		 */
		bool JalaliCalendarSolve(int32 JalaliYear, int32& OutLeapOffset, int32& OutGregorianYear, int32& OutMarchDay)
		{
			if (JalaliYear < MinSupportedJalaliYear || JalaliYear >= MaxSupportedJalaliYearExclusive)
			{
				return false;
			}

			OutGregorianYear = JalaliYear + 621;

			int32 LeapJ = -14;
			int32 PreviousBreak = LeapYearBreaks[0];
			int32 Jump = 0;

			for (int32 Index = 1; Index < NumBreaks; ++Index)
			{
				const int32 CurrentBreak = LeapYearBreaks[Index];
				Jump = CurrentBreak - PreviousBreak;

				if (JalaliYear < CurrentBreak)
				{
					break;
				}

				// Each 33-year cycle contains 8 leap years; the remainder contributes one
				// leap every 4 years.
				LeapJ += DivTrunc(Jump, 33) * 8 + DivTrunc(ModTrunc(Jump, 33), 4);
				PreviousBreak = CurrentBreak;
			}

			int32 YearsIntoCycle = JalaliYear - PreviousBreak;

			LeapJ += DivTrunc(YearsIntoCycle, 33) * 8 + DivTrunc(ModTrunc(YearsIntoCycle, 33) + 3, 4);
			if (ModTrunc(Jump, 33) == 4 && (Jump - YearsIntoCycle) == 4)
			{
				// The 4-year tail of a 33+4 cycle carries one extra leap day.
				LeapJ += 1;
			}

			// Gregorian leap days elapsed, on the same origin.
			const int32 LeapG = DivTrunc(OutGregorianYear, 4)
				- DivTrunc((DivTrunc(OutGregorianYear, 100) + 1) * 3, 4)
				- 150;

			OutMarchDay = 20 + LeapJ - LeapG;

			// Fold the last (short) cycle back onto a full 33-year one so the leap test below
			// stays a simple modulo.
			if ((Jump - YearsIntoCycle) < 6)
			{
				YearsIntoCycle = YearsIntoCycle - Jump + DivTrunc(Jump + 4, 33) * 33;
			}

			OutLeapOffset = ModTrunc(ModTrunc(YearsIntoCycle + 1, 33) - 1, 4);
			if (OutLeapOffset == -1)
			{
				OutLeapOffset = 4;
			}

			return true;
		}
	}

	int32 GregorianToJulianDayNumber(int32 Year, int32 Month, int32 Day)
	{
		using namespace Detail;

		// Standard Gregorian -> JDN, arranged so every intermediate stays in int32 for
		// any year this plugin will ever see.
		int32 JulianDayNumber = DivTrunc((Year + DivTrunc(Month - 8, 6) + 100100) * 1461, 4)
			+ DivTrunc(153 * ModTrunc(Month + 9, 12) + 2, 5)
			+ Day - 34840408;

		JulianDayNumber -= DivTrunc(DivTrunc(Year + 100100 + DivTrunc(Month - 8, 6), 100) * 3, 4) - 752;

		return JulianDayNumber;
	}

	void JulianDayNumberToGregorian(int32 JulianDayNumber, int32& OutYear, int32& OutMonth, int32& OutDay)
	{
		using namespace Detail;

		int32 J = 4 * JulianDayNumber + 139361631;
		J += DivTrunc(DivTrunc(4 * JulianDayNumber + 183187720, 146097) * 3, 4) * 4 - 3908;

		const int32 I = DivTrunc(ModTrunc(J, 1461), 4) * 5 + 308;

		OutDay = DivTrunc(ModTrunc(I, 153), 5) + 1;
		OutMonth = ModTrunc(DivTrunc(I, 153), 12) + 1;
		OutYear = DivTrunc(J, 1461) - 100100 + DivTrunc(8 - OutMonth, 6);
	}

	int32 JalaliToJulianDayNumber(int32 JalaliYear, int32 JalaliMonth, int32 JalaliDay)
	{
		using namespace Detail;

		int32 LeapOffset = 0;
		int32 GregorianYear = 0;
		int32 MarchDay = 0;

		if (!JalaliCalendarSolve(JalaliYear, LeapOffset, GregorianYear, MarchDay))
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("Jalali year %d is outside the supported range [%d, %d). Returning 0."),
				JalaliYear, MinSupportedJalaliYear, MaxSupportedJalaliYearExclusive);
			return 0;
		}

		// Months 1-6 have 31 days, 7-12 have 30. The DivTrunc(Month,7)*(Month-7) term is
		// the closed form of "subtract one day for every month past the sixth".
		return GregorianToJulianDayNumber(GregorianYear, 3, MarchDay)
			+ (JalaliMonth - 1) * 31
			- DivTrunc(JalaliMonth, 7) * (JalaliMonth - 7)
			+ JalaliDay - 1;
	}

	FArchJalaliDate JulianDayNumberToJalali(int32 JulianDayNumber)
	{
		using namespace Detail;

		int32 GregorianYear = 0;
		int32 GregorianMonth = 0;
		int32 GregorianDay = 0;
		JulianDayNumberToGregorian(JulianDayNumber, GregorianYear, GregorianMonth, GregorianDay);

		int32 JalaliYear = GregorianYear - 621;

		int32 LeapOffset = 0;
		int32 SolvedGregorianYear = 0;
		int32 MarchDay = 0;
		if (!JalaliCalendarSolve(JalaliYear, LeapOffset, SolvedGregorianYear, MarchDay))
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("Gregorian year %d maps outside the supported Jalali range. Returning 1/1/1."),
				GregorianYear);
			return FArchJalaliDate(1, 1, 1);
		}

		const int32 NowruzJdn = GregorianToJulianDayNumber(GregorianYear, 3, MarchDay);
		int32 DaysSinceNowruz = JulianDayNumber - NowruzJdn;

		if (DaysSinceNowruz >= 0)
		{
			if (DaysSinceNowruz <= 185)
			{
				// Inside the six 31-day months.
				return FArchJalaliDate(JalaliYear, 1 + DivTrunc(DaysSinceNowruz, 31), ModTrunc(DaysSinceNowruz, 31) + 1);
			}
			DaysSinceNowruz -= 186;
		}
		else
		{
			// Before Nowruz: we are still in the previous Jalali year's second half.
			JalaliYear -= 1;
			DaysSinceNowruz += 179;
			if (LeapOffset == 1)
			{
				DaysSinceNowruz += 1;
			}
		}

		return FArchJalaliDate(JalaliYear, 7 + DivTrunc(DaysSinceNowruz, 30), ModTrunc(DaysSinceNowruz, 30) + 1);
	}

	FArchJalaliDate GregorianToJalali(int32 Year, int32 Month, int32 Day)
	{
		return JulianDayNumberToJalali(GregorianToJulianDayNumber(Year, Month, Day));
	}

	bool JalaliToGregorian(const FArchJalaliDate& JalaliDate, int32& OutYear, int32& OutMonth, int32& OutDay)
	{
		if (!IsValidJalaliDate(JalaliDate))
		{
			UE_LOG(LogArchSky, Warning, TEXT("Rejected invalid Jalali date %d/%d/%d."),
				JalaliDate.Year, JalaliDate.Month, JalaliDate.Day);
			return false;
		}

		const int32 JulianDayNumber = JalaliToJulianDayNumber(JalaliDate.Year, JalaliDate.Month, JalaliDate.Day);
		if (JulianDayNumber == 0)
		{
			return false;
		}

		JulianDayNumberToGregorian(JulianDayNumber, OutYear, OutMonth, OutDay);
		return true;
	}

	bool IsJalaliLeapYear(int32 JalaliYear)
	{
		int32 LeapOffset = 0;
		int32 GregorianYear = 0;
		int32 MarchDay = 0;

		if (!Detail::JalaliCalendarSolve(JalaliYear, LeapOffset, GregorianYear, MarchDay))
		{
			return false;
		}

		return LeapOffset == 0;
	}

	int32 DaysInJalaliMonth(int32 JalaliYear, int32 JalaliMonth)
	{
		if (JalaliMonth < 1 || JalaliMonth > 12)
		{
			return 0;
		}
		if (JalaliMonth <= 6)
		{
			return 31;
		}
		if (JalaliMonth <= 11)
		{
			return 30;
		}
		// Esfand: 30 days in a leap year, otherwise 29.
		return IsJalaliLeapYear(JalaliYear) ? 30 : 29;
	}

	bool IsValidJalaliDate(const FArchJalaliDate& JalaliDate)
	{
		if (JalaliDate.Year < MinSupportedJalaliYear || JalaliDate.Year >= MaxSupportedJalaliYearExclusive)
		{
			return false;
		}
		if (JalaliDate.Month < 1 || JalaliDate.Month > 12)
		{
			return false;
		}

		return JalaliDate.Day >= 1 && JalaliDate.Day <= DaysInJalaliMonth(JalaliDate.Year, JalaliDate.Month);
	}

	int32 GetJalaliDayOfYear(const FArchJalaliDate& JalaliDate)
	{
		if (!IsValidJalaliDate(JalaliDate))
		{
			return 1;
		}

		int32 DayOfYear = JalaliDate.Day;
		for (int32 Month = 1; Month < JalaliDate.Month; ++Month)
		{
			DayOfYear += DaysInJalaliMonth(JalaliDate.Year, Month);
		}
		return DayOfYear;
	}

	FText GetJalaliMonthName(int32 Month)
	{
		// LOCALISATION: these twelve entries plus the Persian-script set below are the only
		// calendar strings in the plugin. They must appear in the ArchJalaliCalendar
		// namespace of the localisation dashboard; see README.md "Localisation".
		switch (Month)
		{
		case 1:  return LOCTEXT("JalaliMonth_1", "Farvardin");
		case 2:  return LOCTEXT("JalaliMonth_2", "Ordibehesht");
		case 3:  return LOCTEXT("JalaliMonth_3", "Khordad");
		case 4:  return LOCTEXT("JalaliMonth_4", "Tir");
		case 5:  return LOCTEXT("JalaliMonth_5", "Mordad");
		case 6:  return LOCTEXT("JalaliMonth_6", "Shahrivar");
		case 7:  return LOCTEXT("JalaliMonth_7", "Mehr");
		case 8:  return LOCTEXT("JalaliMonth_8", "Aban");
		case 9:  return LOCTEXT("JalaliMonth_9", "Azar");
		case 10: return LOCTEXT("JalaliMonth_10", "Dey");
		case 11: return LOCTEXT("JalaliMonth_11", "Bahman");
		case 12: return LOCTEXT("JalaliMonth_12", "Esfand");
		default: return FText::GetEmpty();
		}
	}

	FText GetJalaliMonthNamePersian(int32 Month)
	{
		// Persian script, supplied directly rather than through the localisation system so
		// that a Persian month name is available even in an English build - the North Offset
		// dial and the date picker show both scripts side by side.
		static const TCHAR* const PersianNames[12] =
		{
			TEXT("\x0641\x0631\x0648\x0631\x062F\x06CC\x0646"),                     // Farvardin
			TEXT("\x0627\x0631\x062F\x06CC\x0628\x0647\x0634\x062A"),               // Ordibehesht
			TEXT("\x062E\x0631\x062F\x0627\x062F"),                                 // Khordad
			TEXT("\x062A\x06CC\x0631"),                                             // Tir
			TEXT("\x0645\x0631\x062F\x0627\x062F"),                                 // Mordad
			TEXT("\x0634\x0647\x0631\x06CC\x0648\x0631"),                           // Shahrivar
			TEXT("\x0645\x0647\x0631"),                                             // Mehr
			TEXT("\x0622\x0628\x0627\x0646"),                                       // Aban
			TEXT("\x0622\x0630\x0631"),                                             // Azar
			TEXT("\x062F\x06CC"),                                                   // Dey
			TEXT("\x0628\x0647\x0645\x0646"),                                       // Bahman
			TEXT("\x0627\x0633\x0641\x0646\x062F")                                  // Esfand
		};

		if (Month < 1 || Month > 12)
		{
			return FText::GetEmpty();
		}

		return FText::FromString(PersianNames[Month - 1]);
	}
}

// -----------------------------------------------------------------------------------------
// Blueprint mirror
// -----------------------------------------------------------------------------------------

FArchJalaliDate UArchJalaliCalendarLibrary::GregorianToJalali(int32 Year, int32 Month, int32 Day)
{
	return ArchJalaliCalendar::GregorianToJalali(Year, Month, Day);
}

bool UArchJalaliCalendarLibrary::JalaliToGregorian(const FArchJalaliDate& JalaliDate, int32& OutYear, int32& OutMonth, int32& OutDay)
{
	OutYear = 1;
	OutMonth = 1;
	OutDay = 1;
	return ArchJalaliCalendar::JalaliToGregorian(JalaliDate, OutYear, OutMonth, OutDay);
}

FArchJalaliDate UArchJalaliCalendarLibrary::DateTimeToJalali(const FDateTime& DateTime)
{
	return ArchJalaliCalendar::GregorianToJalali(DateTime.GetYear(), DateTime.GetMonth(), DateTime.GetDay());
}

bool UArchJalaliCalendarLibrary::IsJalaliLeapYear(int32 JalaliYear)
{
	return ArchJalaliCalendar::IsJalaliLeapYear(JalaliYear);
}

int32 UArchJalaliCalendarLibrary::DaysInJalaliMonth(int32 JalaliYear, int32 JalaliMonth)
{
	return ArchJalaliCalendar::DaysInJalaliMonth(JalaliYear, JalaliMonth);
}

FText UArchJalaliCalendarLibrary::GetJalaliMonthName(int32 Month, bool bPersianScript)
{
	return bPersianScript
		? ArchJalaliCalendar::GetJalaliMonthNamePersian(Month)
		: ArchJalaliCalendar::GetJalaliMonthName(Month);
}

FText UArchJalaliCalendarLibrary::FormatJalaliDate(const FArchJalaliDate& JalaliDate, bool bPersianScript)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Day"), FText::AsNumber(JalaliDate.Day));
	Args.Add(TEXT("Month"), GetJalaliMonthName(JalaliDate.Month, bPersianScript));
	Args.Add(TEXT("Year"), FText::AsNumber(JalaliDate.Year, &FNumberFormattingOptions::DefaultNoGrouping()));

	// Word order is part of the translation, not hard-coded here.
	return FText::Format(LOCTEXT("JalaliDateFormat", "{Day} {Month} {Year}"), Args);
}

#undef LOCTEXT_NAMESPACE
