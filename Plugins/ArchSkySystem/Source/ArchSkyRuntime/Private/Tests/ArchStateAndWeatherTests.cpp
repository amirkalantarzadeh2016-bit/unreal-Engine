// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ArchSkyState.h"
#include "Data/ArchWeatherPreset.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the pure state and weather logic.
 *
 * These cover the three places where a silent defect would be hardest to notice by eye:
 * the weather blend (a wrong lerp just looks like slightly different weather), state
 * sanitisation (an out-of-range value that survives becomes an assert three layers later)
 * and the replication quantisation (a saturating value puts every client's sun somewhere
 * the server's is not).
 *
 * Run with:  Automation RunTests ArchSky.State
 */

// -----------------------------------------------------------------------------------------
// Weather blending
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchWeatherBlendTest,
	"ArchSky.State.WeatherBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchWeatherBlendTest::RunTest(const FString& Parameters)
{
	FArchWeatherParams A;
	A.CloudCoverage = 0.f;
	A.FogDensity = 0.01f;
	A.AerosolTurbidity = 2.f;
	A.SunIntensityMultiplier = 1.f;
	A.SkyLuminanceTint = FLinearColor(1.f, 0.f, 0.f, 1.f);
	A.FogInscatteringColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
	A.PrecipType = EArchPrecipType::None;
	A.CloudWindDirectionDeg = 350.f;

	FArchWeatherParams B;
	B.CloudCoverage = 1.f;
	B.FogDensity = 0.05f;
	B.AerosolTurbidity = 12.f;
	B.SunIntensityMultiplier = 0.2f;
	B.SkyLuminanceTint = FLinearColor(0.f, 1.f, 0.f, 1.f);
	B.FogInscatteringColor = FLinearColor(1.f, 1.f, 1.f, 1.f);
	B.PrecipType = EArchPrecipType::Snow;
	B.CloudWindDirectionDeg = 10.f;

	// --- The endpoints must be exact, not merely close. An architect who selects "Clear"
	//     must get the authored Clear, not something 0.001 away from it. ---
	{
		const FArchWeatherParams AtZero = FArchWeatherParams::Blend(A, B, 0.f);
		TestEqual(TEXT("Alpha 0 returns A exactly (coverage)"), AtZero.CloudCoverage, A.CloudCoverage);
		TestEqual(TEXT("Alpha 0 returns A exactly (turbidity)"), AtZero.AerosolTurbidity, A.AerosolTurbidity);
		TestEqual(TEXT("Alpha 0 keeps A's precipitation type"),
			static_cast<int32>(AtZero.PrecipType), static_cast<int32>(A.PrecipType));

		const FArchWeatherParams AtOne = FArchWeatherParams::Blend(A, B, 1.f);
		TestEqual(TEXT("Alpha 1 returns B exactly (coverage)"), AtOne.CloudCoverage, B.CloudCoverage);
		TestEqual(TEXT("Alpha 1 returns B exactly (turbidity)"), AtOne.AerosolTurbidity, B.AerosolTurbidity);
		TestEqual(TEXT("Alpha 1 keeps B's precipitation type"),
			static_cast<int32>(AtOne.PrecipType), static_cast<int32>(B.PrecipType));
	}

	// --- Alpha outside [0,1] must clamp, not extrapolate. ---
	{
		const FArchWeatherParams Below = FArchWeatherParams::Blend(A, B, -5.f);
		TestEqual(TEXT("Negative alpha clamps to A"), Below.CloudCoverage, A.CloudCoverage);

		const FArchWeatherParams Above = FArchWeatherParams::Blend(A, B, 5.f);
		TestEqual(TEXT("Alpha above one clamps to B"), Above.CloudCoverage, B.CloudCoverage);
	}

	// --- Continuous fields lerp linearly. ---
	{
		const FArchWeatherParams Mid = FArchWeatherParams::Blend(A, B, 0.5f);
		TestEqual(TEXT("Coverage lerps"), Mid.CloudCoverage, 0.5f, 1.e-5f);
		TestEqual(TEXT("Fog density lerps"), Mid.FogDensity, 0.03f, 1.e-5f);
		TestEqual(TEXT("Turbidity lerps"), Mid.AerosolTurbidity, 7.f, 1.e-4f);
		TestEqual(TEXT("Sun intensity multiplier lerps"), Mid.SunIntensityMultiplier, 0.6f, 1.e-5f);

		const FArchWeatherParams Quarter = FArchWeatherParams::Blend(A, B, 0.25f);
		TestEqual(TEXT("Coverage lerps at a quarter"), Quarter.CloudCoverage, 0.25f, 1.e-5f);
	}

	// --- Colours lerp componentwise in linear space. ---
	{
		const FArchWeatherParams Mid = FArchWeatherParams::Blend(A, B, 0.5f);
		TestEqual(TEXT("Sky tint R lerps"), Mid.SkyLuminanceTint.R, 0.5f, 1.e-5f);
		TestEqual(TEXT("Sky tint G lerps"), Mid.SkyLuminanceTint.G, 0.5f, 1.e-5f);
		TestEqual(TEXT("Fog colour lerps"), Mid.FogInscatteringColor.B, 0.5f, 1.e-5f);
	}

	// --- Discrete fields SELECT, they do not interpolate. Blending Rain to Snow must
	//     never pass through Hail, which is what lerping the underlying integer would do. ---
	{
		const FArchWeatherParams JustBefore = FArchWeatherParams::Blend(A, B, 0.49f);
		TestEqual(TEXT("Below the midpoint keeps A's precipitation type"),
			static_cast<int32>(JustBefore.PrecipType), static_cast<int32>(EArchPrecipType::None));

		const FArchWeatherParams JustAfter = FArchWeatherParams::Blend(A, B, 0.51f);
		TestEqual(TEXT("Above the midpoint takes B's precipitation type"),
			static_cast<int32>(JustAfter.PrecipType), static_cast<int32>(EArchPrecipType::Snow));

		// Sweeping the whole range must only ever produce one of the two endpoints' types.
		for (float Alpha = 0.f; Alpha <= 1.f; Alpha += 0.01f)
		{
			const FArchWeatherParams Step = FArchWeatherParams::Blend(A, B, Alpha);
			const bool bIsEndpointType = (Step.PrecipType == A.PrecipType) || (Step.PrecipType == B.PrecipType);

			if (!TestTrue(TEXT("A blend never invents a precipitation type neither end had"), bIsEndpointType))
			{
				return false;
			}
		}
	}

	// --- Bearings take the short way round. 350 to 10 must pass through 0, not 180. ---
	{
		const FArchWeatherParams Mid = FArchWeatherParams::Blend(A, B, 0.5f);
		TestEqual(TEXT("Wind bearing 350->10 blends through north"), Mid.CloudWindDirectionDeg, 0.f, 1.e-3f);

		FArchWeatherParams C = A;
		FArchWeatherParams D = B;
		C.CloudWindDirectionDeg = 20.f;
		D.CloudWindDirectionDeg = 40.f;
		TestEqual(TEXT("An ordinary bearing blend is the plain midpoint"),
			FArchWeatherParams::Blend(C, D, 0.5f).CloudWindDirectionDeg, 30.f, 1.e-3f);

		// Whatever the inputs, a blended bearing is always a legal compass bearing.
		for (float StartBearing = 0.f; StartBearing < 360.f; StartBearing += 47.f)
		{
			for (float EndBearing = 0.f; EndBearing < 360.f; EndBearing += 53.f)
			{
				FArchWeatherParams P = A;
				FArchWeatherParams Q = B;
				P.CloudWindDirectionDeg = StartBearing;
				Q.CloudWindDirectionDeg = EndBearing;

				const float Blended = FArchWeatherParams::Blend(P, Q, 0.5f).CloudWindDirectionDeg;
				if (!TestTrue(TEXT("A blended bearing stays inside [0,360)"), Blended >= 0.f && Blended < 360.f))
				{
					return false;
				}
			}
		}
	}

	// --- The change metric used by the Director's recapture throttle. ---
	{
		TestEqual(TEXT("A preset is identical to itself"), A.GetSignificantDelta(A), 0.f, 1.e-6f);
		TestTrue(TEXT("A preset reports itself as nearly equal to itself"), A.IsNearlyEqual(A));
		TestTrue(TEXT("Two very different presets report a large delta"), A.GetSignificantDelta(B) > 0.5f);
		TestFalse(TEXT("Two very different presets are not nearly equal"), A.IsNearlyEqual(B));

		// The metric is normalised, so a full-range move in any single field reads as ~1.
		FArchWeatherParams OnlyCoverage = A;
		OnlyCoverage.CloudCoverage = 1.f;
		TestEqual(TEXT("A full-range coverage change reads as one"),
			A.GetSignificantDelta(OnlyCoverage), 1.f, 1.e-4f);

		// The delta is symmetric - the throttle must not depend on which way weather moved.
		TestEqual(TEXT("The change metric is symmetric"),
			A.GetSignificantDelta(B), B.GetSignificantDelta(A), 1.e-6f);
	}

	return true;
}

// -----------------------------------------------------------------------------------------
// State sanitisation
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSkyStateSanitiseTest,
	"ArchSky.State.Sanitise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSkyStateSanitiseTest::RunTest(const FString& Parameters)
{
	// Sanitise is the plugin's whole defence against a bad value from a text box, a console
	// argument, a hand-edited JSON preset or a malformed packet. It must never assert and
	// must always leave the state legal.
	{
		FArchSkyState State;
		State.Year = 2026;             // not a leap year
		State.DayOfYear = 366;
		State.TimeOfDayHours = 25.5f;
		State.NorthOffsetDegrees = -30.f;
		State.Location.LatitudeDegrees = 200.0;
		State.Location.LongitudeDegrees = -400.0;
		State.Location.TimezoneOffsetHours = 99.f;
		State.WeatherBlendAlpha = 3.f;
		State.WeatherPresetB = NAME_None;

		State.Sanitise();

		TestEqual(TEXT("Day 366 clamps to 365 in a non-leap year"), State.DayOfYear, 365);
		TestEqual(TEXT("25.5 h wraps to 1.5 h"), State.TimeOfDayHours, 1.5f, 1.e-4f);
		TestEqual(TEXT("A negative north offset wraps into [0,360)"), State.NorthOffsetDegrees, 330.f, 1.e-3f);
		TestEqual(TEXT("Latitude clamps to 90"), State.Location.LatitudeDegrees, 90.0, 1.e-6);
		TestEqual(TEXT("Longitude clamps to -180"), State.Location.LongitudeDegrees, -180.0, 1.e-6);
		TestEqual(TEXT("Timezone clamps to +14"), State.Location.TimezoneOffsetHours, 14.f, 1.e-4f);

		// A blend with no destination is not a blend.
		TestEqual(TEXT("Blend alpha collapses when there is no B preset"), State.WeatherBlendAlpha, 0.f, 1.e-6f);
	}

	// A leap year keeps day 366.
	{
		FArchSkyState State;
		State.Year = 2024;
		State.DayOfYear = 366;
		State.Sanitise();
		TestEqual(TEXT("Day 366 survives in a leap year"), State.DayOfYear, 366);
	}

	// An alpha with a real destination is kept, merely clamped.
	{
		FArchSkyState State;
		State.WeatherPresetB = TEXT("Overcast");
		State.WeatherBlendAlpha = 1.7f;
		State.Sanitise();
		TestEqual(TEXT("Blend alpha clamps to one when B is set"), State.WeatherBlendAlpha, 1.f, 1.e-6f);
	}

	// Sanitising twice must change nothing the first pass did not - i.e. it is idempotent.
	{
		FArchSkyState State;
		State.Year = 2026;
		State.DayOfYear = 999;
		State.TimeOfDayHours = -7.25f;
		State.NorthOffsetDegrees = 725.f;
		State.Sanitise();

		const FArchSkyState AfterFirst = State;
		State.Sanitise();

		TestTrue(TEXT("Sanitise is idempotent"), State.IsNearlyEqual(AfterFirst));
	}

	// Every field must land legal for a wide sweep of hostile input.
	for (int32 Index = 0; Index < 200; ++Index)
	{
		FArchSkyState State;
		State.Year = 1000 + Index * 37;
		State.DayOfYear = -50 + Index * 7;
		State.TimeOfDayHours = -100.f + static_cast<float>(Index) * 3.7f;
		State.NorthOffsetDegrees = -1000.f + static_cast<float>(Index) * 17.f;
		State.TimeFlowRate = -5000.f + static_cast<float>(Index) * 91.f;
		State.Sanitise();

		const bool bLegal =
			State.Year >= 1900 && State.Year <= 2200
			&& State.DayOfYear >= 1 && State.DayOfYear <= 366
			&& State.TimeOfDayHours >= 0.f && State.TimeOfDayHours < 24.f
			&& State.NorthOffsetDegrees >= 0.f && State.NorthOffsetDegrees < 360.f
			&& FMath::Abs(State.TimeFlowRate) <= 600.f;

		if (!TestTrue(TEXT("Sanitise always produces a legal state"), bLegal))
		{
			return false;
		}
	}

	return true;
}

// -----------------------------------------------------------------------------------------
// Replication quantisation
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchSkyReplicationQuantisationTest,
	"ArchSky.State.ReplicationQuantisation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchSkyReplicationQuantisationTest::RunTest(const FString& Parameters)
{
	// Time quantisation is 1/1000 h, which is 3.6 s of simulated time - far below one
	// frame of visible sun movement at any sane speed.
	static constexpr float TimeToleranceHours = 0.002f;

	for (float Hours = 0.f; Hours < 24.f; Hours += 0.37f)
	{
		FArchSkyState Source;
		Source.TimeOfDayHours = Hours;
		Source.DayOfYear = 172;
		Source.Year = 2026;
		Source.WeatherPresetA = TEXT("Clear");
		Source.WeatherPresetB = TEXT("Overcast");
		Source.WeatherBlendAlpha = 0.5f;

		const FArchSkyReplicatedState Wire = FArchSkyReplicatedState::FromSkyState(Source, 7);

		FArchSkyState Destination;
		Wire.ApplyToSkyState(Destination);

		if (!TestEqual(TEXT("Time of day survives the wire"), Destination.TimeOfDayHours, Hours, TimeToleranceHours))
		{
			return false;
		}
	}

	// The flow rate must survive its FULL legal range. This is the assertion that catches a
	// too-narrow integer type: an int16 at a scale of 100 saturates at 327.67 h/s, so every
	// value above that would silently come back wrong.
	for (const float Rate : { -600.f, -327.67f, -60.f, -1.f, -0.0167f, 0.f, 0.0167f, 1.f, 60.f, 327.67f, 600.f })
	{
		FArchSkyState Source;
		Source.TimeFlowRate = Rate;

		const FArchSkyReplicatedState Wire = FArchSkyReplicatedState::FromSkyState(Source, 0);

		if (!TestEqual(FString::Printf(TEXT("Flow rate %.4f survives the wire"), Rate),
			Wire.GetTimeFlowRate(), Rate, 0.01f))
		{
			return false;
		}
	}

	// Weather ids, blend alpha and the discontinuity counter.
	{
		FArchSkyState Source;
		Source.WeatherPresetA = TEXT("DustStorm");
		Source.WeatherPresetB = TEXT("Blizzard");
		Source.WeatherBlendAlpha = 0.25f;
		Source.DayOfYear = 355;
		Source.Year = 2031;

		const FArchSkyReplicatedState Wire = FArchSkyReplicatedState::FromSkyState(Source, 42);

		FArchSkyState Destination;
		Wire.ApplyToSkyState(Destination);

		TestEqual(TEXT("Preset A survives"), Destination.WeatherPresetA, Source.WeatherPresetA);
		TestEqual(TEXT("Preset B survives"), Destination.WeatherPresetB, Source.WeatherPresetB);
		TestEqual(TEXT("Blend alpha survives within 1/255"), Destination.WeatherBlendAlpha, 0.25f, 1.f / 255.f);
		TestEqual(TEXT("Day of year survives"), Destination.DayOfYear, 355);
		TestEqual(TEXT("Year survives"), Destination.Year, 2031);
		TestEqual(TEXT("The discontinuity counter survives"), static_cast<int32>(Wire.DiscontinuityCounter), 42);
	}

	// Location and plan north are level authoring, identical on every machine, and must NOT
	// be touched by an incoming packet - otherwise a client would have its site moved.
	{
		FArchSkyState Destination;
		Destination.Location = FArchGeoLocation(35.6892, 51.3890, 3.5f, 1200.f);
		Destination.NorthOffsetDegrees = 27.5f;

		FArchSkyState Source;
		Source.Location = FArchGeoLocation(0.0, 0.0, 0.f, 0.f);
		Source.NorthOffsetDegrees = 0.f;
		Source.TimeOfDayHours = 9.f;

		const FArchSkyReplicatedState Wire = FArchSkyReplicatedState::FromSkyState(Source, 1);
		Wire.ApplyToSkyState(Destination);

		TestEqual(TEXT("An incoming packet does not move the site latitude"),
			Destination.Location.LatitudeDegrees, 35.6892, 1.e-6);
		TestEqual(TEXT("An incoming packet does not change the plan north offset"),
			Destination.NorthOffsetDegrees, 27.5f, 1.e-4f);
		TestEqual(TEXT("But it does carry the time"), Destination.TimeOfDayHours, 9.f, 0.002f);
	}

	// Equality is what the Director uses to decide whether to push at all.
	{
		FArchSkyState Source;
		Source.TimeOfDayHours = 12.f;

		const FArchSkyReplicatedState First = FArchSkyReplicatedState::FromSkyState(Source, 3);
		const FArchSkyReplicatedState Same = FArchSkyReplicatedState::FromSkyState(Source, 3);
		TestTrue(TEXT("Two packets from one state compare equal"), First == Same);

		Source.TimeOfDayHours = 12.01f;
		const FArchSkyReplicatedState Moved = FArchSkyReplicatedState::FromSkyState(Source, 3);
		TestTrue(TEXT("A moved clock produces a different packet"), First != Moved);

		const FArchSkyReplicatedState Bumped = FArchSkyReplicatedState::FromSkyState(Source, 4);
		TestTrue(TEXT("A bumped discontinuity counter produces a different packet"), Moved != Bumped);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
