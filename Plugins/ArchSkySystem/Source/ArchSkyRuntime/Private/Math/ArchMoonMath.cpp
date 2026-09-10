// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/ArchMoonMath.h"

#include "Math/ArchSolarMath.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchMoonMath"

namespace ArchMoonMath
{
	namespace Detail
	{
		FORCEINLINE double Rad(double Degrees) { return Degrees * (UE_DOUBLE_PI / 180.0); }
		FORCEINLINE double Deg(double Radians) { return Radians * (180.0 / UE_DOUBLE_PI); }

		FORCEINLINE double Wrap360(double Degrees)
		{
			double Wrapped = FMath::Fmod(Degrees, 360.0);
			if (Wrapped < 0.0)
			{
				Wrapped += 360.0;
			}
			return Wrapped;
		}

		/** sin() of an angle given in degrees. */
		FORCEINLINE double SinD(double Degrees) { return FMath::Sin(Rad(Degrees)); }

		/** cos() of an angle given in degrees. */
		FORCEINLINE double CosD(double Degrees) { return FMath::Cos(Rad(Degrees)); }

		/** Effective UTC offset, duplicated from ArchSolarMath's private detail to keep
		 *  the two namespaces independent of each other's internals. */
		double UtcOffsetHours(const FArchGeoLocation& Location, const FDateTime& LocalTime)
		{
			double Offset = static_cast<double>(Location.TimezoneOffsetHours);
			if (!Location.bObserveDST)
			{
				return Offset;
			}

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

	void CalculateGeocentricEcliptic(double JulianDay,
		double& OutLongitudeDeg, double& OutLatitudeDeg, double& OutDistanceKm)
	{
		using namespace Detail;

		const double T = ArchSolarMath::ToJulianCentury(JulianDay);
		const double T2 = T * T;
		const double T3 = T2 * T;
		const double T4 = T3 * T;

		// Meeus 47.1 - 47.5. The five fundamental arguments, all in degrees.
		const double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0; // Mean longitude
		const double D  = 297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0; // Mean elongation
		const double M  = 357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000.0;                   // Sun's mean anomaly
		const double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0;    // Moon's mean anomaly
		const double F  = 93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0;  // Argument of latitude

		// --- Longitude: the 20 largest periodic terms of Meeus table 47.A (degrees). ---
		const double DeltaLongitude =
			  6.288774 * SinD(Mp)
			+ 1.274027 * SinD(2.0 * D - Mp)
			+ 0.658314 * SinD(2.0 * D)
			+ 0.213618 * SinD(2.0 * Mp)
			- 0.185116 * SinD(M)
			- 0.114332 * SinD(2.0 * F)
			+ 0.058793 * SinD(2.0 * D - 2.0 * Mp)
			+ 0.057066 * SinD(2.0 * D - M - Mp)
			+ 0.053322 * SinD(2.0 * D + Mp)
			+ 0.045758 * SinD(2.0 * D - M)
			- 0.040923 * SinD(M - Mp)
			- 0.034720 * SinD(D)
			- 0.030383 * SinD(M + Mp)
			+ 0.015327 * SinD(2.0 * D - 2.0 * F)
			- 0.012528 * SinD(Mp + 2.0 * F)
			+ 0.010980 * SinD(Mp - 2.0 * F)
			+ 0.010675 * SinD(4.0 * D - Mp)
			+ 0.010034 * SinD(3.0 * Mp)
			+ 0.008548 * SinD(4.0 * D - 2.0 * Mp)
			- 0.007888 * SinD(2.0 * D + M - Mp);

		// --- Latitude: the 12 largest terms of Meeus table 47.B (degrees). ---
		const double Latitude =
			  5.128122 * SinD(F)
			+ 0.280602 * SinD(Mp + F)
			+ 0.277693 * SinD(Mp - F)
			+ 0.173237 * SinD(2.0 * D - F)
			+ 0.055413 * SinD(2.0 * D - Mp + F)
			+ 0.046271 * SinD(2.0 * D - Mp - F)
			+ 0.032573 * SinD(2.0 * D + F)
			+ 0.017198 * SinD(2.0 * Mp + F)
			+ 0.009266 * SinD(2.0 * D + Mp - F)
			+ 0.008822 * SinD(2.0 * Mp - F)
			+ 0.008216 * SinD(2.0 * D - M - F)
			+ 0.004324 * SinD(2.0 * D - 2.0 * Mp - F);

		// --- Distance: the 16 largest terms of Meeus table 47.A's cosine column (km). ---
		const double Distance =
			  385000.56
			- 20905.355 * CosD(Mp)
			-  3699.111 * CosD(2.0 * D - Mp)
			-  2955.968 * CosD(2.0 * D)
			-   569.925 * CosD(2.0 * Mp)
			+    48.888 * CosD(M)
			-     3.149 * CosD(2.0 * F)
			+   246.158 * CosD(2.0 * D - 2.0 * Mp)
			-   152.138 * CosD(2.0 * D - M - Mp)
			-   170.733 * CosD(2.0 * D + Mp)
			-   204.586 * CosD(2.0 * D - M)
			-   129.620 * CosD(M - Mp)
			+   108.743 * CosD(D)
			+   104.755 * CosD(M + Mp)
			+    79.661 * CosD(Mp - 2.0 * F)
			+    48.888 * CosD(2.0 * D - 3.0 * Mp);

		OutLongitudeDeg = Wrap360(Lp + DeltaLongitude);
		OutLatitudeDeg = Latitude;
		OutDistanceKm = Distance;
	}

	double GreenwichMeanSiderealTimeDegrees(double JulianDay)
	{
		// Meeus 12.4, valid for any instant (not just 0h UT).
		const double T = ArchSolarMath::ToJulianCentury(JulianDay);
		const double Theta = 280.46061837
			+ 360.98564736629 * (JulianDay - ArchSolarMath::JulianDayJ2000)
			+ 0.000387933 * T * T
			- (T * T * T) / 38710000.0;

		return Detail::Wrap360(Theta);
	}

	EArchMoonPhase ClassifyMoonPhase(double AgeDays)
	{
		// Eight buckets of 1/8 synodic month, centred on the named instants: the New Moon
		// bucket therefore straddles the age-0 wrap, which is why we add the half-bucket
		// before flooring rather than after.
		const double Fraction = AgeDays / ArchSolarMath::SynodicMonthDays;
		int32 Bucket = FMath::FloorToInt32(Fraction * 8.0 + 0.5) % 8;
		if (Bucket < 0)
		{
			Bucket += 8;
		}

		static const EArchMoonPhase Phases[8] =
		{
			EArchMoonPhase::NewMoon,
			EArchMoonPhase::WaxingCrescent,
			EArchMoonPhase::FirstQuarter,
			EArchMoonPhase::WaxingGibbous,
			EArchMoonPhase::FullMoon,
			EArchMoonPhase::WaningGibbous,
			EArchMoonPhase::LastQuarter,
			EArchMoonPhase::WaningCrescent
		};

		return Phases[Bucket];
	}

	FText GetMoonPhaseDisplayName(EArchMoonPhase Phase)
	{
		switch (Phase)
		{
		case EArchMoonPhase::NewMoon:        return LOCTEXT("MoonPhase_New", "New Moon");
		case EArchMoonPhase::WaxingCrescent: return LOCTEXT("MoonPhase_WaxingCrescent", "Waxing Crescent");
		case EArchMoonPhase::FirstQuarter:   return LOCTEXT("MoonPhase_FirstQuarter", "First Quarter");
		case EArchMoonPhase::WaxingGibbous:  return LOCTEXT("MoonPhase_WaxingGibbous", "Waxing Gibbous");
		case EArchMoonPhase::FullMoon:       return LOCTEXT("MoonPhase_Full", "Full Moon");
		case EArchMoonPhase::WaningGibbous:  return LOCTEXT("MoonPhase_WaningGibbous", "Waning Gibbous");
		case EArchMoonPhase::LastQuarter:    return LOCTEXT("MoonPhase_LastQuarter", "Last Quarter");
		case EArchMoonPhase::WaningCrescent: return LOCTEXT("MoonPhase_WaningCrescent", "Waning Crescent");
		default:                             return LOCTEXT("MoonPhase_Unknown", "Unknown");
		}
	}

	FArchLunarPosition CalculateMoonPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime)
	{
		SCOPE_CYCLE_COUNTER(STAT_ArchSky_LunarMath);

		using namespace Detail;

		FArchLunarPosition Result;

		const double UtcOffset = UtcOffsetHours(Location, LocalTime);
		const FDateTime UtcTime = LocalTime - FTimespan::FromHours(UtcOffset);
		const double JD = ArchSolarMath::ToJulianDay(UtcTime);
		const double T = ArchSolarMath::ToJulianCentury(JD);

		// --- Moon, in ecliptic coordinates ---
		double MoonLongitude = 0.0;
		double MoonLatitude = 0.0;
		double MoonDistanceKm = MeanDistanceKm;
		CalculateGeocentricEcliptic(JD, MoonLongitude, MoonLatitude, MoonDistanceKm);

		// --- Sun, reusing the solar pipeline so the two bodies can never disagree ---
		const double SunApparentLongitude = ArchSolarMath::SunApparentLongitude(T,
			ArchSolarMath::SunTrueLongitude(
				ArchSolarMath::GeomMeanLongitudeSun(T),
				ArchSolarMath::SunEquationOfCenter(T, ArchSolarMath::GeomMeanAnomalySun(T))));

		// Sun-Earth distance from the radius-vector formula, in km. Needed for the phase
		// angle, which is a triangle solve and therefore genuinely needs both distances.
		const double SunMeanAnomaly = ArchSolarMath::GeomMeanAnomalySun(T);
		const double Eccentricity = ArchSolarMath::EccentricityEarthOrbit(T);
		const double TrueAnomaly = SunMeanAnomaly + ArchSolarMath::SunEquationOfCenter(T, SunMeanAnomaly);
		const double SunRadiusVectorAu = (1.000001018 * (1.0 - Eccentricity * Eccentricity))
			/ (1.0 + Eccentricity * CosD(TrueAnomaly));
		constexpr double AstronomicalUnitKm = 149597870.7;
		const double SunDistanceKm = SunRadiusVectorAu * AstronomicalUnitKm;

		// --- Ecliptic -> equatorial ---
		const double Obliquity = ArchSolarMath::ObliquityCorrection(T, ArchSolarMath::MeanObliquityOfEcliptic(T));

		const double SinLon = SinD(MoonLongitude);
		const double CosLon = CosD(MoonLongitude);
		const double SinLat = SinD(MoonLatitude);
		const double CosLat = CosD(MoonLatitude);
		const double SinObl = SinD(Obliquity);
		const double CosObl = CosD(Obliquity);

		const double RightAscension = Wrap360(Deg(FMath::Atan2(
			SinLon * CosObl - (SinLat / CosLat) * SinObl,
			CosLon)));
		const double Declination = Deg(FMath::Asin(FMath::Clamp(
			SinLat * CosObl + CosLat * SinObl * SinLon, -1.0, 1.0)));

		// --- Equatorial -> horizontal ---
		const double LocalSiderealTime = Wrap360(GreenwichMeanSiderealTimeDegrees(JD) + Location.LongitudeDegrees);
		double HourAngle = Wrap360(LocalSiderealTime - RightAscension);
		if (HourAngle > 180.0)
		{
			HourAngle -= 360.0;
		}

		const double LatRad = Rad(Location.LatitudeDegrees);
		const double DeclRad = Rad(Declination);
		const double HaRad = Rad(HourAngle);

		const double SinAltitude = FMath::Sin(LatRad) * FMath::Sin(DeclRad)
			+ FMath::Cos(LatRad) * FMath::Cos(DeclRad) * FMath::Cos(HaRad);
		const double TrueAltitude = Deg(FMath::Asin(FMath::Clamp(SinAltitude, -1.0, 1.0)));

		// Meeus 13.5. atan2 gives azimuth measured from SOUTH westward, so add 180 to get
		// the from-north-clockwise convention the rest of this plugin uses.
		const double AzimuthFromSouth = Deg(FMath::Atan2(
			FMath::Sin(HaRad),
			FMath::Cos(HaRad) * FMath::Sin(LatRad) - FMath::Tan(DeclRad) * FMath::Cos(LatRad)));
		const double Azimuth = Wrap360(AzimuthFromSouth + 180.0);

		// --- Phase ---
		// Geocentric elongation of the moon from the sun (Meeus 48.2).
		const double Elongation = Deg(FMath::Acos(FMath::Clamp(
			CosLat * CosD(MoonLongitude - SunApparentLongitude), -1.0, 1.0)));

		// Phase angle: solve the Sun-Moon-Earth triangle (Meeus 48.3).
		const double PhaseAngle = Deg(FMath::Atan2(
			SunDistanceKm * FMath::Sin(Rad(Elongation)),
			MoonDistanceKm - SunDistanceKm * FMath::Cos(Rad(Elongation))));

		// Illuminated fraction (Meeus 48.1). 0 at new, 1 at full.
		const double IlluminatedFraction = FMath::Clamp((1.0 + CosD(PhaseAngle)) * 0.5, 0.0, 1.0);

		// Age: the phase angle alone cannot distinguish waxing from waning, so we use the
		// signed difference in ecliptic longitude, which sweeps a clean 0 -> 360 per month.
		const double LongitudeElongation = Wrap360(MoonLongitude - SunApparentLongitude);
		const double AgeDays = (LongitudeElongation / 360.0) * ArchSolarMath::SynodicMonthDays;

		// Position angle of the bright limb (Meeus 48.5). Uses the SUN's equatorial
		// coordinates, so derive those from its apparent longitude (ecliptic latitude ~0).
		const double SunRightAscension = Wrap360(Deg(FMath::Atan2(SinD(SunApparentLongitude) * CosObl,
			CosD(SunApparentLongitude))));
		const double SunDeclination = Deg(FMath::Asin(FMath::Clamp(SinObl * SinD(SunApparentLongitude), -1.0, 1.0)));

		const double DeltaRa = Rad(SunRightAscension - RightAscension);
		const double BrightLimbAngle = Wrap360(Deg(FMath::Atan2(
			FMath::Cos(Rad(SunDeclination)) * FMath::Sin(DeltaRa),
			FMath::Sin(Rad(SunDeclination)) * FMath::Cos(DeclRad)
				- FMath::Cos(Rad(SunDeclination)) * FMath::Sin(DeclRad) * FMath::Cos(DeltaRa))));

		Result.AzimuthDegrees = Azimuth;
		Result.TrueAltitudeDegrees = TrueAltitude;
		Result.AltitudeDegrees = TrueAltitude + ArchSolarMath::ApproxAtmosphericRefraction(TrueAltitude);
		Result.IlluminatedFraction = IlluminatedFraction;
		Result.PhaseAngleDegrees = PhaseAngle;
		Result.AgeDays = AgeDays;
		Result.Phase = ClassifyMoonPhase(AgeDays);
		Result.DistanceKm = MoonDistanceKm;
		Result.RelativeDiscScale = (MoonDistanceKm > UE_DOUBLE_KINDA_SMALL_NUMBER)
			? MeanDistanceKm / MoonDistanceKm
			: 1.0;
		Result.BrightLimbAngleDegrees = BrightLimbAngle;
		Result.EclipticLongitudeDegrees = MoonLongitude;
		Result.EclipticLatitudeDegrees = MoonLatitude;

		// The moon's own semi-diameter is ~0.259 deg, so its upper limb clears the horizon
		// a shade earlier than the sun's. We use the same refracted threshold for both
		// because the difference (~0.5 arcmin) is far below our stated accuracy.
		const double HorizonThreshold = -0.833 - ArchSolarMath::HorizonDipDegrees(Location.ElevationMeters);
		Result.bIsAboveHorizon = TrueAltitude > HorizonThreshold;

		return Result;
	}
}

FArchLunarPosition UArchMoonMathLibrary::CalculateMoonPosition(const FArchGeoLocation& Location, const FDateTime& LocalTime)
{
	return ArchMoonMath::CalculateMoonPosition(Location, LocalTime);
}

FText UArchMoonMathLibrary::GetMoonPhaseDisplayName(EArchMoonPhase Phase)
{
	return ArchMoonMath::GetMoonPhaseDisplayName(Phase);
}

#undef LOCTEXT_NAMESPACE
