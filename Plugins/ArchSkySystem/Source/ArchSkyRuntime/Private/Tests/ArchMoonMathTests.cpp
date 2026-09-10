// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/ArchMoonMath.h"

#include "Math/ArchSolarMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Lunar tests. The two syzygy anchors below are standard published instants; at a new
 * moon the moon's apparent ecliptic longitude equals the sun's, and at a full moon it is
 * exactly 180 degrees away. Those are definitional, so they are the strongest check
 * available without shipping an ephemeris table.
 *
 * Run with:  Automation RunTests ArchSky.Lunar
 */
namespace ArchLunarTestUtils
{
	/** Signed difference of two angles, wrapped into (-180, +180]. */
	static double AngleDelta(double A, double B)
	{
		double Delta = FMath::Fmod(A - B + 180.0, 360.0);
		if (Delta < 0.0)
		{
			Delta += 360.0;
		}
		return Delta - 180.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchLunarSyzygyTest,
	"ArchSky.Lunar.SyzygyLongitudes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchLunarSyzygyTest::RunTest(const FString& Parameters)
{
	using namespace ArchLunarTestUtils;

	auto SunApparentLongitudeAt = [](double JulianDay) -> double
	{
		const double T = ArchSolarMath::ToJulianCentury(JulianDay);
		return ArchSolarMath::SunApparentLongitude(T,
			ArchSolarMath::SunTrueLongitude(
				ArchSolarMath::GeomMeanLongitudeSun(T),
				ArchSolarMath::SunEquationOfCenter(T, ArchSolarMath::GeomMeanAnomalySun(T))));
	};

	// New moon of 2000-01-06 at 18:14 UTC.
	{
		const double JD = ArchSolarMath::ToJulianDay(FDateTime(2000, 1, 6, 18, 14, 0));
		double Longitude = 0.0, Latitude = 0.0, Distance = 0.0;
		ArchMoonMath::CalculateGeocentricEcliptic(JD, Longitude, Latitude, Distance);

		TestEqual(TEXT("New moon: lunar and solar ecliptic longitudes coincide (degrees)"),
			AngleDelta(Longitude, SunApparentLongitudeAt(JD)), 0.0, 0.5);
	}

	// Full moon of 2000-01-21 at 04:40 UTC.
	{
		const double JD = ArchSolarMath::ToJulianDay(FDateTime(2000, 1, 21, 4, 40, 0));
		double Longitude = 0.0, Latitude = 0.0, Distance = 0.0;
		ArchMoonMath::CalculateGeocentricEcliptic(JD, Longitude, Latitude, Distance);

		TestEqual(TEXT("Full moon: lunar longitude is 180 degrees from the sun"),
			FMath::Abs(AngleDelta(Longitude, SunApparentLongitudeAt(JD))), 180.0, 0.5);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchLunarPhaseTest,
	"ArchSky.Lunar.PhaseAndIllumination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchLunarPhaseTest::RunTest(const FString& Parameters)
{
	const FArchGeoLocation Tehran(35.6892, 51.3890, 3.5f, 0.f);

	// At the reference new moon the disc is essentially unlit; at the full moon it is
	// essentially fully lit. These are the values the moon light intensity keys off.
	{
		const FArchLunarPosition New = ArchMoonMath::CalculateMoonPosition(Tehran, FDateTime(2000, 1, 6, 21, 44, 0));
		TestEqual(TEXT("Illuminated fraction at new moon"), New.IlluminatedFraction, 0.0, 0.02);
		TestEqual(TEXT("Phase enum at new moon"), static_cast<int32>(New.Phase), static_cast<int32>(EArchMoonPhase::NewMoon));

		const FArchLunarPosition Full = ArchMoonMath::CalculateMoonPosition(Tehran, FDateTime(2000, 1, 21, 8, 10, 0));
		TestEqual(TEXT("Illuminated fraction at full moon"), Full.IlluminatedFraction, 1.0, 0.02);
		TestEqual(TEXT("Phase enum at full moon"), static_cast<int32>(Full.Phase), static_cast<int32>(EArchMoonPhase::FullMoon));
	}

	// Invariants over a full year of daily samples.
	double MinDistance = TNumericLimits<double>::Max();
	double MaxDistance = TNumericLimits<double>::Lowest();

	for (int32 DayIndex = 0; DayIndex < 365; ++DayIndex)
	{
		const FDateTime Sample = FDateTime(2026, 1, 1, 21, 0, 0) + FTimespan::FromDays(DayIndex);
		const FArchLunarPosition Moon = ArchMoonMath::CalculateMoonPosition(Tehran, Sample);

		if (!TestTrue(TEXT("Illuminated fraction stays inside [0,1]"),
			Moon.IlluminatedFraction >= 0.0 && Moon.IlluminatedFraction <= 1.0))
		{
			return false;
		}
		if (!TestTrue(TEXT("Age stays inside one synodic month"),
			Moon.AgeDays >= 0.0 && Moon.AgeDays <= ArchSolarMath::SynodicMonthDays + 0.001))
		{
			return false;
		}
		if (!TestTrue(TEXT("Azimuth stays inside [0,360)"),
			Moon.AzimuthDegrees >= 0.0 && Moon.AzimuthDegrees < 360.0))
		{
			return false;
		}
		if (!TestTrue(TEXT("Altitude stays inside [-90,90]"),
			Moon.TrueAltitudeDegrees >= -90.0 && Moon.TrueAltitudeDegrees <= 90.0))
		{
			return false;
		}
		if (!TestTrue(TEXT("Ecliptic latitude stays inside the moon's +/-5.3 degree band"),
			FMath::Abs(Moon.EclipticLatitudeDegrees) <= 5.5))
		{
			return false;
		}

		// Illumination and phase angle must agree: k = (1 + cos(i)) / 2.
		const double Expected = (1.0 + FMath::Cos(FMath::DegreesToRadians(Moon.PhaseAngleDegrees))) * 0.5;
		if (!TestEqual(TEXT("Illumination matches the phase angle"), Moon.IlluminatedFraction, Expected, 1.e-9))
		{
			return false;
		}

		MinDistance = FMath::Min(MinDistance, Moon.DistanceKm);
		MaxDistance = FMath::Max(MaxDistance, Moon.DistanceKm);
	}

	// Perigee is ~356500 km and apogee ~406700 km; a year of samples must land inside a
	// slightly padded version of that envelope and must actually span most of it.
	TestTrue(TEXT("Minimum sampled distance is a plausible perigee"), MinDistance > 355000.0 && MinDistance < 372000.0);
	TestTrue(TEXT("Maximum sampled distance is a plausible apogee"), MaxDistance > 400000.0 && MaxDistance < 408000.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchLunarSynodicPeriodTest,
	"ArchSky.Lunar.SynodicPeriod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchLunarSynodicPeriodTest::RunTest(const FString& Parameters)
{
	// Walk a year at one-hour resolution, recording every instant at which the age wraps
	// back through zero (i.e. every new moon). The mean interval must be the synodic month.
	const FArchGeoLocation Reference(0.0, 0.0, 0.f, 0.f);

	TArray<double> NewMoonHours;
	double PreviousAge = ArchMoonMath::CalculateMoonPosition(Reference, FDateTime(2026, 1, 1)).AgeDays;

	for (int32 HourIndex = 1; HourIndex < 24 * 365; ++HourIndex)
	{
		const FDateTime Sample = FDateTime(2026, 1, 1) + FTimespan::FromHours(HourIndex);
		const double Age = ArchMoonMath::CalculateMoonPosition(Reference, Sample).AgeDays;

		if (Age < PreviousAge)
		{
			NewMoonHours.Add(static_cast<double>(HourIndex));
		}
		PreviousAge = Age;
	}

	if (!TestTrue(TEXT("A year contains 12 or 13 new moons"),
		NewMoonHours.Num() == 12 || NewMoonHours.Num() == 13))
	{
		return false;
	}

	double TotalDays = 0.0;
	for (int32 Index = 1; Index < NewMoonHours.Num(); ++Index)
	{
		TotalDays += (NewMoonHours[Index] - NewMoonHours[Index - 1]) / 24.0;
	}
	const double MeanInterval = TotalDays / static_cast<double>(NewMoonHours.Num() - 1);

	TestEqual(TEXT("Mean interval between new moons (days)"),
		MeanInterval, ArchSolarMath::SynodicMonthDays, 0.15);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchLunarPhaseClassificationTest,
	"ArchSky.Lunar.PhaseClassification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchLunarPhaseClassificationTest::RunTest(const FString& Parameters)
{
	const double Synodic = ArchSolarMath::SynodicMonthDays;

	TestEqual(TEXT("Age 0 is a new moon"),
		static_cast<int32>(ArchMoonMath::ClassifyMoonPhase(0.0)), static_cast<int32>(EArchMoonPhase::NewMoon));
	TestEqual(TEXT("Quarter of a month is the first quarter"),
		static_cast<int32>(ArchMoonMath::ClassifyMoonPhase(Synodic * 0.25)), static_cast<int32>(EArchMoonPhase::FirstQuarter));
	TestEqual(TEXT("Half a month is a full moon"),
		static_cast<int32>(ArchMoonMath::ClassifyMoonPhase(Synodic * 0.5)), static_cast<int32>(EArchMoonPhase::FullMoon));
	TestEqual(TEXT("Three quarters of a month is the last quarter"),
		static_cast<int32>(ArchMoonMath::ClassifyMoonPhase(Synodic * 0.75)), static_cast<int32>(EArchMoonPhase::LastQuarter));
	TestEqual(TEXT("The end of the month wraps back to a new moon"),
		static_cast<int32>(ArchMoonMath::ClassifyMoonPhase(Synodic * 0.999)), static_cast<int32>(EArchMoonPhase::NewMoon));

	// Every phase name must be non-empty so the UI never shows a blank label.
	for (int32 PhaseIndex = 0; PhaseIndex <= static_cast<int32>(EArchMoonPhase::WaningCrescent); ++PhaseIndex)
	{
		const FText Name = ArchMoonMath::GetMoonPhaseDisplayName(static_cast<EArchMoonPhase>(PhaseIndex));
		TestFalse(FString::Printf(TEXT("Phase %d has a display name"), PhaseIndex), Name.IsEmpty());
	}

	return true;
}

// -----------------------------------------------------------------------------------------
// The coordinate conversion - the single most bug-prone function in the plugin
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchCoordinateConversionTest,
	"ArchSky.Coordinates.LightRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchCoordinateConversionTest::RunTest(const FString& Parameters)
{
	static constexpr double Tolerance = 1.e-4;

	// --- Direction-to-body: the axis convention itself ---
	{
		// Due north on the horizon -> +X.
		const FVector North = ArchSolarMath::SolarToUnrealDirectionToBody(0.0, 0.0, 0.f);
		TestTrue(TEXT("Azimuth 0 points along +X (true north)"), North.Equals(FVector(1, 0, 0), Tolerance));

		// Due east on the horizon -> +Y.
		const FVector East = ArchSolarMath::SolarToUnrealDirectionToBody(90.0, 0.0, 0.f);
		TestTrue(TEXT("Azimuth 90 points along +Y (east)"), East.Equals(FVector(0, 1, 0), Tolerance));

		// Due south -> -X, due west -> -Y.
		TestTrue(TEXT("Azimuth 180 points along -X (south)"),
			ArchSolarMath::SolarToUnrealDirectionToBody(180.0, 0.0, 0.f).Equals(FVector(-1, 0, 0), Tolerance));
		TestTrue(TEXT("Azimuth 270 points along -Y (west)"),
			ArchSolarMath::SolarToUnrealDirectionToBody(270.0, 0.0, 0.f).Equals(FVector(0, -1, 0), Tolerance));

		// Zenith -> +Z, regardless of azimuth.
		TestTrue(TEXT("Altitude 90 points along +Z (up)"),
			ArchSolarMath::SolarToUnrealDirectionToBody(137.0, 90.0, 0.f).Equals(FVector(0, 0, 1), Tolerance));
	}

	// --- Light rotation: forward must be the direction light TRAVELS ---
	{
		// Sun due east on the horizon: light travels due west.
		const FRotator Rotation = ArchSolarMath::SolarToUnrealLightRotation(90.0, 0.0, 0.f);
		const FVector Forward = Rotation.Vector();
		TestTrue(TEXT("Sun in the east makes the light shine west"), Forward.Equals(FVector(0, -1, 0), Tolerance));

		// Sun overhead: light shines straight down.
		const FVector Overhead = ArchSolarMath::SolarToUnrealLightRotation(0.0, 90.0, 0.f).Vector();
		TestTrue(TEXT("Sun overhead makes the light shine straight down"), Overhead.Equals(FVector(0, 0, -1), Tolerance));

		// Pitch is the negated altitude.
		const FRotator Mid = ArchSolarMath::SolarToUnrealLightRotation(210.0, 42.5, 0.f);
		TestEqual(TEXT("Pitch is the negated altitude"), static_cast<double>(Mid.Pitch), -42.5, 1.e-3);
	}

	// --- The two functions must be exact negatives of each other, at every angle ---
	{
		for (double Azimuth = 0.0; Azimuth < 360.0; Azimuth += 17.0)
		{
			for (double Altitude = -80.0; Altitude <= 80.0; Altitude += 23.0)
			{
				for (float Offset : { 0.f, 37.f, -113.f, 180.f })
				{
					const FVector ToBody = ArchSolarMath::SolarToUnrealDirectionToBody(Azimuth, Altitude, Offset);
					const FVector LightForward = ArchSolarMath::SolarToUnrealLightRotation(Azimuth, Altitude, Offset).Vector();

					if (!TestTrue(FString::Printf(
							TEXT("Light forward is the negated direction-to-body (az %.0f alt %.0f offset %.0f)"),
							Azimuth, Altitude, Offset),
						LightForward.Equals(-ToBody, 1.e-3)))
					{
						return false;
					}
				}
			}
		}
	}

	// --- North offset semantics: it rotates the celestial sphere over the plan ---
	{
		// With true north at scene yaw 90, a sun due true-north appears along scene +Y.
		const FVector Rotated = ArchSolarMath::SolarToUnrealDirectionToBody(0.0, 0.0, 90.f);
		TestTrue(TEXT("North offset 90 puts true north along +Y"), Rotated.Equals(FVector(0, 1, 0), Tolerance));

		// A 360-degree offset is a no-op.
		TestTrue(TEXT("A 360 degree north offset changes nothing"),
			ArchSolarMath::SolarToUnrealDirectionToBody(55.0, 20.0, 360.f)
				.Equals(ArchSolarMath::SolarToUnrealDirectionToBody(55.0, 20.0, 0.f), Tolerance));

		// Offsetting north is identical to offsetting the azimuth.
		TestTrue(TEXT("Offsetting north equals offsetting the azimuth"),
			ArchSolarMath::SolarToUnrealDirectionToBody(55.0, 20.0, 30.f)
				.Equals(ArchSolarMath::SolarToUnrealDirectionToBody(85.0, 20.0, 0.f), Tolerance));
	}

	// --- Shadow length ---
	{
		TestEqual(TEXT("A 45 degree sun casts a shadow as long as the object is tall"),
			ArchSolarMath::ShadowLengthMultiplier(45.0), 1.0, 1.e-6);
		TestEqual(TEXT("A sun at the zenith casts no shadow"),
			ArchSolarMath::ShadowLengthMultiplier(90.0), 0.0, 1.e-6);
		TestEqual(TEXT("A sun on the horizon clamps to the maximum multiplier"),
			ArchSolarMath::ShadowLengthMultiplier(0.0, 100.0), 100.0, 1.e-6);
		TestEqual(TEXT("A sun below the horizon clamps to the maximum multiplier"),
			ArchSolarMath::ShadowLengthMultiplier(-10.0, 100.0), 100.0, 1.e-6);
		TestTrue(TEXT("Shadows lengthen as the sun drops"),
			ArchSolarMath::ShadowLengthMultiplier(20.0) > ArchSolarMath::ShadowLengthMultiplier(60.0));
	}

	// --- Time-phase classification ---
	{
		TestEqual(TEXT("High sun is Day"),
			static_cast<int32>(ArchSolarMath::ClassifyTimePhase(45.0, true)), static_cast<int32>(EArchTimePhase::Day));
		TestEqual(TEXT("Low rising sun is the morning golden hour"),
			static_cast<int32>(ArchSolarMath::ClassifyTimePhase(3.0, true)), static_cast<int32>(EArchTimePhase::GoldenHourMorning));
		TestEqual(TEXT("Low setting sun is the evening golden hour"),
			static_cast<int32>(ArchSolarMath::ClassifyTimePhase(3.0, false)), static_cast<int32>(EArchTimePhase::GoldenHourEvening));
		TestEqual(TEXT("Minus five degrees is civil twilight"),
			static_cast<int32>(ArchSolarMath::ClassifyTimePhase(-5.0, false)), static_cast<int32>(EArchTimePhase::CivilTwilight));
		TestEqual(TEXT("Minus thirty degrees is night"),
			static_cast<int32>(ArchSolarMath::ClassifyTimePhase(-30.0, false)), static_cast<int32>(EArchTimePhase::Night));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
