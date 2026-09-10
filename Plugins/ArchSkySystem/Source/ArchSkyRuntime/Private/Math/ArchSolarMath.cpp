// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/ArchSolarMath.h"

#include "Util/ArchSkyLog.h"

namespace ArchSolarMath
{
	namespace Detail
	{
		/** Degrees -> radians in double. FMath::DegreesToRadians is templated but this reads better inline. */
		FORCEINLINE double Rad(double Degrees) { return Degrees * (UE_DOUBLE_PI / 180.0); }

		/** Radians -> degrees in double. */
		FORCEINLINE double Deg(double Radians) { return Radians * (180.0 / UE_DOUBLE_PI); }

		/** Wraps to [0, 360). FMath::Fmod keeps the sign of the numerator, so we fix it up. */
		FORCEINLINE double Wrap360(double Degrees)
		{
			double Wrapped = FMath::Fmod(Degrees, 360.0);
			if (Wrapped < 0.0)
			{
				Wrapped += 360.0;
			}
			return Wrapped;
		}

		/** Wraps minutes-of-day to [0, 1440). */
		FORCEINLINE double WrapMinutes(double Minutes)
		{
			double Wrapped = FMath::Fmod(Minutes, 1440.0);
			if (Wrapped < 0.0)
			{
				Wrapped += 1440.0;
			}
			return Wrapped;
		}

		/**
		 * Effective UTC offset for an instant, including the optional DST hour.
		 *
		 * ARCH NOTE: we implement one rule - the EU rule (last Sunday of March 01:00 UTC
		 * to last Sunday of October 01:00 UTC), applied about the equator-flipped dates in
		 * the southern hemisphere. This is deliberately simplistic and is documented as
		 * such; bObserveDST defaults to false and every shipped location preset leaves it
		 * off, because shadow studies are quoted in standard time.
		 */
		double EffectiveUtcOffsetHours(const FArchGeoLocation& Location, const FDateTime& LocalTime)
		{
			double Offset = static_cast<double>(Location.TimezoneOffsetHours);
			if (!Location.bObserveDST)
			{
				return Offset;
			}

			// Last Sunday of a month: walk back from the last day until we hit a Sunday.
			auto LastSundayOfMonth = [](int32 Year, int32 Month) -> int32
			{
				const int32 DaysInMonth = FDateTime::DaysInMonth(Year, Month);
				for (int32 Day = DaysInMonth; Day >= 1; --Day)
				{
					if (FDateTime(Year, Month, Day).GetDayOfWeek() == EDayOfWeek::Sunday)
					{
						return Day;
					}
				}
				return DaysInMonth;
			};

			const int32 Year = LocalTime.GetYear();
			const FDateTime DstStart(Year, 3, LastSundayOfMonth(Year, 3), 2, 0);
			const FDateTime DstEnd(Year, 10, LastSundayOfMonth(Year, 10), 3, 0);

			const bool bInNorthernWindow = (LocalTime >= DstStart) && (LocalTime < DstEnd);
			const bool bIsDst = Location.IsSouthernHemisphere() ? !bInNorthernWindow : bInNorthernWindow;

			return bIsDst ? Offset + 1.0 : Offset;
		}
	}

	// -------------------------------------------------------------------------------------
	// Step 1 - time
	// -------------------------------------------------------------------------------------

	double ToJulianDay(const FDateTime& UTC)
	{
		int32 Year = UTC.GetYear();
		int32 Month = UTC.GetMonth();
		const int32 Day = UTC.GetDay();

		// January and February are treated as months 13 and 14 of the previous year so
		// that the leap day always falls at the end of the "year" the formula counts.
		if (Month <= 2)
		{
			Year -= 1;
			Month += 12;
		}

		const int32 A = Year / 100;
		const int32 B = 2 - A + (A / 4); // Gregorian correction.

		const double DayFraction =
			(static_cast<double>(UTC.GetHour())
				+ static_cast<double>(UTC.GetMinute()) / 60.0
				+ static_cast<double>(UTC.GetSecond()) / 3600.0
				+ static_cast<double>(UTC.GetMillisecond()) / 3600000.0) / 24.0;

		return FMath::FloorToDouble(365.25 * (Year + 4716))
			+ FMath::FloorToDouble(30.6001 * (Month + 1))
			+ static_cast<double>(Day) + static_cast<double>(B) - 1524.5 + DayFraction;
	}

	double ToJulianCentury(double JulianDay)
	{
		return (JulianDay - JulianDayJ2000) / DaysPerJulianCentury;
	}

	// -------------------------------------------------------------------------------------
	// Step 2 - the sun's orbit
	// -------------------------------------------------------------------------------------

	double GeomMeanLongitudeSun(double T)
	{
		return Detail::Wrap360(280.46646 + T * (36000.76983 + T * 0.0003032));
	}

	double GeomMeanAnomalySun(double T)
	{
		return 357.52911 + T * (35999.05029 - 0.0001537 * T);
	}

	double EccentricityEarthOrbit(double T)
	{
		return 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
	}

	double SunEquationOfCenter(double T, double MeanAnomalyDegrees)
	{
		const double M = Detail::Rad(MeanAnomalyDegrees);
		return FMath::Sin(M) * (1.914602 - T * (0.004817 + 0.000014 * T))
			+ FMath::Sin(2.0 * M) * (0.019993 - 0.000101 * T)
			+ FMath::Sin(3.0 * M) * 0.000289;
	}

	double SunTrueLongitude(double MeanLongitudeDegrees, double EquationOfCenterDegrees)
	{
		return MeanLongitudeDegrees + EquationOfCenterDegrees;
	}

	double SunApparentLongitude(double T, double TrueLongitudeDegrees)
	{
		// Omega is the longitude of the Moon's ascending node - the dominant nutation term.
		const double Omega = 125.04 - 1934.136 * T;
		return TrueLongitudeDegrees - 0.00569 - 0.00478 * FMath::Sin(Detail::Rad(Omega));
	}

	double MeanObliquityOfEcliptic(double T)
	{
		const double Seconds = 21.448 - T * (46.815 + T * (0.00059 - T * 0.001813));
		return 23.0 + (26.0 + Seconds / 60.0) / 60.0;
	}

	double ObliquityCorrection(double T, double MeanObliquityDegrees)
	{
		const double Omega = 125.04 - 1934.136 * T;
		return MeanObliquityDegrees + 0.00256 * FMath::Cos(Detail::Rad(Omega));
	}

	double SunDeclination(double ObliquityCorrectedDegrees, double ApparentLongitudeDegrees)
	{
		const double SinDecl = FMath::Sin(Detail::Rad(ObliquityCorrectedDegrees))
			* FMath::Sin(Detail::Rad(ApparentLongitudeDegrees));
		return Detail::Deg(FMath::Asin(FMath::Clamp(SinDecl, -1.0, 1.0)));
	}

	double EquationOfTime(double T)
	{
		const double Epsilon = ObliquityCorrection(T, MeanObliquityOfEcliptic(T));
		const double L0 = GeomMeanLongitudeSun(T);
		const double M = GeomMeanAnomalySun(T);
		const double E = EccentricityEarthOrbit(T);

		// y = tan^2(eps/2) - the "reduction to the equator" factor.
		const double TanHalfEps = FMath::Tan(Detail::Rad(Epsilon) * 0.5);
		const double Y = TanHalfEps * TanHalfEps;

		const double L0Rad = Detail::Rad(L0);
		const double MRad = Detail::Rad(M);

		const double Radians =
			Y * FMath::Sin(2.0 * L0Rad)
			- 2.0 * E * FMath::Sin(MRad)
			+ 4.0 * E * Y * FMath::Sin(MRad) * FMath::Cos(2.0 * L0Rad)
			- 0.5 * Y * Y * FMath::Sin(4.0 * L0Rad)
			- 1.25 * E * E * FMath::Sin(2.0 * MRad);

		// 4 minutes of time per degree of rotation.
		return 4.0 * Detail::Deg(Radians);
	}

	double HourAngleAtZenith(double LatitudeDegrees, double SolarDeclDegrees,
		double ZenithDegrees, bool& bOutIsPolarDay, bool& bOutIsPolarNight)
	{
		bOutIsPolarDay = false;
		bOutIsPolarNight = false;

		const double LatRad = Detail::Rad(LatitudeDegrees);
		const double DeclRad = Detail::Rad(SolarDeclDegrees);

		const double CosLat = FMath::Cos(LatRad);
		const double CosDecl = FMath::Cos(DeclRad);

		// Exactly at a pole cos(lat) is 0 and the expression is singular. Nudge instead of
		// dividing by zero: at the pole the sun's altitude is simply its declination.
		if (FMath::IsNearlyZero(CosLat, 1.e-9) || FMath::IsNearlyZero(CosDecl, 1.e-12))
		{
			const bool bSunUp = (LatitudeDegrees >= 0.0) ? (SolarDeclDegrees > 0.0) : (SolarDeclDegrees < 0.0);
			bOutIsPolarDay = bSunUp;
			bOutIsPolarNight = !bSunUp;
			return 0.0;
		}

		const double CosHourAngle = FMath::Cos(Detail::Rad(ZenithDegrees)) / (CosLat * CosDecl)
			- FMath::Tan(LatRad) * FMath::Tan(DeclRad);

		if (CosHourAngle > 1.0)
		{
			// The sun never climbs to the target zenith: it stays below it all day.
			bOutIsPolarNight = true;
			return 0.0;
		}
		if (CosHourAngle < -1.0)
		{
			// The sun never drops to the target zenith: it stays above it all day.
			bOutIsPolarDay = true;
			return 0.0;
		}

		return Detail::Deg(FMath::Acos(CosHourAngle));
	}

	double HourAngleSunrise(double LatitudeDegrees, double SolarDeclDegrees)
	{
		bool bPolarDay = false;
		bool bPolarNight = false;
		return HourAngleAtZenith(LatitudeDegrees, SolarDeclDegrees, SunriseZenithDegrees, bPolarDay, bPolarNight);
	}

	double ApproxAtmosphericRefraction(double TrueAltitudeDeg)
	{
		// Above 85 degrees refraction is below 0.001 deg - not worth the transcendentals.
		if (TrueAltitudeDeg > 85.0)
		{
			return 0.0;
		}

		const double TanAlt = FMath::Tan(Detail::Rad(TrueAltitudeDeg));
		double RefractionArcSeconds;

		if (TrueAltitudeDeg > 5.0)
		{
			RefractionArcSeconds = 58.1 / TanAlt
				- 0.07 / (TanAlt * TanAlt * TanAlt)
				+ 0.000086 / (TanAlt * TanAlt * TanAlt * TanAlt * TanAlt);
		}
		else if (TrueAltitudeDeg > -0.575)
		{
			// Polynomial fit across the horizon, where tan() blows up.
			const double A = TrueAltitudeDeg;
			RefractionArcSeconds = 1735.0 + A * (-518.2 + A * (103.4 + A * (-12.79 + A * 0.711)));
		}
		else
		{
			RefractionArcSeconds = -20.772 / TanAlt;
		}

		return RefractionArcSeconds / 3600.0;
	}

	double HorizonDipDegrees(double ElevationMeters)
	{
		if (ElevationMeters <= 0.0)
		{
			return 0.0;
		}

		constexpr double EarthRadiusMeters = 6371000.0;
		const double CosDip = EarthRadiusMeters / (EarthRadiusMeters + ElevationMeters);
		return Detail::Deg(FMath::Acos(FMath::Clamp(CosDip, -1.0, 1.0)));
	}

	// -------------------------------------------------------------------------------------
	// Step 3 - position and day info
	// -------------------------------------------------------------------------------------

	FArchSolarPosition CalculateSolarPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime)
	{
		SCOPE_CYCLE_COUNTER(STAT_ArchSky_SolarMath);

		FArchSolarPosition Result;

		const double UtcOffsetHours = Detail::EffectiveUtcOffsetHours(Location, LocalTime);

		// FTimespan takes whole ticks, so build the UTC instant from a fractional-hour span.
		const FDateTime UtcTime = LocalTime - FTimespan::FromHours(UtcOffsetHours);

		const double JD = ToJulianDay(UtcTime);
		const double T = ToJulianCentury(JD);

		const double MeanLong = GeomMeanLongitudeSun(T);
		const double MeanAnom = GeomMeanAnomalySun(T);
		const double EqOfCenter = SunEquationOfCenter(T, MeanAnom);
		const double TrueLong = SunTrueLongitude(MeanLong, EqOfCenter);
		const double AppLong = SunApparentLongitude(T, TrueLong);
		const double ObliqCorr = ObliquityCorrection(T, MeanObliquityOfEcliptic(T));

		const double Declination = SunDeclination(ObliqCorr, AppLong);
		const double EqTimeMinutes = EquationOfTime(T);

		// True solar time, in minutes past local apparent midnight.
		const double LocalMinutes =
			static_cast<double>(LocalTime.GetHour()) * 60.0
			+ static_cast<double>(LocalTime.GetMinute())
			+ static_cast<double>(LocalTime.GetSecond()) / 60.0
			+ static_cast<double>(LocalTime.GetMillisecond()) / 60000.0;

		const double TrueSolarTimeMinutes = Detail::WrapMinutes(
			LocalMinutes + EqTimeMinutes + 4.0 * Location.LongitudeDegrees - 60.0 * UtcOffsetHours);

		// 4 minutes per degree; shift so that noon is hour angle 0.
		double HourAngle = TrueSolarTimeMinutes / 4.0 - 180.0;
		if (HourAngle < -180.0)
		{
			HourAngle += 360.0;
		}

		const double LatRad = Detail::Rad(Location.LatitudeDegrees);
		const double DeclRad = Detail::Rad(Declination);
		const double HourAngleRad = Detail::Rad(HourAngle);

		const double CosZenith = FMath::Clamp(
			FMath::Sin(LatRad) * FMath::Sin(DeclRad)
			+ FMath::Cos(LatRad) * FMath::Cos(DeclRad) * FMath::Cos(HourAngleRad),
			-1.0, 1.0);

		const double ZenithDeg = Detail::Deg(FMath::Acos(CosZenith));
		const double TrueAltitude = 90.0 - ZenithDeg;
		const double Refraction = ApproxAtmosphericRefraction(TrueAltitude);
		const double ApparentAltitude = TrueAltitude + Refraction;

		// Azimuth from the spherical law of cosines, disambiguated by the hour angle sign.
		double Azimuth;
		const double SinZenith = FMath::Sin(Detail::Rad(ZenithDeg));
		if (FMath::IsNearlyZero(SinZenith, 1.e-9) || FMath::IsNearlyZero(FMath::Cos(LatRad), 1.e-9))
		{
			// Sun exactly overhead, or observer exactly at a pole - azimuth is undefined.
			// Fall back to the hour-angle bearing so the value stays continuous.
			Azimuth = Detail::Wrap360(HourAngle + 180.0);
		}
		else
		{
			const double CosAzNumerator = (FMath::Sin(LatRad) * CosZenith) - FMath::Sin(DeclRad);
			const double CosAz = FMath::Clamp(CosAzNumerator / (FMath::Cos(LatRad) * SinZenith), -1.0, 1.0);
			const double AzAcos = Detail::Deg(FMath::Acos(CosAz));

			Azimuth = (HourAngle > 0.0)
				? Detail::Wrap360(AzAcos + 180.0)   // afternoon: sun is west of the meridian
				: Detail::Wrap360(540.0 - AzAcos);  // morning:   sun is east of the meridian
		}

		Result.AzimuthDegrees = Azimuth;
		Result.AltitudeDegrees = ApparentAltitude;
		Result.TrueAltitudeDegrees = TrueAltitude;
		Result.ZenithDegrees = 90.0 - ApparentAltitude;
		Result.DeclinationDegrees = Declination;
		Result.EquationOfTimeMinutes = EqTimeMinutes;
		Result.HourAngleDegrees = HourAngle;
		Result.AtmosphericRefractionDegrees = Refraction;

		// The upper limb clears the horizon at -0.833 deg geometric, plus the dip from altitude.
		const double HorizonThreshold = -(SunriseZenithDegrees - 90.0) - HorizonDipDegrees(Location.ElevationMeters);
		Result.bIsAboveHorizon = TrueAltitude > HorizonThreshold;

		return Result;
	}

	FArchSolarDayInfo CalculateSolarDayInfo(const FArchGeoLocation& Location, const FDateTime& LocalDate)
	{
		SCOPE_CYCLE_COUNTER(STAT_ArchSky_SolarMath);

		FArchSolarDayInfo Info;

		const int32 Year = LocalDate.GetYear();
		const int32 Month = LocalDate.GetMonth();
		const int32 Day = LocalDate.GetDay();
		const FDateTime LocalMidnight(Year, Month, Day);

		const double UtcOffsetHours = Detail::EffectiveUtcOffsetHours(Location, FDateTime(Year, Month, Day, 12));

		// Julian century for an arbitrary local instant on this date.
		auto CenturyAtLocalMinutes = [&](double LocalMinutes) -> double
		{
			const FDateTime Utc = LocalMidnight + FTimespan::FromMinutes(LocalMinutes) - FTimespan::FromHours(UtcOffsetHours);
			return ToJulianCentury(ToJulianDay(Utc));
		};

		// Solar noon, in local minutes past midnight. NOAA:
		//   noon = 720 - 4*longitude - EqTime + 60*timezone
		auto SolarNoonMinutes = [&](double AtCentury) -> double
		{
			return 720.0 - 4.0 * Location.LongitudeDegrees - EquationOfTime(AtCentury) + 60.0 * UtcOffsetHours;
		};

		// First guess at local noon, then one refinement at the resulting instant.
		double NoonMinutes = SolarNoonMinutes(CenturyAtLocalMinutes(720.0));
		NoonMinutes = SolarNoonMinutes(CenturyAtLocalMinutes(NoonMinutes));

		const double NoonCentury = CenturyAtLocalMinutes(NoonMinutes);
		const double NoonDeclination = SunDeclination(
			ObliquityCorrection(NoonCentury, MeanObliquityOfEcliptic(NoonCentury)),
			SunApparentLongitude(NoonCentury, SunTrueLongitude(GeomMeanLongitudeSun(NoonCentury),
				SunEquationOfCenter(NoonCentury, GeomMeanAnomalySun(NoonCentury)))));

		Info.SolarNoonHours = static_cast<float>(NoonMinutes / 60.0);
		Info.MaxAltitudeDegrees = static_cast<float>(90.0 - FMath::Abs(Location.LatitudeDegrees - NoonDeclination));

		// The observer's own horizon sits below the astronomical one when they are up high.
		const double EffectiveSunriseZenith = SunriseZenithDegrees + HorizonDipDegrees(Location.ElevationMeters);

		// Declination and equation-of-time re-evaluated at the crossing instant itself.
		// This is the refinement pass described in the header.
		auto SolveCrossing = [&](double TargetZenith, bool bMorning, bool& bOutPolarDay, bool& bOutPolarNight) -> double
		{
			bool bPolarDay = false;
			bool bPolarNight = false;
			const double FirstHourAngle = HourAngleAtZenith(Location.LatitudeDegrees, NoonDeclination,
				TargetZenith, bPolarDay, bPolarNight);

			bOutPolarDay = bPolarDay;
			bOutPolarNight = bPolarNight;
			if (bPolarDay || bPolarNight)
			{
				return bMorning ? 0.0 : 1440.0;
			}

			const double FirstGuessMinutes = bMorning
				? NoonMinutes - FirstHourAngle * 4.0
				: NoonMinutes + FirstHourAngle * 4.0;

			const double Century = CenturyAtLocalMinutes(FirstGuessMinutes);
			const double Declination = SunDeclination(
				ObliquityCorrection(Century, MeanObliquityOfEcliptic(Century)),
				SunApparentLongitude(Century, SunTrueLongitude(GeomMeanLongitudeSun(Century),
					SunEquationOfCenter(Century, GeomMeanAnomalySun(Century)))));

			const double RefinedHourAngle = HourAngleAtZenith(Location.LatitudeDegrees, Declination,
				TargetZenith, bPolarDay, bPolarNight);

			// A refinement that tips into a polar case means we are within minutes of the
			// polar boundary; keep the first-pass answer rather than reporting nonsense.
			if (bPolarDay || bPolarNight)
			{
				return FirstGuessMinutes;
			}

			const double RefinedNoon = SolarNoonMinutes(Century);
			return bMorning
				? RefinedNoon - RefinedHourAngle * 4.0
				: RefinedNoon + RefinedHourAngle * 4.0;
		};

		bool bPolarDay = false;
		bool bPolarNight = false;
		const double SunriseMinutes = SolveCrossing(EffectiveSunriseZenith, /*bMorning*/ true, bPolarDay, bPolarNight);
		bool bSunsetPolarDay = false;
		bool bSunsetPolarNight = false;
		const double SunsetMinutes = SolveCrossing(EffectiveSunriseZenith, /*bMorning*/ false, bSunsetPolarDay, bSunsetPolarNight);

		Info.bPolarDay = bPolarDay || bSunsetPolarDay;
		Info.bPolarNight = bPolarNight || bSunsetPolarNight;

		if (Info.bPolarDay)
		{
			Info.SunriseHours = 0.f;
			Info.SunsetHours = 24.f;
			Info.DayLengthHours = 24.f;
			Info.CivilTwilightStartHours = 0.f;
			Info.CivilTwilightEndHours = 24.f;
			return Info;
		}
		if (Info.bPolarNight)
		{
			Info.SunriseHours = static_cast<float>(Info.SolarNoonHours);
			Info.SunsetHours = static_cast<float>(Info.SolarNoonHours);
			Info.DayLengthHours = 0.f;
			Info.CivilTwilightStartHours = static_cast<float>(Info.SolarNoonHours);
			Info.CivilTwilightEndHours = static_cast<float>(Info.SolarNoonHours);
			return Info;
		}

		Info.SunriseHours = static_cast<float>(SunriseMinutes / 60.0);
		Info.SunsetHours = static_cast<float>(SunsetMinutes / 60.0);
		Info.DayLengthHours = static_cast<float>((SunsetMinutes - SunriseMinutes) / 60.0);

		// Civil twilight. A latitude can have a normal sunrise but no civil-twilight
		// crossing (high summer at 60 deg N), so fall back to midnight / end-of-day.
		bool bCivilPolarDay = false;
		bool bCivilPolarNight = false;
		const double CivilHourAngle = HourAngleAtZenith(Location.LatitudeDegrees, NoonDeclination,
			CivilTwilightZenithDegrees, bCivilPolarDay, bCivilPolarNight);

		if (bCivilPolarDay)
		{
			Info.CivilTwilightStartHours = 0.f;
			Info.CivilTwilightEndHours = 24.f;
		}
		else if (bCivilPolarNight)
		{
			Info.CivilTwilightStartHours = static_cast<float>(Info.SolarNoonHours);
			Info.CivilTwilightEndHours = static_cast<float>(Info.SolarNoonHours);
		}
		else
		{
			Info.CivilTwilightStartHours = static_cast<float>((NoonMinutes - CivilHourAngle * 4.0) / 60.0);
			Info.CivilTwilightEndHours = static_cast<float>((NoonMinutes + CivilHourAngle * 4.0) / 60.0);
		}

		return Info;
	}

	// -------------------------------------------------------------------------------------
	// Step 4 - the engine boundary
	// -------------------------------------------------------------------------------------

	/*
	 * DERIVATION - astronomical alt/az to an Unreal directional-light rotation.
	 * ------------------------------------------------------------------------
	 * This is the single most bug-prone conversion in a sky system, so here is the whole
	 * argument written out. If you change one line of the code below, re-read this first.
	 *
	 * 1. THE ASTRONOMICAL FRAME
	 *    Azimuth A is a compass bearing: 0 = true North, 90 = East, 180 = South, 270 = West,
	 *    i.e. it increases CLOCKWISE when viewed from above.
	 *    Altitude h is the angle above the horizon plane, positive up.
	 *
	 * 2. THE SCENE FRAME (a choice we make, and must therefore state)
	 *    Unreal is left-handed: +X forward, +Y right, +Z up. We DEFINE:
	 *        +X = True North,  +Y = East,  +Z = Up.
	 *    Unreal's yaw rotates +X towards +Y. With the mapping above, +X -> +Y is
	 *    North -> East, which is clockwise seen from above. Therefore:
	 *
	 *        Unreal yaw and compass azimuth are numerically the SAME quantity.
	 *
	 *    That equality is the entire reason for choosing this axis convention rather than
	 *    the "+X = East" one used by some engines; it removes a sign flip that is otherwise
	 *    impossible to keep straight.
	 *
	 * 3. THE UNIT VECTOR TOWARDS THE SUN
	 *    Decomposing a unit vector at bearing A and altitude h:
	 *        north component = cos(h) * cos(A)   -> X
	 *        east  component = cos(h) * sin(A)   -> Y
	 *        up    component = sin(h)            -> Z
	 *    so  DirToSun = ( cos h cos A, cos h sin A, sin h ).
	 *    Sanity check: A = 90 (east), h = 0 gives (0, 1, 0) = +Y = East. Correct.
	 *
	 * 4. PLAN NORTH
	 *    Architectural plans are almost never modelled with north along +X. We expose
	 *    NorthOffsetDegrees, DEFINED AS: the scene-space yaw at which true north lies.
	 *    So if the modelled plan's "up the page" direction is +X but true north is actually
	 *    30 degrees clockwise of it, NorthOffsetDegrees = 30.
	 *    A celestial bearing A therefore lands at scene yaw  A' = A + NorthOffsetDegrees.
	 *
	 * 5. THE LIGHT ROTATION
	 *    An Unreal directional light emits along its FORWARD vector: forward is the
	 *    direction the photons travel, so it must point FROM the sun TOWARDS the scene:
	 *        LightForward = -DirToSun.
	 *    A rotator (Pitch = P, Yaw = Y, Roll = 0) has forward
	 *        ( cos P cos Y, cos P sin Y, sin P ).
	 *    We need that to equal ( -cos h cos A', -cos h sin A', -sin h ).
	 *    Choose Y = A' + 180, so cos Y = -cos A' and sin Y = -sin A'. Then the X and Y
	 *    components require cos P = cos h, and the Z component requires sin P = -sin h.
	 *    Both are satisfied by P = -h. Hence:
	 *
	 *        Pitch = -Altitude
	 *        Yaw   =  Azimuth + NorthOffsetDegrees + 180
	 *        Roll  =  0
	 *
	 *    Sanity check: sun due east on the horizon (A = 90, h = 0, offset 0) gives
	 *    Pitch 0, Yaw 270 - a light shining due west, i.e. lighting the east faces of
	 *    the building. Correct.
	 *    Sanity check: sun overhead (h = 90) gives Pitch -90 - a light shining straight
	 *    down. Correct.
	 *
	 * 6. WHY NOT JUST USE (-DirToSun).Rotation()?
	 *    We could, and it produces the same rotator. We use the closed form because it is
	 *    branch-free, keeps full double precision through the trig, and - more importantly -
	 *    it is the form the derivation above proves. FVector::Rotation() would hide the
	 *    convention inside engine code and make this comment unverifiable.
	 */
	FRotator SolarToUnrealLightRotation(double AzimuthDeg, double AltitudeDeg, float NorthOffsetDegrees)
	{
		const double SceneYaw = AzimuthDeg + static_cast<double>(NorthOffsetDegrees) + 180.0;

		FRotator Rotation(
			static_cast<double>(-AltitudeDeg),      // Pitch
			Detail::Wrap360(SceneYaw),              // Yaw
			0.0);                                   // Roll

		Rotation.Normalize();
		return Rotation;
	}

	FVector SolarToUnrealDirectionToBody(double AzimuthDeg, double AltitudeDeg, float NorthOffsetDegrees)
	{
		const double SceneAzimuth = Detail::Rad(AzimuthDeg + static_cast<double>(NorthOffsetDegrees));
		const double AltRad = Detail::Rad(AltitudeDeg);
		const double CosAlt = FMath::Cos(AltRad);

		return FVector(
			CosAlt * FMath::Cos(SceneAzimuth),  // North / +X
			CosAlt * FMath::Sin(SceneAzimuth),  // East  / +Y
			FMath::Sin(AltRad));                // Up    / +Z
	}

	double ShadowLengthMultiplier(double AltitudeDegrees, double MaxMultiplier)
	{
		if (AltitudeDegrees <= 0.0)
		{
			return MaxMultiplier;
		}

		const double TanAlt = FMath::Tan(Detail::Rad(AltitudeDegrees));
		if (TanAlt <= UE_DOUBLE_SMALL_NUMBER)
		{
			return MaxMultiplier;
		}

		return FMath::Min(1.0 / TanAlt, MaxMultiplier);
	}

	EArchTimePhase ClassifyTimePhase(double AltitudeDegrees, bool bIsRising)
	{
		// Thresholds are the standard photographic/nautical ones. Note the asymmetry:
		// "golden hour" is conventionally 0..6 deg and "blue hour" -6..-4 deg.
		if (AltitudeDegrees > 6.0)
		{
			return EArchTimePhase::Day;
		}
		if (AltitudeDegrees > 0.833)
		{
			return bIsRising ? EArchTimePhase::GoldenHourMorning : EArchTimePhase::GoldenHourEvening;
		}
		if (AltitudeDegrees > -4.0)
		{
			return bIsRising ? EArchTimePhase::Sunrise : EArchTimePhase::Sunset;
		}
		if (AltitudeDegrees > -6.0)
		{
			return EArchTimePhase::CivilTwilight;
		}
		if (AltitudeDegrees > -12.0)
		{
			return EArchTimePhase::NauticalTwilight;
		}
		if (AltitudeDegrees > -18.0)
		{
			return EArchTimePhase::AstronomicalTwilight;
		}
		return EArchTimePhase::Night;
	}
}

// -----------------------------------------------------------------------------------------
// Blueprint mirror
// -----------------------------------------------------------------------------------------

FArchSolarPosition UArchSolarMathLibrary::CalculateSolarPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime)
{
	return ArchSolarMath::CalculateSolarPosition(Location, LocalTime);
}

FArchSolarDayInfo UArchSolarMathLibrary::CalculateSolarDayInfo(const FArchGeoLocation& Location, const FDateTime& LocalDate)
{
	return ArchSolarMath::CalculateSolarDayInfo(Location, LocalDate);
}

FRotator UArchSolarMathLibrary::SolarToUnrealLightRotation(float AzimuthDegrees, float AltitudeDegrees, float NorthOffsetDegrees)
{
	return ArchSolarMath::SolarToUnrealLightRotation(AzimuthDegrees, AltitudeDegrees, NorthOffsetDegrees);
}

FVector UArchSolarMathLibrary::SolarToUnrealDirectionToBody(float AzimuthDegrees, float AltitudeDegrees, float NorthOffsetDegrees)
{
	return ArchSolarMath::SolarToUnrealDirectionToBody(AzimuthDegrees, AltitudeDegrees, NorthOffsetDegrees);
}

float UArchSolarMathLibrary::ShadowLengthMultiplier(float AltitudeDegrees, float MaxMultiplier)
{
	return static_cast<float>(ArchSolarMath::ShadowLengthMultiplier(AltitudeDegrees, MaxMultiplier));
}

double UArchSolarMathLibrary::EquationOfTimeMinutes(const FDateTime& UTC)
{
	return ArchSolarMath::EquationOfTime(ArchSolarMath::ToJulianCentury(ArchSolarMath::ToJulianDay(UTC)));
}

double UArchSolarMathLibrary::ToJulianDay(const FDateTime& UTC)
{
	return ArchSolarMath::ToJulianDay(UTC);
}
