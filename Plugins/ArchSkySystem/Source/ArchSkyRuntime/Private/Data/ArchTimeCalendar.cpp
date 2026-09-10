// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ArchTimeCalendar.h"

#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchTimeCalendar"

namespace ArchTimeCalendar
{
	namespace Detail
	{
		/** Cumulative days before the start of each month, non-leap year. */
		static const int32 CumulativeDaysBeforeMonth[13] =
		{
			0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
		};

		static const int32 DaysPerMonth[13] =
		{
			0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
		};
	}

	bool IsLeapYear(int32 Year)
	{
		return (Year % 4 == 0) && ((Year % 100 != 0) || (Year % 400 == 0));
	}

	int32 DaysInYear(int32 Year)
	{
		return IsLeapYear(Year) ? 366 : 365;
	}

	void DayOfYearToMonthDay(int32 Year, int32 DayOfYear, int32& OutMonth, int32& OutDay)
	{
		const int32 TotalDays = DaysInYear(Year);
		DayOfYear = FMath::Clamp(DayOfYear, 1, TotalDays);

		const bool bLeap = IsLeapYear(Year);
		int32 Remaining = DayOfYear;

		for (int32 Month = 1; Month <= 12; ++Month)
		{
			const int32 MonthLength = Detail::DaysPerMonth[Month] + ((bLeap && Month == 2) ? 1 : 0);
			if (Remaining <= MonthLength)
			{
				OutMonth = Month;
				OutDay = Remaining;
				return;
			}
			Remaining -= MonthLength;
		}

		// Unreachable given the clamp above, but leave the outputs defined.
		OutMonth = 12;
		OutDay = 31;
	}

	int32 MonthDayToDayOfYear(int32 Year, int32 Month, int32 Day)
	{
		Month = FMath::Clamp(Month, 1, 12);

		const bool bLeap = IsLeapYear(Year);
		const int32 MonthLength = Detail::DaysPerMonth[Month] + ((bLeap && Month == 2) ? 1 : 0);
		Day = FMath::Clamp(Day, 1, MonthLength);

		const int32 LeapAdjust = (bLeap && Month > 2) ? 1 : 0;
		return Detail::CumulativeDaysBeforeMonth[Month] + Day + LeapAdjust;
	}

	FDateTime MakeDateTime(int32 Year, int32 DayOfYear, float TimeOfDayHours)
	{
		int32 Month = 1;
		int32 Day = 1;
		DayOfYearToMonthDay(Year, DayOfYear, Month, Day);

		// FDateTime's constructor validates its arguments with a check(), so the clamping
		// inside DayOfYearToMonthDay is load-bearing, not decorative.
		return FDateTime(Year, Month, Day) + FTimespan::FromHours(static_cast<double>(WrapHours(TimeOfDayHours)));
	}

	float WrapHours(float Hours)
	{
		float Wrapped = FMath::Fmod(Hours, 24.f);
		if (Wrapped < 0.f)
		{
			Wrapped += 24.f;
		}
		// Fmod of a value just under 24 can round up to exactly 24 in float; fold it back.
		return (Wrapped >= 24.f) ? 0.f : Wrapped;
	}

	int32 GetSolsticeDayOfYear(int32 Year, EArchSolsticePreset Preset)
	{
		switch (Preset)
		{
		case EArchSolsticePreset::SpringEquinox:  return MonthDayToDayOfYear(Year, 3, 20);
		case EArchSolsticePreset::SummerSolstice: return MonthDayToDayOfYear(Year, 6, 21);
		case EArchSolsticePreset::AutumnEquinox:  return MonthDayToDayOfYear(Year, 9, 22);
		case EArchSolsticePreset::WinterSolstice: return MonthDayToDayOfYear(Year, 12, 21);
		default:                                  return MonthDayToDayOfYear(Year, 6, 21);
		}
	}

	EArchSeason GetSeason(int32 Year, int32 DayOfYear, bool bSouthernHemisphere)
	{
		const int32 Spring = GetSolsticeDayOfYear(Year, EArchSolsticePreset::SpringEquinox);
		const int32 Summer = GetSolsticeDayOfYear(Year, EArchSolsticePreset::SummerSolstice);
		const int32 Autumn = GetSolsticeDayOfYear(Year, EArchSolsticePreset::AutumnEquinox);
		const int32 Winter = GetSolsticeDayOfYear(Year, EArchSolsticePreset::WinterSolstice);

		EArchSeason Season;
		if (DayOfYear < Spring || DayOfYear >= Winter)
		{
			Season = EArchSeason::Winter;
		}
		else if (DayOfYear < Summer)
		{
			Season = EArchSeason::Spring;
		}
		else if (DayOfYear < Autumn)
		{
			Season = EArchSeason::Summer;
		}
		else
		{
			Season = EArchSeason::Autumn;
		}

		if (!bSouthernHemisphere)
		{
			return Season;
		}

		// Southern hemisphere: the seasons are exactly six months out of phase.
		switch (Season)
		{
		case EArchSeason::Winter: return EArchSeason::Summer;
		case EArchSeason::Spring: return EArchSeason::Autumn;
		case EArchSeason::Summer: return EArchSeason::Winter;
		default:                  return EArchSeason::Spring;
		}
	}

	float GetSeasonBlend01(int32 Year, int32 DayOfYear, bool bSouthernHemisphere)
	{
		// A cosine phase-locked so that the winter solstice reads 0 and the summer
		// solstice reads 1. Continuous across the year boundary, which a piecewise
		// season lookup is not - materials need the continuity.
		const int32 WinterDay = GetSolsticeDayOfYear(Year, EArchSolsticePreset::WinterSolstice);
		const int32 TotalDays = DaysInYear(Year);

		const float Phase = (static_cast<float>(DayOfYear - WinterDay) / static_cast<float>(TotalDays)) * 2.f * UE_PI;
		const float Blend = 0.5f - 0.5f * FMath::Cos(Phase);

		return bSouthernHemisphere ? (1.f - Blend) : Blend;
	}

	FText GetGregorianMonthName(int32 Month)
	{
		switch (Month)
		{
		case 1:  return LOCTEXT("Month_1", "January");
		case 2:  return LOCTEXT("Month_2", "February");
		case 3:  return LOCTEXT("Month_3", "March");
		case 4:  return LOCTEXT("Month_4", "April");
		case 5:  return LOCTEXT("Month_5", "May");
		case 6:  return LOCTEXT("Month_6", "June");
		case 7:  return LOCTEXT("Month_7", "July");
		case 8:  return LOCTEXT("Month_8", "August");
		case 9:  return LOCTEXT("Month_9", "September");
		case 10: return LOCTEXT("Month_10", "October");
		case 11: return LOCTEXT("Month_11", "November");
		case 12: return LOCTEXT("Month_12", "December");
		default: return FText::GetEmpty();
		}
	}

	FText GetSeasonDisplayName(EArchSeason Season)
	{
		switch (Season)
		{
		case EArchSeason::Spring: return LOCTEXT("Season_Spring", "Spring");
		case EArchSeason::Summer: return LOCTEXT("Season_Summer", "Summer");
		case EArchSeason::Autumn: return LOCTEXT("Season_Autumn", "Autumn");
		case EArchSeason::Winter: return LOCTEXT("Season_Winter", "Winter");
		default:                  return FText::GetEmpty();
		}
	}

	FText GetTimePhaseDisplayName(EArchTimePhase Phase)
	{
		switch (Phase)
		{
		case EArchTimePhase::Night:                 return LOCTEXT("Phase_Night", "Night");
		case EArchTimePhase::AstronomicalTwilight:  return LOCTEXT("Phase_AstroTwilight", "Astronomical Twilight");
		case EArchTimePhase::NauticalTwilight:      return LOCTEXT("Phase_NauticalTwilight", "Nautical Twilight");
		case EArchTimePhase::CivilTwilight:         return LOCTEXT("Phase_CivilTwilight", "Civil Twilight");
		case EArchTimePhase::Sunrise:               return LOCTEXT("Phase_Sunrise", "Sunrise");
		case EArchTimePhase::GoldenHourMorning:     return LOCTEXT("Phase_GoldenMorning", "Golden Hour (Morning)");
		case EArchTimePhase::Day:                   return LOCTEXT("Phase_Day", "Day");
		case EArchTimePhase::GoldenHourEvening:     return LOCTEXT("Phase_GoldenEvening", "Golden Hour (Evening)");
		case EArchTimePhase::Sunset:                return LOCTEXT("Phase_Sunset", "Sunset");
		default:                                    return FText::GetEmpty();
		}
	}

	FText FormatTimeOfDay(float Hours, bool b24Hour)
	{
		const float Wrapped = WrapHours(Hours);

		int32 Hour = FMath::FloorToInt32(Wrapped);
		int32 Minute = FMath::RoundToInt32((Wrapped - static_cast<float>(Hour)) * 60.f);

		// Rounding 13.999 h must produce 14:00, not 13:60.
		if (Minute >= 60)
		{
			Minute -= 60;
			Hour = (Hour + 1) % 24;
		}

		FFormatNamedArguments Args;
		FNumberFormattingOptions TwoDigits;
		TwoDigits.MinimumIntegralDigits = 2;
		TwoDigits.UseGrouping = false;

		Args.Add(TEXT("Minute"), FText::AsNumber(Minute, &TwoDigits));

		if (b24Hour)
		{
			Args.Add(TEXT("Hour"), FText::AsNumber(Hour, &TwoDigits));
			return FText::Format(LOCTEXT("Time24Format", "{Hour}:{Minute}"), Args);
		}

		const int32 Hour12 = (Hour % 12 == 0) ? 12 : (Hour % 12);
		FNumberFormattingOptions NoGrouping;
		NoGrouping.UseGrouping = false;

		Args.Add(TEXT("Hour"), FText::AsNumber(Hour12, &NoGrouping));
		Args.Add(TEXT("Meridiem"), (Hour < 12) ? LOCTEXT("TimeAM", "AM") : LOCTEXT("TimePM", "PM"));

		return FText::Format(LOCTEXT("Time12Format", "{Hour}:{Minute} {Meridiem}"), Args);
	}

	FText FormatDuration(float Hours)
	{
		const float Absolute = FMath::Abs(Hours);
		int32 WholeHours = FMath::FloorToInt32(Absolute);
		int32 Minutes = FMath::RoundToInt32((Absolute - static_cast<float>(WholeHours)) * 60.f);

		if (Minutes >= 60)
		{
			Minutes -= 60;
			WholeHours += 1;
		}

		FNumberFormattingOptions NoGrouping;
		NoGrouping.UseGrouping = false;

		FFormatNamedArguments Args;
		Args.Add(TEXT("Hours"), FText::AsNumber(WholeHours, &NoGrouping));
		Args.Add(TEXT("Minutes"), FText::AsNumber(Minutes, &NoGrouping));

		return FText::Format(LOCTEXT("DurationFormat", "{Hours}h {Minutes}m"), Args);
	}
}

// -----------------------------------------------------------------------------------------
// Blueprint mirror
// -----------------------------------------------------------------------------------------

bool UArchTimeCalendarLibrary::IsLeapYear(int32 Year)
{
	return ArchTimeCalendar::IsLeapYear(Year);
}

void UArchTimeCalendarLibrary::DayOfYearToMonthDay(int32 Year, int32 DayOfYear, int32& OutMonth, int32& OutDay)
{
	OutMonth = 1;
	OutDay = 1;
	ArchTimeCalendar::DayOfYearToMonthDay(Year, DayOfYear, OutMonth, OutDay);
}

int32 UArchTimeCalendarLibrary::MonthDayToDayOfYear(int32 Year, int32 Month, int32 Day)
{
	return ArchTimeCalendar::MonthDayToDayOfYear(Year, Month, Day);
}

int32 UArchTimeCalendarLibrary::GetSolsticeDayOfYear(int32 Year, EArchSolsticePreset Preset)
{
	return ArchTimeCalendar::GetSolsticeDayOfYear(Year, Preset);
}

FText UArchTimeCalendarLibrary::FormatTimeOfDay(float Hours, bool b24Hour)
{
	return ArchTimeCalendar::FormatTimeOfDay(Hours, b24Hour);
}

FText UArchTimeCalendarLibrary::FormatDuration(float Hours)
{
	return ArchTimeCalendar::FormatDuration(Hours);
}

FText UArchTimeCalendarLibrary::GetSeasonDisplayName(EArchSeason Season)
{
	return ArchTimeCalendar::GetSeasonDisplayName(Season);
}

FText UArchTimeCalendarLibrary::GetTimePhaseDisplayName(EArchTimePhase Phase)
{
	return ArchTimeCalendar::GetTimePhaseDisplayName(Phase);
}

#undef LOCTEXT_NAMESPACE
