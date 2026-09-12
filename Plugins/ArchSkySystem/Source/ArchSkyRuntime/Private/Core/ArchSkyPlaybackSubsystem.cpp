// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/ArchSkyPlaybackSubsystem.h"

#include "Core/ArchSkySubsystem.h"
#include "Data/ArchSkySettings.h"
#include "Data/ArchTimeCalendar.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "ArchSkyPlayback"

namespace ArchPlaybackDetail
{
	/**
	 * The multipliers behind the four named presets.
	 *
	 * ARCH NOTE ON UNITS - read this before changing any number here.
	 *
	 * The brief gives both a formula and a set of labels:
	 *
	 *     CurrentSimTime += DeltaSeconds * SpeedMultiplier        (CurrentSimTime in minutes)
	 *     x1 = real time, x60 = 1 min/sec, x3600 = 1 hour/sec, x8640 = a day in 10 s
	 *
	 * Those two disagree by a factor of 60. Taken literally, the formula makes x1 advance
	 * one MINUTE per real second, which is sixty times real time, and makes x8640 cover six
	 * simulated days every second.
	 *
	 * We implemented the LABELS, because all four are individually annotated, they agree
	 * with each other, and they agree with the stated default of 60 being the "Fast" preset.
	 * SpeedMultiplier is therefore a dimensionless ratio of simulated to real time, and the
	 * accumulation in minutes is:
	 *
	 *     CurrentSimTime += DeltaSeconds * SpeedMultiplier / 60
	 *
	 * which is exactly what the sky subsystem does once the rate is expressed in its own
	 * units of hours per real second (SpeedMultiplier / 3600).
	 */
	static constexpr float RealTimeMultiplier = 1.f;
	static constexpr float FastMultiplier = 60.f;
	static constexpr float HourPerSecondMultiplier = 3600.f;
	static constexpr float FullDayTenSecondsMultiplier = 8640.f;

	/** Seconds in an hour, the conversion between a speed ratio and hours per real second. */
	static constexpr float SecondsPerHour = 3600.f;

	/** Minutes in an hour. */
	static constexpr float MinutesPerHour = 60.f;

	/**
	 * Tolerance when matching a multiplier to a preset, as a fraction of the preset value.
	 * Proportional rather than absolute because the presets span four orders of magnitude:
	 * an absolute epsilon that suits x1 would never match x8640 after a float round trip.
	 */
	static constexpr float PresetMatchTolerance = 1.e-3f;

	/** Shortest loop window we will accept, in minutes. Below this the sun appears frozen. */
	static constexpr float MinimumLoopSpanMinutes = 1.f;
}

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------

bool UArchSkyPlaybackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	// Playback is a runtime transport. Unlike UArchSkySubsystem we do NOT exist in editor
	// worlds: there is nothing to play back when the level is not running, and the editor's
	// Sun Study sliders already scrub the same clock directly.
	switch (World->WorldType)
	{
	case EWorldType::Game:
	case EWorldType::PIE:
	case EWorldType::GamePreview:
		return true;
	default:
		return false;
	}
}

void UArchSkyPlaybackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Declaring the dependency makes the engine create the sky subsystem first, so our
	// Initialize can rely on it existing rather than hoping.
	Collection.InitializeDependency<UArchSkySubsystem>();

	Super::Initialize(Collection);

	ApplyProjectDefaults();

	PreviousSimTime = GetCurrentSimTime();
	bHasPreviousSimTime = true;
	bInitialised = true;

	UE_LOG(LogArchSky, Log,
		TEXT("ArchSky playback initialised (speed x%.0f, step %.0f min, loop %s %.0f-%.0f)."),
		SpeedMultiplier, StepSize, bLoopEnabled ? TEXT("on") : TEXT("off"), LoopStart, LoopEnd);
}

void UArchSkyPlaybackSubsystem::Deinitialize()
{
	OnPlaybackStateChanged.Clear();
	OnPlaybackLooped.Clear();
	OnPlaybackReachedEnd.Clear();
	OnPlaybackSpeedChanged.Clear();

	SkySubsystem.Reset();
	bInitialised = false;

	Super::Deinitialize();
}

void UArchSkyPlaybackSubsystem::ApplyProjectDefaults()
{
	const UArchSkySettings* Settings = UArchSkySettings::Get();
	if (!Settings)
	{
		return;
	}

	// The brief asks for the speed to be "configurable before runtime". Project Settings is
	// where that belongs: a subsystem cannot be placed in a level and so has no details
	// panel of its own, and putting the authored default on the widget would mean every
	// widget instance carried its own copy.
	SpeedPreset = Settings->DefaultPlaybackSpeedPreset;
	SpeedMultiplier = (SpeedPreset == EArchPlaybackSpeedPreset::Custom)
		? FMath::Max(Settings->DefaultPlaybackSpeedMultiplier, UE_KINDA_SMALL_NUMBER)
		: GetMultiplierForPreset(SpeedPreset);

	StepSize = FMath::Clamp(Settings->DefaultPlaybackStepSizeMinutes, 0.01f, MinutesPerDay);

	bLoopEnabled = Settings->bDefaultPlaybackLoopEnabled;

	const float ConfiguredStart = FMath::Clamp(Settings->DefaultPlaybackLoopStartMinutes, 0.f, MinutesPerDay);
	const float ConfiguredEnd = FMath::Clamp(Settings->DefaultPlaybackLoopEndMinutes, 0.f, MinutesPerDay);

	if (ConfiguredEnd - ConfiguredStart >= ArchPlaybackDetail::MinimumLoopSpanMinutes)
	{
		LoopStart = ConfiguredStart;
		LoopEnd = ConfiguredEnd;
	}
	else
	{
		UE_LOG(LogArchSky, Warning,
			TEXT("Configured playback loop window %.1f-%.1f is empty or inverted; using the whole day."),
			ConfiguredStart, ConfiguredEnd);
		LoopStart = 0.f;
		LoopEnd = MinutesPerDay;
	}
}

UArchSkySubsystem* UArchSkyPlaybackSubsystem::GetSkySubsystem() const
{
	if (UArchSkySubsystem* Cached = SkySubsystem.Get())
	{
		return Cached;
	}

	const UWorld* World = GetWorld();
	UArchSkySubsystem* Found = World ? World->GetSubsystem<UArchSkySubsystem>() : nullptr;

	if (Found)
	{
		const_cast<UArchSkyPlaybackSubsystem*>(this)->SkySubsystem = Found;
	}

	return Found;
}

UArchSkyPlaybackSubsystem* UArchSkyPlaybackSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UArchSkyPlaybackSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------

bool UArchSkyPlaybackSubsystem::IsTickable() const
{
	if (IsTemplate() || !bInitialised)
	{
		return false;
	}

	// Nothing to supervise when the clock is not moving. A paused transport costs nothing.
	return IsPlaying();
}

TStatId UArchSkyPlaybackSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UArchSkyPlaybackSubsystem, STATGROUP_Tickables);
}

UWorld* UArchSkyPlaybackSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

void UArchSkyPlaybackSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ArchSky_PlaybackTick);

	if (!IsPlaying())
	{
		return;
	}

	// ARCH NOTE: the clock itself is advanced by UArchSkySubsystem, from the flow rate this
	// transport set. We do NOT accumulate a second copy here - that is the whole point of
	// the design note in the header. This tick only supervises the loop window, which is
	// the one piece of playback behaviour the clock underneath knows nothing about.
	ReconcileExternalSpeedChange();

	const bool bMoved = EnforceLoopBoundary();

	// Record the position AFTER any boundary correction, so the next frame's wrap detection
	// compares against where we actually are rather than where we were about to be.
	PreviousSimTime = GetCurrentSimTime();
	bHasPreviousSimTime = true;

	(void)bMoved;
}

void UArchSkyPlaybackSubsystem::ReconcileExternalSpeedChange()
{
	const UArchSkySubsystem* Sky = GetSkySubsystem();
	if (!Sky)
	{
		return;
	}

	const float ActualFlowRate = Sky->GetSkyState().TimeFlowRate;

	// Something other than this transport changed the rate - `ArchSky.TimeScale`, a loaded
	// preset, a replicated packet. Adopt it rather than fighting it, so the speed label and
	// the dropdown keep telling the truth.
	if (!FMath::IsNearlyEqual(ActualFlowRate, LastPushedFlowRate, 1.e-6f))
	{
		const float AdoptedMultiplier = FlowRateToSpeedMultiplier(ActualFlowRate);

		if (!FMath::IsNearlyEqual(AdoptedMultiplier, SpeedMultiplier, 1.e-3f))
		{
			SpeedMultiplier = AdoptedMultiplier;
			SpeedPreset = ClassifyMultiplier(SpeedMultiplier);
			OnPlaybackSpeedChanged.Broadcast(SpeedMultiplier, SpeedPreset);

			UE_LOG(LogArchSky, Verbose,
				TEXT("Playback adopted an externally set speed of x%.2f."), SpeedMultiplier);
		}

		LastPushedFlowRate = ActualFlowRate;
	}
}

bool UArchSkyPlaybackSubsystem::EnforceLoopBoundary()
{
	const float Current = GetCurrentSimTime();

	// A window covering the whole day with looping off has no boundary to enforce: the
	// clock simply rolls into the next day, which is the existing behaviour and is what an
	// unattended presentation wants.
	const bool bIsWholeDayWindow = (LoopStart <= 0.f) && (LoopEnd >= MinutesPerDay);
	if (!bLoopEnabled && bIsWholeDayWindow)
	{
		return false;
	}

	const bool bRunningForward = SpeedMultiplier >= 0.f;

	const bool bReachedBoundary = bHasPreviousSimTime
		&& DidCrossWindowBoundary(PreviousSimTime, Current, LoopStart, LoopEnd, bRunningForward);

	if (!bReachedBoundary)
	{
		return false;
	}

	if (bLoopEnabled)
	{
		// Reset to the far end of the window, as specified. The overshoot - at most one
		// frame's worth - is discarded rather than carried over, which keeps every lap
		// starting at exactly the same instant and therefore keeps a looped shadow study
		// frame-comparable against itself.
		const float ResetTo = bRunningForward ? LoopStart : LoopEnd;
		SetCurrentSimTime(ResetTo);

		++LoopCount;
		OnPlaybackLooped.Broadcast(LoopCount);

		UE_LOG(LogArchSky, Verbose, TEXT("Playback looped (lap %d), back to %.1f min."), LoopCount, ResetTo);
		return true;
	}

	// Looping is off: land exactly on the boundary and stop, rather than stopping wherever
	// the frame happened to end. A study that stops at 18:00 every time is reproducible.
	const float StopAt = bRunningForward ? LoopEnd : LoopStart;
	SetCurrentSimTime(StopAt);
	Pause();

	OnPlaybackReachedEnd.Broadcast();
	UE_LOG(LogArchSky, Log, TEXT("Playback reached the end of its window at %.1f min and stopped."), StopAt);

	return true;
}

// ---------------------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------------------

bool UArchSkyPlaybackSubsystem::IsPlaying() const
{
	const UArchSkySubsystem* Sky = GetSkySubsystem();
	return Sky && !Sky->IsTimePaused();
}

void UArchSkyPlaybackSubsystem::Play()
{
	UArchSkySubsystem* Sky = GetSkySubsystem();
	if (!Sky)
	{
		UE_LOG(LogArchSky, Warning, TEXT("Play: no ArchSky subsystem in this world."));
		return;
	}

	if (IsPlaying())
	{
		return;
	}

	// Starting from outside the loop window would run to the end of the day and stop,
	// which is never what pressing Play is understood to mean.
	if (bLoopEnabled)
	{
		const float Current = GetCurrentSimTime();
		if (Current < LoopStart || Current >= LoopEnd)
		{
			SetCurrentSimTime(LoopStart);
		}
	}

	LoopCount = 0;
	PreviousSimTime = GetCurrentSimTime();
	bHasPreviousSimTime = true;

	LastPushedFlowRate = SpeedMultiplierToFlowRate(SpeedMultiplier);
	Sky->SetTimeFlowRate(LastPushedFlowRate);

	OnPlaybackStateChanged.Broadcast(true);
}

void UArchSkyPlaybackSubsystem::Pause()
{
	UArchSkySubsystem* Sky = GetSkySubsystem();
	if (!Sky || !IsPlaying())
	{
		return;
	}

	LastPushedFlowRate = 0.f;
	Sky->SetTimeFlowRate(0.f);

	OnPlaybackStateChanged.Broadcast(false);
}

void UArchSkyPlaybackSubsystem::Stop()
{
	const bool bWasPlaying = IsPlaying();

	if (UArchSkySubsystem* Sky = GetSkySubsystem())
	{
		LastPushedFlowRate = 0.f;
		Sky->SetTimeFlowRate(0.f);
	}

	// Stop rewinds; Pause does not. That is the entire difference between the two buttons.
	SetCurrentSimTime(bLoopEnabled ? LoopStart : 0.f);

	LoopCount = 0;
	PreviousSimTime = GetCurrentSimTime();
	bHasPreviousSimTime = true;

	// Stopping mid-drag would otherwise leave the scrub latch armed, and releasing the
	// handle would restart playback the user just stopped.
	bIsScrubbing = false;
	bWasPlayingBeforeScrub = false;

	if (bWasPlaying)
	{
		OnPlaybackStateChanged.Broadcast(false);
	}
}

void UArchSkyPlaybackSubsystem::TogglePlayPause()
{
	IsPlaying() ? Pause() : Play();
}

// ---------------------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------------------

float UArchSkyPlaybackSubsystem::GetCurrentSimTime() const
{
	const UArchSkySubsystem* Sky = GetSkySubsystem();
	if (!Sky)
	{
		return 0.f;
	}

	// The derived view described in the header: one clock, expressed in minutes.
	return Sky->GetSkyState().TimeOfDayHours * ArchPlaybackDetail::MinutesPerHour;
}

void UArchSkyPlaybackSubsystem::SetCurrentSimTime(float NewSimTimeMinutes)
{
	UArchSkySubsystem* Sky = GetSkySubsystem();
	if (!Sky)
	{
		return;
	}

	const float Wrapped = WrapMinutes(NewSimTimeMinutes);
	Sky->SetTimeOfDay(Wrapped / ArchPlaybackDetail::MinutesPerHour);

	// Seeking must not look like a loop wrap on the next tick.
	PreviousSimTime = GetCurrentSimTime();
	bHasPreviousSimTime = true;
}

float UArchSkyPlaybackSubsystem::GetCurrentSimTimeNormalised() const
{
	return FMath::Clamp(GetCurrentSimTime() / MinutesPerDay, 0.f, 1.f);
}

void UArchSkyPlaybackSubsystem::BeginScrub()
{
	if (bIsScrubbing)
	{
		return;
	}

	bIsScrubbing = true;
	bWasPlayingBeforeScrub = IsPlaying();

	if (bWasPlayingBeforeScrub)
	{
		Pause();
	}
}

void UArchSkyPlaybackSubsystem::EndScrub()
{
	if (!bIsScrubbing)
	{
		return;
	}

	bIsScrubbing = false;

	// "Playback resumes from the new position": Play() reads the clock where the user left
	// it, so there is nothing extra to restore.
	//
	// ARCH NOTE: one consequence worth being explicit about. With looping enabled, Play()
	// pulls the clock into [LoopStart, LoopEnd], so releasing a drag that ended OUTSIDE the
	// window snaps into it. That is deliberate and consistent: a loop window is a
	// constraint the user asked for, and the boundary check would pull them in on the very
	// next tick anyway. Scrubbing while PAUSED is unconstrained - the transport does not
	// tick at all then - so inspecting 20:00 with a 06:00-18:00 loop set is still one drag
	// away, it just does not survive pressing Play.
	if (bWasPlayingBeforeScrub)
	{
		bWasPlayingBeforeScrub = false;
		Play();
	}
}

// ---------------------------------------------------------------------------------------
// Speed
// ---------------------------------------------------------------------------------------

float UArchSkyPlaybackSubsystem::SpeedMultiplierToFlowRate(float InSpeedMultiplier) const
{
	// The sky subsystem counts in simulated HOURS per real second.
	return InSpeedMultiplier / ArchPlaybackDetail::SecondsPerHour;
}

float UArchSkyPlaybackSubsystem::FlowRateToSpeedMultiplier(float FlowRate) const
{
	return FlowRate * ArchPlaybackDetail::SecondsPerHour;
}

float UArchSkyPlaybackSubsystem::GetMultiplierForPreset(EArchPlaybackSpeedPreset Preset) const
{
	using namespace ArchPlaybackDetail;

	switch (Preset)
	{
	case EArchPlaybackSpeedPreset::RealTime:          return RealTimeMultiplier;
	case EArchPlaybackSpeedPreset::Fast:              return FastMultiplier;
	case EArchPlaybackSpeedPreset::HourPerSecond:     return HourPerSecondMultiplier;
	case EArchPlaybackSpeedPreset::FullDayTenSeconds: return FullDayTenSecondsMultiplier;
	default:                                          return SpeedMultiplier;
	}
}

EArchPlaybackSpeedPreset UArchSkyPlaybackSubsystem::ClassifyMultiplier(float InSpeedMultiplier) const
{
	using namespace ArchPlaybackDetail;

	static const EArchPlaybackSpeedPreset Candidates[] =
	{
		EArchPlaybackSpeedPreset::RealTime,
		EArchPlaybackSpeedPreset::Fast,
		EArchPlaybackSpeedPreset::HourPerSecond,
		EArchPlaybackSpeedPreset::FullDayTenSeconds
	};

	for (const EArchPlaybackSpeedPreset Candidate : Candidates)
	{
		const float CandidateValue = GetMultiplierForPreset(Candidate);

		// Proportional tolerance; see the note on PresetMatchTolerance.
		if (FMath::Abs(InSpeedMultiplier - CandidateValue) <= CandidateValue * PresetMatchTolerance)
		{
			return Candidate;
		}
	}

	return EArchPlaybackSpeedPreset::Custom;
}

void UArchSkyPlaybackSubsystem::SetSpeedMultiplier(float NewSpeedMultiplier)
{
	// A zero multiplier would be indistinguishable from Pause and would leave the transport
	// claiming to be playing while nothing moved. Refuse it and say why.
	if (FMath::IsNearlyZero(NewSpeedMultiplier))
	{
		UE_LOG(LogArchSky, Warning,
			TEXT("A speed multiplier of zero is not a speed - use Pause() instead. Ignoring."));
		return;
	}

	// The sky subsystem clamps its flow rate to +/-600 h/s, which is 2160000x.
	NewSpeedMultiplier = FMath::Clamp(NewSpeedMultiplier, -2160000.f, 2160000.f);

	if (FMath::IsNearlyEqual(NewSpeedMultiplier, SpeedMultiplier, 1.e-4f))
	{
		return;
	}

	SpeedMultiplier = NewSpeedMultiplier;
	SpeedPreset = ClassifyMultiplier(SpeedMultiplier);

	// Changing speed mid-playback takes effect immediately; changing it while paused only
	// records the setting, so pressing Play later uses it.
	if (IsPlaying())
	{
		if (UArchSkySubsystem* Sky = GetSkySubsystem())
		{
			LastPushedFlowRate = SpeedMultiplierToFlowRate(SpeedMultiplier);
			Sky->SetTimeFlowRate(LastPushedFlowRate);
		}
	}

	OnPlaybackSpeedChanged.Broadcast(SpeedMultiplier, SpeedPreset);
}

void UArchSkyPlaybackSubsystem::SetSpeedPreset(EArchPlaybackSpeedPreset NewPreset)
{
	if (NewPreset == EArchPlaybackSpeedPreset::Custom)
	{
		// Custom is a readout, not a command: it describes a multiplier that matches no
		// preset. Selecting it from a dropdown should not change the speed.
		return;
	}

	SetSpeedMultiplier(GetMultiplierForPreset(NewPreset));
}

FText UArchSkyPlaybackSubsystem::GetSpeedPresetDisplayName(EArchPlaybackSpeedPreset Preset) const
{
	switch (Preset)
	{
	case EArchPlaybackSpeedPreset::RealTime:
		return LOCTEXT("Speed_RealTime", "Real-time (x1)");
	case EArchPlaybackSpeedPreset::Fast:
		return LOCTEXT("Speed_Fast", "Fast (x60 - 1 min/sec)");
	case EArchPlaybackSpeedPreset::HourPerSecond:
		return LOCTEXT("Speed_HourPerSecond", "Hour per second (x3600)");
	case EArchPlaybackSpeedPreset::FullDayTenSeconds:
		return LOCTEXT("Speed_FullDay", "Full day in 10 s (x8640)");
	default:
		break;
	}

	// Custom shows the number, because "Custom" on its own tells the user nothing.
	FNumberFormattingOptions Options;
	Options.MaximumFractionalDigits = 2;
	Options.UseGrouping = true;

	return FText::Format(LOCTEXT("Speed_Custom", "Custom (x{0})"), FText::AsNumber(SpeedMultiplier, &Options));
}

FText UArchSkyPlaybackSubsystem::GetCurrentSpeedDisplayName() const
{
	return GetSpeedPresetDisplayName(SpeedPreset);
}

TArray<EArchPlaybackSpeedPreset> UArchSkyPlaybackSubsystem::GetSelectableSpeedPresets() const
{
	// Custom is excluded: it is a state the transport reports, never one a user picks.
	return {
		EArchPlaybackSpeedPreset::RealTime,
		EArchPlaybackSpeedPreset::Fast,
		EArchPlaybackSpeedPreset::HourPerSecond,
		EArchPlaybackSpeedPreset::FullDayTenSeconds
	};
}

// ---------------------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------------------

void UArchSkyPlaybackSubsystem::SetLoopEnabled(bool bEnabled)
{
	if (bLoopEnabled == bEnabled)
	{
		return;
	}

	bLoopEnabled = bEnabled;
	LoopCount = 0;

	// Turning looping on while parked outside the window would otherwise do nothing visible
	// until the clock happened to wander in.
	if (bLoopEnabled && IsPlaying())
	{
		const float Current = GetCurrentSimTime();
		if (Current < LoopStart || Current >= LoopEnd)
		{
			SetCurrentSimTime(LoopStart);
		}
	}
}

bool UArchSkyPlaybackSubsystem::SetLoopRange(float NewLoopStart, float NewLoopEnd)
{
	const float ClampedStart = FMath::Clamp(NewLoopStart, 0.f, MinutesPerDay);
	const float ClampedEnd = FMath::Clamp(NewLoopEnd, 0.f, MinutesPerDay);

	if (ClampedEnd - ClampedStart < ArchPlaybackDetail::MinimumLoopSpanMinutes)
	{
		UE_LOG(LogArchSky, Warning,
			TEXT("Rejected loop window %.1f-%.1f: the end must be at least %.0f minute(s) after the start. ")
			TEXT("The previous window (%.1f-%.1f) is unchanged."),
			ClampedStart, ClampedEnd, ArchPlaybackDetail::MinimumLoopSpanMinutes, LoopStart, LoopEnd);
		return false;
	}

	LoopStart = ClampedStart;
	LoopEnd = ClampedEnd;
	LoopCount = 0;

	return true;
}

// ---------------------------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------------------------

void UArchSkyPlaybackSubsystem::SetStepSize(float NewStepSize)
{
	// A zero or negative step would make the buttons silent no-ops.
	StepSize = FMath::Clamp(FMath::Abs(NewStepSize), 0.01f, MinutesPerDay);
}

void UArchSkyPlaybackSubsystem::StepForward()
{
	StepByMinutes(StepSize);
}

void UArchSkyPlaybackSubsystem::StepBackward()
{
	StepByMinutes(-StepSize);
}

void UArchSkyPlaybackSubsystem::StepByMinutes(float DeltaMinutes)
{
	// "Step mode pauses continuous playback." Stepping while the clock runs would land
	// somewhere the user did not choose, because the clock moves between the press and
	// the next frame.
	if (IsPlaying())
	{
		Pause();
	}

	SetCurrentSimTime(ApplyStepToMinutes(GetCurrentSimTime(), DeltaMinutes));
}

// ---------------------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------------------

float UArchSkyPlaybackSubsystem::WrapMinutes(float Minutes)
{
	float Wrapped = FMath::Fmod(Minutes, MinutesPerDay);
	if (Wrapped < 0.f)
	{
		Wrapped += MinutesPerDay;
	}

	// Fmod of a value just under 1440 can round up to exactly 1440 in float.
	return (Wrapped >= MinutesPerDay) ? 0.f : Wrapped;
}

bool UArchSkyPlaybackSubsystem::DidCrossWindowBoundary(float PreviousMinutes, float CurrentMinutes,
	float WindowStart, float WindowEnd, bool bRunningForward)
{
	if (bRunningForward)
	{
		// Either the clock passed the end, or it moved backwards - which, while running
		// forwards, can only mean it wrapped through midnight. That second case is the one
		// that catches a window ending at 1440.
		return (CurrentMinutes >= WindowEnd) || (CurrentMinutes < PreviousMinutes);
	}

	return (CurrentMinutes <= WindowStart) || (CurrentMinutes > PreviousMinutes);
}

float UArchSkyPlaybackSubsystem::ApplyStepToMinutes(float CurrentMinutes, float DeltaMinutes)
{
	// ARCH NOTE: the brief asks for the result to be CLAMPED to [0, 1440] rather than
	// wrapped, and we clamp as specified - stepping back from 00:05 lands on midnight and
	// stays there. Wrapping to 23:50 instead would silently change the date under an
	// architect stepping through a single day's shadows, which is a worse failure than a
	// button that stops having an effect at the end of the day.
	const float Stepped = FMath::Clamp(CurrentMinutes + DeltaMinutes, 0.f, MinutesPerDay);

	// 1440 and 0 are the same instant; normalise so the readout says 00:00, not 24:00.
	return (Stepped >= MinutesPerDay) ? 0.f : Stepped;
}

FText UArchSkyPlaybackSubsystem::FormatMinutesAsClock(float Minutes, bool b24Hour)
{
	// Reuses the same formatter the rest of the plugin uses, so the transport's clock and
	// the panel's clock can never disagree about rounding at, say, 13:59.7.
	return ArchTimeCalendar::FormatTimeOfDay(WrapMinutes(Minutes) / ArchPlaybackDetail::MinutesPerHour, b24Hour);
}

FText UArchSkyPlaybackSubsystem::GetFormattedSimTime(bool b24Hour) const
{
	return FormatMinutesAsClock(GetCurrentSimTime(), b24Hour);
}

FText UArchSkyPlaybackSubsystem::GetFormattedSimDate(bool bJalali) const
{
	const UArchSkySubsystem* Sky = GetSkySubsystem();
	if (!Sky)
	{
		return FText::GetEmpty();
	}

	return Sky->GetFormattedDateString(bJalali ? EArchCalendarType::Jalali : EArchCalendarType::Gregorian);
}

#undef LOCTEXT_NAMESPACE
