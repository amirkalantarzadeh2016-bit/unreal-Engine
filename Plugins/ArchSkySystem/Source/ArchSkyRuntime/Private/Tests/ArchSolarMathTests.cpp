// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/ArchSolarMath.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Reference values below are the standard textbook results, cross-checked against the
 * NOAA Solar Calculator (gml.noaa.gov/grad/solcalc/). The tolerance is +/- 0.5 degrees,
 * which is roughly the angular diameter of the sun itself.
 *
 * Run with:  Automation RunTests ArchSky.Solar
 */
namespace ArchSolarTestUtils
{
	static constexpr double AngleToleranceDegrees = 0.5;

	/** Tehran, Iran. UTC+3:30, no DST since 2022. Elevation zeroed so the horizon dip
	 *  does not perturb the noon-altitude comparisons. */
	static FArchGeoLocation Tehran()
	{
		return FArchGeoLocation(35.6892, 51.3890, 3.5f, 0.f);
	}

	/** Null Island - equator on the prime meridian, UTC. */
	static FArchGeoLocation Equator()
	{
		return FArchGeoLocation(0.0, 0.0, 0.f, 0.f);
	}

	/** Tromso, Norway - inside the Arctic Circle, so it has a genuine polar day. */
	static FArchGeoLocation Tromso()
	{
		return FArchGeoLocation(69.6492, 18.9553, 1.f, 0.f);
	}

	/** Converts local decimal hours into an FDateTime on the given calendar day. */
	static FDateTime AtHours(int32 Year, int32 Month, int32 Day, double Hours)
	{
		return FDateTime(Year, Month, Day) + FTimespan::FromHours(Hours);
	}

	/** Solar altitude at the exact instant of solar noon on a given day. */
	static double NoonAltitude(const FArchGeoLocation& Location, int32 Year, int32 Month, int32 Day)
	{
		const FArchSolarDayInfo DayInfo = ArchSolarMath::CalculateSolarDayInfo(Location, FDateTime(Year, Month, Day));
		const FArchSolarPosition Position = ArchSolarMath::CalculateSolarPosition(
			Location, AtHours(Year, Month, Day, DayInfo.SolarNoonHours));
		return Position.TrueAltitudeDegrees;
	}
}

// -----------------------------------------------------------------------------------------
// Tehran, summer solstice
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSolarTehranSummerSolsticeTest,
	"ArchSky.Solar.Tehran.SummerSolsticeNoonAltitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSolarTehranSummerSolsticeTest::RunTest(const FString& Parameters)
{
	using namespace ArchSolarTestUtils;

	// 90 - |latitude - declination| = 90 - |35.689 - 23.44| = 77.75 deg.
	const double Altitude = NoonAltitude(Tehran(), 2026, 6, 21);

	TestEqual(TEXT("Tehran summer solstice solar-noon altitude (degrees)"),
		Altitude, 77.8, AngleToleranceDegrees);

	return true;
}

// -----------------------------------------------------------------------------------------
// Tehran, winter solstice - the date every shadow study is dimensioned from
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSolarTehranWinterSolsticeTest,
	"ArchSky.Solar.Tehran.WinterSolsticeNoonAltitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSolarTehranWinterSolsticeTest::RunTest(const FString& Parameters)
{
	using namespace ArchSolarTestUtils;

	// 90 - |35.689 - (-23.44)| = 30.87 deg.
	const double Altitude = NoonAltitude(Tehran(), 2026, 12, 21);

	TestEqual(TEXT("Tehran winter solstice solar-noon altitude (degrees)"),
		Altitude, 30.9, AngleToleranceDegrees);

	// A shadow at 30.9 deg is 1.67x the height of its caster - the number an architect
	// uses to size a setback. Guard the helper against a sign or unit slip.
	const double ShadowMultiplier = ArchSolarMath::ShadowLengthMultiplier(Altitude);
	TestEqual(TEXT("Winter solstice shadow-length multiplier"), ShadowMultiplier, 1.671, 0.05);

	return true;
}

// -----------------------------------------------------------------------------------------
// Equator at equinox - the sun passes essentially through the zenith
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSolarEquatorEquinoxTest,
	"ArchSky.Solar.Equator.EquinoxNoonAltitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSolarEquatorEquinoxTest::RunTest(const FString& Parameters)
{
	using namespace ArchSolarTestUtils;

	const double Altitude = NoonAltitude(Equator(), 2026, 3, 20);

	TestEqual(TEXT("Equator equinox solar-noon altitude (degrees)"),
		Altitude, 90.0, AngleToleranceDegrees);

	// Declination should be within a fraction of a degree of zero at the equinox.
	const FArchSolarPosition Position = ArchSolarMath::CalculateSolarPosition(
		Equator(), AtHours(2026, 3, 20, 12.0));
	TestEqual(TEXT("Equinox solar declination (degrees)"), Position.DeclinationDegrees, 0.0, 0.5);

	// Day length at the equator is ~12 h all year (a few minutes over, from refraction).
	const FArchSolarDayInfo DayInfo = ArchSolarMath::CalculateSolarDayInfo(Equator(), FDateTime(2026, 3, 20));
	TestEqual(TEXT("Equator equinox day length (hours)"), static_cast<double>(DayInfo.DayLengthHours), 12.1, 0.2);

	return true;
}

// -----------------------------------------------------------------------------------------
// Polar day
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSolarPolarDayTest,
	"ArchSky.Solar.Tromso.PolarDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSolarPolarDayTest::RunTest(const FString& Parameters)
{
	using namespace ArchSolarTestUtils;

	const FArchSolarDayInfo Summer = ArchSolarMath::CalculateSolarDayInfo(Tromso(), FDateTime(2026, 6, 21));

	TestTrue(TEXT("Tromso on 21 June is a polar day"), Summer.bPolarDay);
	TestFalse(TEXT("Tromso on 21 June is not a polar night"), Summer.bPolarNight);
	TestEqual(TEXT("Polar day reports 24 h of daylight"), static_cast<double>(Summer.DayLengthHours), 24.0, 0.001);

	// The complementary case: the same latitude in midwinter never sees the sun.
	const FArchSolarDayInfo Winter = ArchSolarMath::CalculateSolarDayInfo(Tromso(), FDateTime(2026, 12, 21));
	TestTrue(TEXT("Tromso on 21 December is a polar night"), Winter.bPolarNight);
	TestFalse(TEXT("Tromso on 21 December is not a polar day"), Winter.bPolarDay);
	TestEqual(TEXT("Polar night reports 0 h of daylight"), static_cast<double>(Winter.DayLengthHours), 0.0, 0.001);

	// And the sun must actually stay up all night on the polar day: sample midnight.
	const FArchSolarPosition Midnight = ArchSolarMath::CalculateSolarPosition(
		Tromso(), AtHours(2026, 6, 21, 0.0));
	TestTrue(TEXT("Tromso midnight sun is above the horizon"), Midnight.TrueAltitudeDegrees > 0.0);

	return true;
}

// -----------------------------------------------------------------------------------------
// Sunrise / sunset symmetry about solar noon
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSolarNoonSymmetryTest,
	"ArchSky.Solar.SunriseSunsetSymmetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSolarNoonSymmetryTest::RunTest(const FString& Parameters)
{
	using namespace ArchSolarTestUtils;

	// Sunrise and sunset are symmetric about solar noon to within the amount the
	// equation of time drifts across half a day - a couple of minutes at most.
	static constexpr double SymmetryToleranceMinutes = 2.0;

	const int32 SampleMonths[] = { 1, 3, 6, 9, 12 };
	for (const int32 Month : SampleMonths)
	{
		const FArchSolarDayInfo DayInfo = ArchSolarMath::CalculateSolarDayInfo(Tehran(), FDateTime(2026, Month, 15));

		if (!TestFalse(FString::Printf(TEXT("Month %d in Tehran is a normal day"), Month),
			DayInfo.bPolarDay || DayInfo.bPolarNight))
		{
			continue;
		}

		const double MorningMinutes = (DayInfo.SolarNoonHours - DayInfo.SunriseHours) * 60.0;
		const double EveningMinutes = (DayInfo.SunsetHours - DayInfo.SolarNoonHours) * 60.0;

		TestEqual(FString::Printf(TEXT("Month %d: morning and evening half-days match (minutes)"), Month),
			MorningMinutes, EveningMinutes, SymmetryToleranceMinutes);

		TestTrue(FString::Printf(TEXT("Month %d: sunrise precedes solar noon"), Month),
			DayInfo.SunriseHours < DayInfo.SolarNoonHours);
		TestTrue(FString::Printf(TEXT("Month %d: solar noon precedes sunset"), Month),
			DayInfo.SolarNoonHours < DayInfo.SunsetHours);

		// Civil twilight must bracket sunrise and sunset.
		TestTrue(FString::Printf(TEXT("Month %d: civil dawn precedes sunrise"), Month),
			DayInfo.CivilTwilightStartHours < DayInfo.SunriseHours);
		TestTrue(FString::Printf(TEXT("Month %d: civil dusk follows sunset"), Month),
			DayInfo.CivilTwilightEndHours > DayInfo.SunsetHours);
	}

	return true;
}

// -----------------------------------------------------------------------------------------
// Sun crosses the horizon exactly at the reported sunrise / sunset
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSolarHorizonCrossingTest,
	"ArchSky.Solar.HorizonCrossingConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSolarHorizonCrossingTest::RunTest(const FString& Parameters)
{
	using namespace ArchSolarTestUtils;

	// CalculateSolarDayInfo and CalculateSolarPosition are separate code paths; they must
	// agree. At the reported sunrise the geometric altitude must be -0.833 deg.
	const FArchGeoLocation Location = Tehran();
	const FArchSolarDayInfo DayInfo = ArchSolarMath::CalculateSolarDayInfo(Location, FDateTime(2026, 9, 15));

	const FArchSolarPosition AtSunrise = ArchSolarMath::CalculateSolarPosition(
		Location, AtHours(2026, 9, 15, DayInfo.SunriseHours));
	const FArchSolarPosition AtSunset = ArchSolarMath::CalculateSolarPosition(
		Location, AtHours(2026, 9, 15, DayInfo.SunsetHours));

	TestEqual(TEXT("Geometric altitude at reported sunrise (degrees)"),
		AtSunrise.TrueAltitudeDegrees, -0.833, 0.1);
	TestEqual(TEXT("Geometric altitude at reported sunset (degrees)"),
		AtSunset.TrueAltitudeDegrees, -0.833, 0.1);

	// The sun rises in the east and sets in the west, near the equinox close to 90/270.
	TestTrue(TEXT("Sunrise azimuth is easterly"), AtSunrise.AzimuthDegrees > 60.0 && AtSunrise.AzimuthDegrees < 120.0);
	TestTrue(TEXT("Sunset azimuth is westerly"), AtSunset.AzimuthDegrees > 240.0 && AtSunset.AzimuthDegrees < 300.0);

	// At solar noon in the northern hemisphere the sun is due south.
	const FArchSolarPosition AtNoon = ArchSolarMath::CalculateSolarPosition(
		Location, AtHours(2026, 9, 15, DayInfo.SolarNoonHours));
	TestEqual(TEXT("Solar-noon azimuth is due south (degrees)"), AtNoon.AzimuthDegrees, 180.0, 0.5);

	return true;
}

// -----------------------------------------------------------------------------------------
// Individual NOAA steps
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSolarPipelineStepsTest,
	"ArchSky.Solar.PipelineSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSolarPipelineStepsTest::RunTest(const FString& Parameters)
{
	// J2000.0 epoch: 2000-01-01 12:00 UTC is Julian Day 2451545.0 by definition.
	const double JD = ArchSolarMath::ToJulianDay(FDateTime(2000, 1, 1, 12, 0, 0));
	TestEqual(TEXT("Julian Day at the J2000.0 epoch"), JD, 2451545.0, 1.e-6);

	// ...and therefore Julian century zero.
	TestEqual(TEXT("Julian century at J2000.0"), ArchSolarMath::ToJulianCentury(JD), 0.0, 1.e-12);

	// A known reference: 2026-01-01 00:00 UTC.
	const double JD2026 = ArchSolarMath::ToJulianDay(FDateTime(2026, 1, 1, 0, 0, 0));
	TestEqual(TEXT("Julian Day for 2026-01-01T00:00Z"), JD2026, 2461041.5, 1.e-6);

	const double T = ArchSolarMath::ToJulianCentury(JD);

	// Orbital constants at the epoch, from Meeus / NOAA.
	TestEqual(TEXT("Earth orbital eccentricity"), ArchSolarMath::EccentricityEarthOrbit(T), 0.016709, 1.e-5);
	TestEqual(TEXT("Mean obliquity of the ecliptic (degrees)"),
		ArchSolarMath::MeanObliquityOfEcliptic(T), 23.4393, 1.e-3);

	// Mean longitude must stay wrapped.
	for (int32 Year = 1990; Year <= 2060; Year += 7)
	{
		const double Century = ArchSolarMath::ToJulianCentury(ArchSolarMath::ToJulianDay(FDateTime(Year, 7, 1)));
		const double L0 = ArchSolarMath::GeomMeanLongitudeSun(Century);
		TestTrue(FString::Printf(TEXT("Mean longitude wrapped into [0,360) for %d"), Year),
			L0 >= 0.0 && L0 < 360.0);
	}

	// The equation of time peaks near -14.2 min in mid-February and +16.4 min in early November.
	const double EotFeb = ArchSolarMath::EquationOfTime(
		ArchSolarMath::ToJulianCentury(ArchSolarMath::ToJulianDay(FDateTime(2026, 2, 11, 12, 0, 0))));
	const double EotNov = ArchSolarMath::EquationOfTime(
		ArchSolarMath::ToJulianCentury(ArchSolarMath::ToJulianDay(FDateTime(2026, 11, 3, 12, 0, 0))));

	TestEqual(TEXT("Equation of time minimum in February (minutes)"), EotFeb, -14.2, 0.6);
	TestEqual(TEXT("Equation of time maximum in November (minutes)"), EotNov, 16.4, 0.6);

	// Refraction: ~0.567 deg at the horizon, ~0 high up, monotonically decreasing.
	TestEqual(TEXT("Refraction at the horizon (degrees)"),
		ArchSolarMath::ApproxAtmosphericRefraction(0.0), 0.482, 0.05);
	TestEqual(TEXT("Refraction above 85 degrees is zero"),
		ArchSolarMath::ApproxAtmosphericRefraction(88.0), 0.0, 1.e-9);
	TestTrue(TEXT("Refraction decreases with altitude"),
		ArchSolarMath::ApproxAtmosphericRefraction(2.0) > ArchSolarMath::ApproxAtmosphericRefraction(20.0));

	// Horizon dip from elevation: Tehran at 1200 m sees ~1.11 deg further round the curve.
	TestEqual(TEXT("Horizon dip at 1200 m (degrees)"),
		ArchSolarMath::HorizonDipDegrees(1200.0), 1.113, 0.02);
	TestEqual(TEXT("Horizon dip at sea level is zero"),
		ArchSolarMath::HorizonDipDegrees(0.0), 0.0, 1.e-12);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
