// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ArchSkyPlaybackSubsystem.h"

#include "Internationalization/Internationalization.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Playback transport tests.
 *
 * Everything here exercises the pure, world-free surface: the speed table, the minute/clock
 * conversions, the loop wrap detection and the step clamp. Those are the parts where an
 * off-by-one or a unit slip would be invisible until an architect noticed the sun in the
 * wrong place, so they are the parts worth pinning down.
 *
 * Behaviour that genuinely needs a live world - Play/Pause actually moving the clock,
 * scrub suspend/resume, the RPC path - is covered by the manual test plan in README.md
 * rather than faked here with a stub world that would test the stub.
 *
 * Run with:  Automation RunTests ArchSky.Playback
 */
namespace ArchPlaybackTestUtils
{
	/** The CDO is enough for every accessor exercised below; none of them touch a world. */
	static UArchSkyPlaybackSubsystem* Transport()
	{
		return GetMutableDefault<UArchSkyPlaybackSubsystem>();
	}
}

// -----------------------------------------------------------------------------------------
// The speed table - the requirement most at risk of a factor-of-60 error
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchPlaybackSpeedTableTest,
	"ArchSky.Playback.SpeedPresetTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchPlaybackSpeedTableTest::RunTest(const FString& Parameters)
{
	UArchSkyPlaybackSubsystem* Playback = ArchPlaybackTestUtils::Transport();
	if (!TestNotNull(TEXT("Playback CDO is available"), Playback))
	{
		return false;
	}

	// The four multipliers themselves.
	TestEqual(TEXT("Real-time is x1"),
		Playback->GetMultiplierForPreset(EArchPlaybackSpeedPreset::RealTime), 1.f);
	TestEqual(TEXT("Fast is x60"),
		Playback->GetMultiplierForPreset(EArchPlaybackSpeedPreset::Fast), 60.f);
	TestEqual(TEXT("Hour-per-second is x3600"),
		Playback->GetMultiplierForPreset(EArchPlaybackSpeedPreset::HourPerSecond), 3600.f);
	TestEqual(TEXT("Full-day-in-ten-seconds is x8640"),
		Playback->GetMultiplierForPreset(EArchPlaybackSpeedPreset::FullDayTenSeconds), 8640.f);

	// And, more importantly, that each one MEANS what its label says. SpeedMultiplier is a
	// ratio of simulated to real time, so simulated minutes per real second is
	// Multiplier / 60. This is the assertion that would have caught the factor-of-60
	// discrepancy between the brief's formula and its labels.
	auto SimulatedMinutesPerRealSecond = [Playback](EArchPlaybackSpeedPreset Preset)
	{
		return Playback->GetMultiplierForPreset(Preset) / 60.f;
	};

	TestEqual(TEXT("Real-time advances one simulated SECOND per real second"),
		SimulatedMinutesPerRealSecond(EArchPlaybackSpeedPreset::RealTime) * 60.f, 1.f, 1.e-4f);

	TestEqual(TEXT("Fast advances one simulated MINUTE per real second"),
		SimulatedMinutesPerRealSecond(EArchPlaybackSpeedPreset::Fast), 1.f, 1.e-4f);

	TestEqual(TEXT("Hour-per-second advances one simulated HOUR per real second"),
		SimulatedMinutesPerRealSecond(EArchPlaybackSpeedPreset::HourPerSecond), 60.f, 1.e-3f);

	TestEqual(TEXT("Full-day preset covers 1440 simulated minutes in ten real seconds"),
		SimulatedMinutesPerRealSecond(EArchPlaybackSpeedPreset::FullDayTenSeconds) * 10.f,
		UArchSkyPlaybackSubsystem::MinutesPerDay, 1.e-2f);

	// The conversion the transport hands the clock: hours per real second.
	auto FlowRateFor = [Playback](EArchPlaybackSpeedPreset Preset)
	{
		return Playback->GetMultiplierForPreset(Preset) / 3600.f;
	};

	TestEqual(TEXT("Hour-per-second is a flow rate of 1.0 h/s"),
		FlowRateFor(EArchPlaybackSpeedPreset::HourPerSecond), 1.f, 1.e-5f);
	TestEqual(TEXT("Full-day preset is a flow rate of 2.4 h/s"),
		FlowRateFor(EArchPlaybackSpeedPreset::FullDayTenSeconds), 2.4f, 1.e-4f);
	TestEqual(TEXT("A full day at 2.4 h/s takes ten real seconds"),
		24.f / FlowRateFor(EArchPlaybackSpeedPreset::FullDayTenSeconds), 10.f, 1.e-3f);

	// The dropdown offers the four real presets and never Custom.
	const TArray<EArchPlaybackSpeedPreset> Selectable = Playback->GetSelectableSpeedPresets();
	TestEqual(TEXT("Four selectable speed presets"), Selectable.Num(), 4);
	TestFalse(TEXT("Custom is not offered in the dropdown"),
		Selectable.Contains(EArchPlaybackSpeedPreset::Custom));

	// Every preset has a non-empty label, including Custom.
	for (int32 Index = 0; Index <= static_cast<int32>(EArchPlaybackSpeedPreset::Custom); ++Index)
	{
		const FText Label = Playback->GetSpeedPresetDisplayName(static_cast<EArchPlaybackSpeedPreset>(Index));
		TestFalse(FString::Printf(TEXT("Speed preset %d has a label"), Index), Label.IsEmpty());
	}

	return true;
}

// -----------------------------------------------------------------------------------------
// Minute wrapping and the HH:MM readout
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchPlaybackTimeFormatTest,
	"ArchSky.Playback.MinutesAndClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchPlaybackTimeFormatTest::RunTest(const FString& Parameters)
{
	// The string comparisons assert English formatting; pin the culture so running the
	// suite in a Persian editor does not fail on Eastern Arabic numerals.
	FInternationalization& I18N = FInternationalization::Get();
	const FString PreviousCulture = I18N.GetCurrentCulture()->GetName();
	I18N.SetCurrentCulture(TEXT("en"));
	ON_SCOPE_EXIT
	{
		I18N.SetCurrentCulture(PreviousCulture);
	};

	using FPlayback = UArchSkyPlaybackSubsystem;

	// --- Wrapping ---
	TestEqual(TEXT("0 stays 0"), FPlayback::WrapMinutes(0.f), 0.f, 1.e-4f);
	TestEqual(TEXT("1440 wraps to 0"), FPlayback::WrapMinutes(1440.f), 0.f, 1.e-4f);
	TestEqual(TEXT("1441 wraps to 1"), FPlayback::WrapMinutes(1441.f), 1.f, 1.e-3f);
	TestEqual(TEXT("-1 wraps to 1439"), FPlayback::WrapMinutes(-1.f), 1439.f, 1.e-3f);
	TestEqual(TEXT("-1440 wraps to 0"), FPlayback::WrapMinutes(-1440.f), 0.f, 1.e-3f);
	TestEqual(TEXT("3000 wraps to 120"), FPlayback::WrapMinutes(3000.f), 120.f, 1.e-2f);

	for (float Minutes = -5000.f; Minutes < 5000.f; Minutes += 37.3f)
	{
		const float Wrapped = FPlayback::WrapMinutes(Minutes);
		if (!TestTrue(TEXT("Wrapped minutes always land inside [0, 1440)"),
			Wrapped >= 0.f && Wrapped < UArchSkyPlaybackSubsystem::MinutesPerDay))
		{
			return false;
		}
	}

	// --- The HH:MM readout ---
	TestEqual(TEXT("0 min is 00:00"), FPlayback::FormatMinutesAsClock(0.f, true).ToString(), FString(TEXT("00:00")));
	TestEqual(TEXT("402 min is 06:42"), FPlayback::FormatMinutesAsClock(402.f, true).ToString(), FString(TEXT("06:42")));
	TestEqual(TEXT("720 min is 12:00"), FPlayback::FormatMinutesAsClock(720.f, true).ToString(), FString(TEXT("12:00")));
	TestEqual(TEXT("1094 min is 18:14"), FPlayback::FormatMinutesAsClock(1094.f, true).ToString(), FString(TEXT("18:14")));
	TestEqual(TEXT("1439 min is 23:59"), FPlayback::FormatMinutesAsClock(1439.f, true).ToString(), FString(TEXT("23:59")));

	// 1440 is midnight, and must read 00:00 rather than 24:00.
	TestEqual(TEXT("1440 min reads as 00:00"),
		FPlayback::FormatMinutesAsClock(1440.f, true).ToString(), FString(TEXT("00:00")));

	// The rounding edge: 719.7 min is 11:59.7, which must read 12:00 and never 11:60.
	TestEqual(TEXT("719.7 min rounds to 12:00"),
		FPlayback::FormatMinutesAsClock(719.7f, true).ToString(), FString(TEXT("12:00")));

	// 12-hour form.
	TestEqual(TEXT("0 min in 12-hour form is 12:00 AM"),
		FPlayback::FormatMinutesAsClock(0.f, false).ToString(), FString(TEXT("12:00 AM")));
	TestEqual(TEXT("870 min in 12-hour form is 2:30 PM"),
		FPlayback::FormatMinutesAsClock(870.f, false).ToString(), FString(TEXT("2:30 PM")));

	// Minutes and the plugin's decimal hours must describe the same instant.
	for (float Minutes = 0.f; Minutes < 1440.f; Minutes += 17.f)
	{
		const float Hours = Minutes / 60.f;
		if (!TestEqual(TEXT("Minutes and decimal hours agree"), Hours * 60.f, Minutes, 1.e-2f))
		{
			return false;
		}
	}

	return true;
}

// -----------------------------------------------------------------------------------------
// Loop window detection - including the 1440 wrap that has no "greater than" to catch it
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchPlaybackLoopBoundaryTest,
	"ArchSky.Playback.LoopBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchPlaybackLoopBoundaryTest::RunTest(const FString& Parameters)
{
	using FPlayback = UArchSkyPlaybackSubsystem;

	// A morning-to-evening window, running forwards.
	constexpr float Start = 360.f;   // 06:00
	constexpr float End = 1080.f;    // 18:00

	TestFalse(TEXT("Mid-window motion is not a boundary crossing"),
		FPlayback::DidCrossWindowBoundary(700.f, 705.f, Start, End, true));

	TestFalse(TEXT("Arriving just short of the end is not a crossing"),
		FPlayback::DidCrossWindowBoundary(1070.f, 1079.f, Start, End, true));

	TestTrue(TEXT("Landing exactly on the end is a crossing"),
		FPlayback::DidCrossWindowBoundary(1070.f, 1080.f, Start, End, true));

	TestTrue(TEXT("Overshooting the end is a crossing"),
		FPlayback::DidCrossWindowBoundary(1070.f, 1095.f, Start, End, true));

	// The case the second term exists for: a window that ends at midnight. The clock wraps
	// 1439 -> 3 and so never compares greater than 1440.
	TestTrue(TEXT("Wrapping through midnight is a crossing when the window ends at 1440"),
		FPlayback::DidCrossWindowBoundary(1439.f, 3.f, 0.f, 1440.f, true));

	TestFalse(TEXT("Ordinary forward motion in a full-day window is not a crossing"),
		FPlayback::DidCrossWindowBoundary(1400.f, 1430.f, 0.f, 1440.f, true));

	// Running backwards, the roles of the two ends swap.
	TestFalse(TEXT("Mid-window reverse motion is not a crossing"),
		FPlayback::DidCrossWindowBoundary(700.f, 695.f, Start, End, false));

	TestTrue(TEXT("Reaching the start while reversing is a crossing"),
		FPlayback::DidCrossWindowBoundary(365.f, 360.f, Start, End, false));

	TestTrue(TEXT("Reversing past the start is a crossing"),
		FPlayback::DidCrossWindowBoundary(365.f, 350.f, Start, End, false));

	TestTrue(TEXT("Wrapping backwards through midnight is a crossing"),
		FPlayback::DidCrossWindowBoundary(3.f, 1439.f, 0.f, 1440.f, false));

	// A simulated run: stepping forward through a 06:00-18:00 window at a coarse rate must
	// report exactly one crossing, on the frame that reaches or passes 18:00.
	{
		int32 Crossings = 0;
		float Previous = Start;

		for (int32 Frame = 0; Frame < 200; ++Frame)
		{
			const float Current = FPlayback::WrapMinutes(Previous + 5.f);
			if (FPlayback::DidCrossWindowBoundary(Previous, Current, Start, End, true))
			{
				++Crossings;
				break;
			}
			Previous = Current;
		}

		TestEqual(TEXT("A forward run through the window reports exactly one crossing"), Crossings, 1);
		TestTrue(TEXT("The crossing happens at or after the window end"), Previous + 5.f >= End - 5.f);
	}

	return true;
}

// -----------------------------------------------------------------------------------------
// Step clamping
// -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchPlaybackStepTest,
	"ArchSky.Playback.Step",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchPlaybackStepTest::RunTest(const FString& Parameters)
{
	using FPlayback = UArchSkyPlaybackSubsystem;

	// The default 15-minute step.
	TestEqual(TEXT("Stepping forward from 12:00 lands on 12:15"),
		FPlayback::ApplyStepToMinutes(720.f, 15.f), 735.f, 1.e-4f);
	TestEqual(TEXT("Stepping backward from 12:00 lands on 11:45"),
		FPlayback::ApplyStepToMinutes(720.f, -15.f), 705.f, 1.e-4f);

	// The brief asks for a clamp, not a wrap.
	TestEqual(TEXT("Stepping back from 00:05 clamps to midnight, it does not wrap to 23:50"),
		FPlayback::ApplyStepToMinutes(5.f, -15.f), 0.f, 1.e-4f);
	TestEqual(TEXT("Stepping back from midnight stays at midnight"),
		FPlayback::ApplyStepToMinutes(0.f, -15.f), 0.f, 1.e-4f);

	// The far end clamps to 1440, which is normalised to 0 so the clock reads 00:00.
	TestEqual(TEXT("Stepping forward from 23:50 clamps to the end of the day and normalises to 0"),
		FPlayback::ApplyStepToMinutes(1430.f, 15.f), 0.f, 1.e-4f);
	TestEqual(TEXT("Stepping forward from 23:45 lands exactly on the end of the day, reported as 0"),
		FPlayback::ApplyStepToMinutes(1425.f, 15.f), 0.f, 1.e-4f);
	TestEqual(TEXT("Stepping forward from 23:30 lands on 23:45"),
		FPlayback::ApplyStepToMinutes(1410.f, 15.f), 1425.f, 1.e-4f);

	// An enormous step clamps rather than wrapping many times.
	TestEqual(TEXT("A huge forward step clamps to the end of the day"),
		FPlayback::ApplyStepToMinutes(720.f, 99999.f), 0.f, 1.e-4f);
	TestEqual(TEXT("A huge backward step clamps to midnight"),
		FPlayback::ApplyStepToMinutes(720.f, -99999.f), 0.f, 1.e-4f);

	// The result is always a legal position.
	for (float Current = 0.f; Current <= 1440.f; Current += 53.f)
	{
		for (const float Delta : { -1440.f, -60.f, -15.f, -1.f, 0.f, 1.f, 15.f, 60.f, 1440.f })
		{
			const float Result = FPlayback::ApplyStepToMinutes(Current, Delta);
			if (!TestTrue(TEXT("A step always lands inside [0, 1440)"),
				Result >= 0.f && Result < UArchSkyPlaybackSubsystem::MinutesPerDay))
			{
				return false;
			}
		}
	}

	// Stepping forward then back returns to where it started, away from the clamps.
	for (float Current = 100.f; Current < 1300.f; Current += 97.f)
	{
		const float RoundTrip = FPlayback::ApplyStepToMinutes(FPlayback::ApplyStepToMinutes(Current, 15.f), -15.f);
		if (!TestEqual(TEXT("Step forward then back is a round trip"), RoundTrip, Current, 1.e-3f))
		{
			return false;
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
