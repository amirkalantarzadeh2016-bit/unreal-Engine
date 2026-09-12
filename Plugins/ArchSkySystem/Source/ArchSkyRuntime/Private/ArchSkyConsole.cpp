// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ArchSkyDirector.h"
#include "Core/ArchSkyPlaybackSubsystem.h"
#include "Core/ArchSkySubsystem.h"
#include "Data/ArchLocationPreset.h"
#include "Data/ArchSkySettings.h"
#include "Data/ArchTimeCalendar.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Math/ArchMoonMath.h"
#include "Math/ArchSolarMath.h"
#include "Util/ArchSkyLog.h"

/**
 * Console surface for the ArchSky System.
 *
 * ARCH NOTE: the whole file is compiled out of a Shipping build. A packaged architectural
 * walkthrough handed to a client should not carry commands that can silently move the sun
 * to a date the shadow study was not signed off against - and the on-screen debug HUD
 * would be an outright defect in a client deliverable. Everything an end user legitimately
 * needs is on the UI panel, which ships.
 */
#if !UE_BUILD_SHIPPING

namespace ArchSkyConsole
{
	/**
	 * Resolves the subsystem for whichever world the console is attached to.
	 * The InWorld the console hands us is authoritative; GWorld is not, and using it is
	 * the classic way a console command ends up driving the editor world during PIE.
	 */
	UArchSkySubsystem* GetSubsystem(UWorld* InWorld)
	{
		if (!InWorld)
		{
			UE_LOG(LogArchSky, Warning, TEXT("ArchSky console command ran with no world context."));
			return nullptr;
		}

		UArchSkySubsystem* Subsystem = InWorld->GetSubsystem<UArchSkySubsystem>();
		if (!Subsystem)
		{
			UE_LOG(LogArchSky, Warning, TEXT("No ArchSky subsystem in world '%s'."), *InWorld->GetName());
		}

		return Subsystem;
	}

	/** Parses Args[Index] as a float, reporting the problem rather than defaulting silently. */
	bool ParseFloatArg(const TArray<FString>& Args, int32 Index, const TCHAR* ArgName, float& OutValue)
	{
		if (!Args.IsValidIndex(Index))
		{
			UE_LOG(LogArchSky, Warning, TEXT("Missing argument <%s>."), ArgName);
			return false;
		}

		if (!Args[Index].IsNumeric())
		{
			UE_LOG(LogArchSky, Warning, TEXT("Argument <%s> must be a number; got '%s'."), ArgName, *Args[Index]);
			return false;
		}

		OutValue = FCString::Atof(*Args[Index]);
		return true;
	}

	/** Parses Args[Index] as an int32. */
	bool ParseIntArg(const TArray<FString>& Args, int32 Index, const TCHAR* ArgName, int32& OutValue)
	{
		float AsFloat = 0.f;
		if (!ParseFloatArg(Args, Index, ArgName, AsFloat))
		{
			return false;
		}

		OutValue = FMath::RoundToInt32(AsFloat);
		return true;
	}

	// -----------------------------------------------------------------------------------
	// Command implementations
	// -----------------------------------------------------------------------------------

	void SetTime(const TArray<FString>& Args, UWorld* InWorld)
	{
		float Hours = 0.f;
		if (!ParseFloatArg(Args, 0, TEXT("0-24"), Hours))
		{
			return;
		}

		if (UArchSkySubsystem* Subsystem = GetSubsystem(InWorld))
		{
			Subsystem->SetTimeOfDay(Hours);
			UE_LOG(LogArchSky, Display, TEXT("Time of day set to %s."),
				*Subsystem->GetFormattedTimeString(true).ToString());
		}
	}

	void SetDay(const TArray<FString>& Args, UWorld* InWorld)
	{
		int32 Day = 1;
		if (!ParseIntArg(Args, 0, TEXT("1-366"), Day))
		{
			return;
		}

		if (UArchSkySubsystem* Subsystem = GetSubsystem(InWorld))
		{
			Subsystem->SetDayOfYear(Day);
			UE_LOG(LogArchSky, Display, TEXT("Date set to %s."),
				*Subsystem->GetFormattedDateString(EArchCalendarType::Gregorian).ToString());
		}
	}

	void SetLocation(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (!Args.IsValidIndex(0))
		{
			// An unknown or missing id is a chance to be helpful, not just to complain.
			FString Available;
			for (const FArchLocationEntry& Entry : UArchLocationLibrary::GetBuiltInLocations())
			{
				Available += (Available.IsEmpty() ? TEXT("") : TEXT(", "));
				Available += Entry.CityId.ToString();
			}

			UE_LOG(LogArchSky, Warning, TEXT("Usage: ArchSky.SetLocation <CityId>. Available: %s"), *Available);
			return;
		}

		if (UArchSkySubsystem* Subsystem = GetSubsystem(InWorld))
		{
			if (Subsystem->SetLocationPreset(FName(*Args[0])))
			{
				const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();
				UE_LOG(LogArchSky, Display, TEXT("Location set to %s. Sunrise %s, sunset %s."),
					*Args[0],
					*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunriseHours, true).ToString(),
					*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunsetHours, true).ToString());
			}
		}
	}

	void SetWeather(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (!Args.IsValidIndex(0))
		{
			UArchSkySubsystem* Subsystem = GetSubsystem(InWorld);
			FString Available;
			if (Subsystem)
			{
				for (const FName& Id : Subsystem->GetAvailableWeatherPresetIds())
				{
					Available += (Available.IsEmpty() ? TEXT("") : TEXT(", "));
					Available += Id.ToString();
				}
			}

			UE_LOG(LogArchSky, Warning,
				TEXT("Usage: ArchSky.SetWeather <PresetId> [TransitionSeconds]. Available: %s"), *Available);
			return;
		}

		float TransitionSeconds = 3.f;
		if (Args.IsValidIndex(1))
		{
			ParseFloatArg(Args, 1, TEXT("TransitionSeconds"), TransitionSeconds);
		}

		if (UArchSkySubsystem* Subsystem = GetSubsystem(InWorld))
		{
			Subsystem->SetWeatherPreset(FName(*Args[0]), TransitionSeconds);
		}
	}

	void SetNorthOffset(const TArray<FString>& Args, UWorld* InWorld)
	{
		float Degrees = 0.f;
		if (!ParseFloatArg(Args, 0, TEXT("degrees"), Degrees))
		{
			return;
		}

		if (UArchSkySubsystem* Subsystem = GetSubsystem(InWorld))
		{
			Subsystem->SetNorthOffset(Degrees);
			UE_LOG(LogArchSky, Display, TEXT("Plan north offset set to %.2f degrees."), Degrees);
		}
	}

	void SetTimeScale(const TArray<FString>& Args, UWorld* InWorld)
	{
		float Rate = 0.f;
		if (!ParseFloatArg(Args, 0, TEXT("HoursPerSecond"), Rate))
		{
			return;
		}

		if (UArchSkySubsystem* Subsystem = GetSubsystem(InWorld))
		{
			Subsystem->SetTimeFlowRate(Rate);
			UE_LOG(LogArchSky, Display, TEXT("Time flow rate set to %.3f simulated hours per real second."), Rate);
		}
	}

	void LogState(const TArray<FString>& Args, UWorld* InWorld)
	{
		UArchSkySubsystem* Subsystem = GetSubsystem(InWorld);
		if (!Subsystem)
		{
			return;
		}

		const FArchSkyState& State = Subsystem->GetSkyState();
		const FArchSolarPosition& Sun = Subsystem->GetSolarPosition();
		const FArchLunarPosition& Moon = Subsystem->GetLunarPosition();
		const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();

		UE_LOG(LogArchSky, Display, TEXT("--- ArchSky state ---"));
		UE_LOG(LogArchSky, Display, TEXT("  Date/time  : %s %s (day %d of %d)"),
			*Subsystem->GetFormattedDateString(EArchCalendarType::Gregorian).ToString(),
			*Subsystem->GetFormattedTimeString(true).ToString(),
			State.DayOfYear, State.Year);
		UE_LOG(LogArchSky, Display, TEXT("  Jalali     : %s"),
			*Subsystem->GetFormattedDateString(EArchCalendarType::Jalali).ToString());
		UE_LOG(LogArchSky, Display, TEXT("  Location   : %.4f, %.4f  UTC%+.1f  %.0f m"),
			State.Location.LatitudeDegrees, State.Location.LongitudeDegrees,
			State.Location.TimezoneOffsetHours, State.Location.ElevationMeters);
		UE_LOG(LogArchSky, Display, TEXT("  Plan north : %.2f deg"), State.NorthOffsetDegrees);
		UE_LOG(LogArchSky, Display, TEXT("  Sun        : az %.3f deg, alt %.3f deg (apparent %.3f), refraction %.4f deg"),
			Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees, Sun.AltitudeDegrees, Sun.AtmosphericRefractionDegrees);
		UE_LOG(LogArchSky, Display, TEXT("  Solar      : declination %.4f deg, EoT %.3f min, hour angle %.3f deg"),
			Sun.DeclinationDegrees, Sun.EquationOfTimeMinutes, Sun.HourAngleDegrees);
		UE_LOG(LogArchSky, Display, TEXT("  Day        : sunrise %.4f h, noon %.4f h, sunset %.4f h, length %.4f h%s"),
			DayInfo.SunriseHours, DayInfo.SolarNoonHours, DayInfo.SunsetHours, DayInfo.DayLengthHours,
			DayInfo.bPolarDay ? TEXT(" [POLAR DAY]") : (DayInfo.bPolarNight ? TEXT(" [POLAR NIGHT]") : TEXT("")));
		UE_LOG(LogArchSky, Display, TEXT("  Moon       : az %.3f deg, alt %.3f deg, illum %.3f, age %.2f d, %.0f km"),
			Moon.AzimuthDegrees, Moon.TrueAltitudeDegrees, Moon.IlluminatedFraction, Moon.AgeDays, Moon.DistanceKm);
		UE_LOG(LogArchSky, Display, TEXT("  Weather    : %s -> %s @ %.3f"),
			*State.WeatherPresetA.ToString(), *State.WeatherPresetB.ToString(), State.WeatherBlendAlpha);
		UE_LOG(LogArchSky, Display, TEXT("  Shadow     : %.3f x object height"), Subsystem->GetShadowLengthMultiplier());
		UE_LOG(LogArchSky, Display, TEXT("  Director   : %s"),
			Subsystem->HasSceneDirector() ? TEXT("registered") : TEXT("NONE - nothing will be visible"));
	}

	/** Resolves the playback transport, which does not exist in an editor world. */
	UArchSkyPlaybackSubsystem* GetPlayback(UWorld* InWorld)
	{
		if (!InWorld)
		{
			UE_LOG(LogArchSky, Warning, TEXT("ArchSky console command ran with no world context."));
			return nullptr;
		}

		UArchSkyPlaybackSubsystem* Playback = InWorld->GetSubsystem<UArchSkyPlaybackSubsystem>();
		if (!Playback)
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("No ArchSky playback transport in world '%s'. Playback exists only in a running game or PIE."),
				*InWorld->GetName());
		}

		return Playback;
	}

	void PlaybackPlay(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (UArchSkyPlaybackSubsystem* Playback = GetPlayback(InWorld))
		{
			Playback->Play();
			UE_LOG(LogArchSky, Display, TEXT("Playing at x%.0f from %s."),
				Playback->GetSpeedMultiplier(), *Playback->GetFormattedSimTime(true).ToString());
		}
	}

	void PlaybackPause(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (UArchSkyPlaybackSubsystem* Playback = GetPlayback(InWorld))
		{
			Playback->Pause();
			UE_LOG(LogArchSky, Display, TEXT("Paused at %s."), *Playback->GetFormattedSimTime(true).ToString());
		}
	}

	void PlaybackStop(const TArray<FString>& Args, UWorld* InWorld)
	{
		if (UArchSkyPlaybackSubsystem* Playback = GetPlayback(InWorld))
		{
			Playback->Stop();
			UE_LOG(LogArchSky, Display, TEXT("Stopped and rewound to %s."),
				*Playback->GetFormattedSimTime(true).ToString());
		}
	}

	void PlaybackStep(const TArray<FString>& Args, UWorld* InWorld)
	{
		UArchSkyPlaybackSubsystem* Playback = GetPlayback(InWorld);
		if (!Playback)
		{
			return;
		}

		// No argument steps forward by one StepSize; an argument steps by that many minutes.
		float Minutes = Playback->GetStepSize();
		if (Args.IsValidIndex(0) && !ParseFloatArg(Args, 0, TEXT("Minutes"), Minutes))
		{
			return;
		}

		Playback->StepByMinutes(Minutes);
		UE_LOG(LogArchSky, Display, TEXT("Stepped %+.1f min to %s."),
			Minutes, *Playback->GetFormattedSimTime(true).ToString());
	}

	void PlaybackSetSimTime(const TArray<FString>& Args, UWorld* InWorld)
	{
		float Minutes = 0.f;
		if (!ParseFloatArg(Args, 0, TEXT("0-1440"), Minutes))
		{
			return;
		}

		if (UArchSkyPlaybackSubsystem* Playback = GetPlayback(InWorld))
		{
			Playback->SetCurrentSimTime(Minutes);
			UE_LOG(LogArchSky, Display, TEXT("Sought to %s (%.1f min)."),
				*Playback->GetFormattedSimTime(true).ToString(), Playback->GetCurrentSimTime());
		}
	}

	void PlaybackSetSpeed(const TArray<FString>& Args, UWorld* InWorld)
	{
		UArchSkyPlaybackSubsystem* Playback = GetPlayback(InWorld);
		if (!Playback)
		{
			return;
		}

		if (!Args.IsValidIndex(0))
		{
			UE_LOG(LogArchSky, Warning,
				TEXT("Usage: ArchSky.Playback.Speed <multiplier | realtime | fast | hour | day>"));
			return;
		}

		const FString Argument = Args[0].ToLower();

		// Accept the preset names as well as a raw number, because "ArchSky.Playback.Speed
		// day" is a great deal easier to remember mid-presentation than "8640".
		if (Argument == TEXT("realtime") || Argument == TEXT("real"))
		{
			Playback->SetSpeedPreset(EArchPlaybackSpeedPreset::RealTime);
		}
		else if (Argument == TEXT("fast"))
		{
			Playback->SetSpeedPreset(EArchPlaybackSpeedPreset::Fast);
		}
		else if (Argument == TEXT("hour"))
		{
			Playback->SetSpeedPreset(EArchPlaybackSpeedPreset::HourPerSecond);
		}
		else if (Argument == TEXT("day"))
		{
			Playback->SetSpeedPreset(EArchPlaybackSpeedPreset::FullDayTenSeconds);
		}
		else
		{
			float Multiplier = 60.f;
			if (!ParseFloatArg(Args, 0, TEXT("multiplier"), Multiplier))
			{
				return;
			}
			Playback->SetSpeedMultiplier(Multiplier);
		}

		UE_LOG(LogArchSky, Display, TEXT("Playback speed is now %s."),
			*Playback->GetCurrentSpeedDisplayName().ToString());
	}

	void PlaybackLoop(const TArray<FString>& Args, UWorld* InWorld)
	{
		UArchSkyPlaybackSubsystem* Playback = GetPlayback(InWorld);
		if (!Playback)
		{
			return;
		}

		if (!Args.IsValidIndex(0))
		{
			UE_LOG(LogArchSky, Display, TEXT("Loop is %s, window %.0f-%.0f min (%s - %s)."),
				Playback->IsLoopEnabled() ? TEXT("ON") : TEXT("OFF"),
				Playback->GetLoopStart(), Playback->GetLoopEnd(),
				*UArchSkyPlaybackSubsystem::FormatMinutesAsClock(Playback->GetLoopStart(), true).ToString(),
				*UArchSkyPlaybackSubsystem::FormatMinutesAsClock(Playback->GetLoopEnd(), true).ToString());
			UE_LOG(LogArchSky, Display, TEXT("Usage: ArchSky.Playback.Loop <0|1> [StartMinutes] [EndMinutes]"));
			return;
		}

		int32 Enabled = 0;
		if (!ParseIntArg(Args, 0, TEXT("0|1"), Enabled))
		{
			return;
		}

		if (Args.IsValidIndex(2))
		{
			float Start = 0.f;
			float End = UArchSkyPlaybackSubsystem::MinutesPerDay;
			if (ParseFloatArg(Args, 1, TEXT("StartMinutes"), Start)
				&& ParseFloatArg(Args, 2, TEXT("EndMinutes"), End))
			{
				Playback->SetLoopRange(Start, End);
			}
		}

		Playback->SetLoopEnabled(Enabled != 0);

		UE_LOG(LogArchSky, Display, TEXT("Loop %s, window %.0f-%.0f min."),
			Playback->IsLoopEnabled() ? TEXT("ON") : TEXT("OFF"),
			Playback->GetLoopStart(), Playback->GetLoopEnd());
	}

	// -----------------------------------------------------------------------------------
	// CVars
	// -----------------------------------------------------------------------------------

	static TAutoConsoleVariable<int32> CVarShowState(
		TEXT("ArchSky.Debug.ShowState"),
		0,
		TEXT("Draws the live ArchSky state as an on-screen HUD.\n")
		TEXT("  0: off (default)\n")
		TEXT("  1: on"),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarDrawSunPath(
		TEXT("ArchSky.Debug.DrawSunPath"),
		0,
		TEXT("Draws the sun's path for the current date as debug lines around the Director.\n")
		TEXT("  0: off (default)\n")
		TEXT("  1: on"),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarLogSolarMath(
		TEXT("ArchSky.Debug.LogSolarMath"),
		0,
		TEXT("Logs the full solar/lunar solution every second while time is flowing.\n")
		TEXT("  0: off (default)\n")
		TEXT("  1: on"),
		ECVF_Cheat);

	// ARCH NOTE: ArchSky.Perf.SkyRecaptureThreshold is deliberately NOT declared here.
	// It is a scalability knob, not a cheat, so it must exist in Shipping too - and this
	// whole file is compiled out of Shipping. It lives in ArchSkyDirector.cpp instead,
	// next to its only consumer.

	/** Radius, in world units, of the debug sun-path arc drawn around the Director. */
	static TAutoConsoleVariable<float> CVarSunPathRadius(
		TEXT("ArchSky.Debug.SunPathRadius"),
		2000.f,
		TEXT("Radius in centimetres of the debug sun-path arc."),
		ECVF_Cheat);

	// -----------------------------------------------------------------------------------
	// Command registration
	// -----------------------------------------------------------------------------------

	static FAutoConsoleCommandWithWorldAndArgs CmdSetTime(
		TEXT("ArchSky.SetTime"),
		TEXT("ArchSky.SetTime <0-24>  -  Sets the local time of day in decimal hours."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetTime));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetDay(
		TEXT("ArchSky.SetDay"),
		TEXT("ArchSky.SetDay <1-366>  -  Sets the day of the year."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetDay));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetLocation(
		TEXT("ArchSky.SetLocation"),
		TEXT("ArchSky.SetLocation <CityId>  -  Selects a city from the location library."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetLocation));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetWeather(
		TEXT("ArchSky.SetWeather"),
		TEXT("ArchSky.SetWeather <PresetId> [TransitionSeconds]  -  Starts a weather transition."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetWeather));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetNorthOffset(
		TEXT("ArchSky.SetNorthOffset"),
		TEXT("ArchSky.SetNorthOffset <deg>  -  Sets the scene yaw at which true north lies."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetNorthOffset));

	static FAutoConsoleCommandWithWorldAndArgs CmdTimeScale(
		TEXT("ArchSky.TimeScale"),
		TEXT("ArchSky.TimeScale <float>  -  Simulated hours per real second. 0 pauses."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetTimeScale));

	static FAutoConsoleCommandWithWorldAndArgs CmdPlaybackPlay(
		TEXT("ArchSky.Playback.Play"),
		TEXT("ArchSky.Playback.Play  -  Starts the simulation running at the current speed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaybackPlay));

	static FAutoConsoleCommandWithWorldAndArgs CmdPlaybackPause(
		TEXT("ArchSky.Playback.Pause"),
		TEXT("ArchSky.Playback.Pause  -  Stops the clock where it is."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaybackPause));

	static FAutoConsoleCommandWithWorldAndArgs CmdPlaybackStop(
		TEXT("ArchSky.Playback.Stop"),
		TEXT("ArchSky.Playback.Stop  -  Stops the clock and rewinds to the loop start, or midnight."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaybackStop));

	static FAutoConsoleCommandWithWorldAndArgs CmdPlaybackStep(
		TEXT("ArchSky.Playback.Step"),
		TEXT("ArchSky.Playback.Step [Minutes]  -  Steps by StepSize, or by the given minutes. Negative goes back."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaybackStep));

	static FAutoConsoleCommandWithWorldAndArgs CmdPlaybackSeek(
		TEXT("ArchSky.Playback.Seek"),
		TEXT("ArchSky.Playback.Seek <0-1440>  -  Seeks to a position in minutes from midnight."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaybackSetSimTime));

	static FAutoConsoleCommandWithWorldAndArgs CmdPlaybackSpeed(
		TEXT("ArchSky.Playback.Speed"),
		TEXT("ArchSky.Playback.Speed <multiplier | realtime | fast | hour | day>  -  Sets the playback speed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaybackSetSpeed));

	static FAutoConsoleCommandWithWorldAndArgs CmdPlaybackLoop(
		TEXT("ArchSky.Playback.Loop"),
		TEXT("ArchSky.Playback.Loop <0|1> [StartMinutes] [EndMinutes]  -  Sets loop mode and its window."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaybackLoop));

	static FAutoConsoleCommandWithWorldAndArgs CmdLogState(
		TEXT("ArchSky.LogState"),
		TEXT("ArchSky.LogState  -  Dumps the full sky state, solar solution and lunar solution to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LogState));
}

// ---------------------------------------------------------------------------------------
// Debug drawing
// ---------------------------------------------------------------------------------------

namespace ArchSkyDebugDraw
{
	/** Seconds between LogSolarMath dumps, so the option is usable rather than a firehose. */
	static constexpr float SolarLogInterval = 1.f;

	static float SecondsSinceSolarLog = 0.f;

	/**
	 * Per-frame debug tick, hooked to the world tick rather than to the Director, so the
	 * HUD and the sun path still draw in a level whose Director has been deleted - which
	 * is exactly the situation someone turning on ShowState is usually trying to diagnose.
	 */
	void OnWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
	{
		if (!World || !World->IsGameWorld() || TickType != LEVELTICK_All)
		{
			return;
		}

		UArchSkySubsystem* Subsystem = World->GetSubsystem<UArchSkySubsystem>();
		if (!Subsystem)
		{
			return;
		}

		const FArchSkyState& State = Subsystem->GetSkyState();
		const FArchSolarPosition& Sun = Subsystem->GetSolarPosition();
		const FArchLunarPosition& Moon = Subsystem->GetLunarPosition();
		const FArchSolarDayInfo& DayInfo = Subsystem->GetSolarDayInfo();

		// --- State HUD ---
		const UArchSkySettings* Settings = UArchSkySettings::Get();
		const bool bShowState = (ArchSkyConsole::CVarShowState.GetValueOnGameThread() != 0)
			|| (Settings && Settings->bShowStateHudByDefault);

		if (bShowState && GEngine)
		{
			// Fixed keys so the lines overwrite in place instead of scrolling.
			auto Line = [](int32 Key, const FColor& Colour, const FString& Text)
			{
				GEngine->AddOnScreenDebugMessage(Key, 0.f, Colour, Text);
			};

			Line(9000, FColor::Cyan, TEXT("--- ArchSky ---"));
			Line(9001, FColor::White, FString::Printf(TEXT("  %s   %s"),
				*Subsystem->GetFormattedDateString(EArchCalendarType::Gregorian).ToString(),
				*Subsystem->GetFormattedTimeString(true).ToString()));
			Line(9002, FColor::White, FString::Printf(TEXT("  Jalali: %s"),
				*Subsystem->GetFormattedDateString(EArchCalendarType::Jalali).ToString()));
			Line(9003, FColor::White, FString::Printf(TEXT("  Sun:  az %6.2f   alt %6.2f"),
				Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees));
			Line(9004, FColor::White, FString::Printf(TEXT("  Rise %s   Noon %s   Set %s"),
				*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunriseHours, true).ToString(),
				*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SolarNoonHours, true).ToString(),
				*ArchTimeCalendar::FormatTimeOfDay(DayInfo.SunsetHours, true).ToString()));
			Line(9005, FColor::White, FString::Printf(TEXT("  Shadow: %.2f x height   Noon alt %.2f"),
				Subsystem->GetShadowLengthMultiplier(), DayInfo.MaxAltitudeDegrees));
			Line(9006, FColor::White, FString::Printf(TEXT("  Moon: %s  %.0f%% lit  alt %6.2f"),
				*ArchMoonMath::GetMoonPhaseDisplayName(Moon.Phase).ToString(),
				Moon.IlluminatedFraction * 100.0, Moon.TrueAltitudeDegrees));
			Line(9007, FColor::White, FString::Printf(TEXT("  Weather: %s -> %s @ %.2f"),
				*State.WeatherPresetA.ToString(), *State.WeatherPresetB.ToString(), State.WeatherBlendAlpha));
			Line(9008, FColor::White, FString::Printf(TEXT("  Plan north: %.1f deg   Rate: %.2f h/s%s"),
				State.NorthOffsetDegrees, State.TimeFlowRate,
				Subsystem->IsTimePaused() ? TEXT(" [PAUSED]") : TEXT("")));

			if (const UArchSkyPlaybackSubsystem* Playback = World->GetSubsystem<UArchSkyPlaybackSubsystem>())
			{
				Line(9009, FColor::White, FString::Printf(TEXT("  Transport: %s   %.1f min   %s"),
					Playback->IsPlaying() ? TEXT("PLAYING") : TEXT("PAUSED"),
					Playback->GetCurrentSimTime(),
					*Playback->GetCurrentSpeedDisplayName().ToString()));

				if (Playback->IsLoopEnabled())
				{
					Line(9010, FColor::White, FString::Printf(TEXT("  Loop: %.0f-%.0f min   lap %d"),
						Playback->GetLoopStart(), Playback->GetLoopEnd(), Playback->GetLoopCount()));
				}
			}

			if (!Subsystem->HasSceneDirector())
			{
				Line(9011, FColor::Red, TEXT("  NO ARCHSKY DIRECTOR IN THIS LEVEL - the sky will not update."));
			}
		}

		// --- Sun path ---
		if (ArchSkyConsole::CVarDrawSunPath.GetValueOnGameThread() != 0)
		{
			// Anchor on the Director when there is one, otherwise on the world origin.
			FVector Origin = FVector::ZeroVector;
			for (TActorIterator<AArchSkyDirector> It(World); It; ++It)
			{
				Origin = It->GetActorLocation();
				break;
			}

			const float Radius = ArchSkyConsole::CVarSunPathRadius.GetValueOnGameThread();

			// Sample the whole day at 15-minute resolution: fine enough that the arc reads
			// as a curve, coarse enough that it is 96 line segments rather than thousands.
			constexpr int32 SampleCount = 96;
			FVector PreviousPoint = FVector::ZeroVector;
			bool bHasPrevious = false;

			for (int32 Index = 0; Index <= SampleCount; ++Index)
			{
				const float SampleHours = (static_cast<float>(Index) / static_cast<float>(SampleCount)) * 24.f;
				const FArchSolarPosition Sample = Subsystem->GetSolarPositionAtHour(SampleHours);

				const FVector Direction = ArchSolarMath::SolarToUnrealDirectionToBody(
					Sample.AzimuthDegrees, Sample.TrueAltitudeDegrees, State.NorthOffsetDegrees);

				const FVector Point = Origin + Direction * Radius;

				if (bHasPrevious)
				{
					// Above the horizon in yellow, below in dim blue, so the day/night
					// split of the arc is obvious at a glance.
					const FColor Colour = (Sample.TrueAltitudeDegrees > 0.0)
						? FColor(255, 214, 92)
						: FColor(40, 60, 110);

					DrawDebugLine(World, PreviousPoint, Point, Colour, false, -1.f, 0, 2.f);
				}

				PreviousPoint = Point;
				bHasPrevious = true;
			}

			// The sun's current position, plus the line back to the origin.
			const FVector SunDirection = ArchSolarMath::SolarToUnrealDirectionToBody(
				Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees, State.NorthOffsetDegrees);
			const FVector SunPoint = Origin + SunDirection * Radius;

			DrawDebugSphere(World, SunPoint, Radius * 0.03f, 12, FColor::Yellow, false, -1.f, 0, 2.f);
			DrawDebugLine(World, Origin, SunPoint, FColor::Yellow, false, -1.f, 0, 1.f);

			// True north and plan north, in the visualiser's colours.
			const FVector TrueNorth = FRotator(0.f, State.NorthOffsetDegrees, 0.f).RotateVector(FVector::ForwardVector);
			DrawDebugDirectionalArrow(World, Origin, Origin + TrueNorth * Radius,
				Radius * 0.05f, FColor(255, 64, 64), false, -1.f, 0, 3.f);
			DrawDebugDirectionalArrow(World, Origin, Origin + FVector::ForwardVector * Radius * 0.8f,
				Radius * 0.05f, FColor(64, 160, 255), false, -1.f, 0, 3.f);
		}

		// --- Solar math log ---
		if (ArchSkyConsole::CVarLogSolarMath.GetValueOnGameThread() != 0)
		{
			SecondsSinceSolarLog += DeltaSeconds;
			if (SecondsSinceSolarLog >= SolarLogInterval)
			{
				SecondsSinceSolarLog = 0.f;

				UE_LOG(LogArchSky, Display,
					TEXT("[SolarMath] t=%.4f h  JD-based decl=%.5f  EoT=%.4f min  HA=%.5f  az=%.5f  alt=%.5f (app %.5f)"),
					State.TimeOfDayHours, Sun.DeclinationDegrees, Sun.EquationOfTimeMinutes,
					Sun.HourAngleDegrees, Sun.AzimuthDegrees, Sun.TrueAltitudeDegrees, Sun.AltitudeDegrees);
			}
		}
	}

	static FDelegateHandle WorldTickHandle;
}

/**
 * Registers the debug tick when the module starts. Called from FArchSkyRuntimeModule so
 * the hook's lifetime is tied to the module rather than to any world.
 */
void ArchSkyConsole_RegisterDebugHooks()
{
	if (!ArchSkyDebugDraw::WorldTickHandle.IsValid())
	{
		ArchSkyDebugDraw::WorldTickHandle =
			FWorldDelegates::OnWorldTickStart.AddStatic(&ArchSkyDebugDraw::OnWorldTick);
	}
}

/** Unregisters the debug tick. */
void ArchSkyConsole_UnregisterDebugHooks()
{
	if (ArchSkyDebugDraw::WorldTickHandle.IsValid())
	{
		FWorldDelegates::OnWorldTickStart.Remove(ArchSkyDebugDraw::WorldTickHandle);
		ArchSkyDebugDraw::WorldTickHandle.Reset();
	}
}

#else // UE_BUILD_SHIPPING

// Shipping builds get no console surface at all; these keep the module code uniform.
void ArchSkyConsole_RegisterDebugHooks() {}
void ArchSkyConsole_UnregisterDebugHooks() {}

#endif // !UE_BUILD_SHIPPING
