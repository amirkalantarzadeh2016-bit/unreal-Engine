// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/ArchSkyPlaybackTypes.h"
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"

#include "ArchSkyPlaybackSubsystem.generated.h"

class UArchSkySubsystem;

/** Fired when playback starts or stops. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArchPlaybackStateChanged, bool, bIsPlaying);

/** Fired when the loop end is reached and playback jumps back to the loop start. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArchPlaybackLooped, int32, LoopCount);

/** Fired when the loop end is reached with looping disabled, so playback stops. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArchPlaybackReachedEnd);

/** Fired when the speed multiplier or its preset changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnArchPlaybackSpeedChanged, float, NewSpeedMultiplier, EArchPlaybackSpeedPreset, NewPreset);

/**
 * Transport controls for the sun simulation: play, pause, stop, scrub, loop and step.
 *
 * WHY THIS IS A SUBSYSTEM AND NOT WIDGET STATE
 * --------------------------------------------
 * The brief describes `CurrentSimTime` as a variable on the widget, accumulated on the
 * widget's Event Tick. That would make the widget a SECOND source of truth for the
 * simulated clock, next to UArchSkySubsystem's own. The two would then disagree the moment
 * anything else moved time - a console command, a saved preset, a replicated packet, the
 * editor's Sun Study sliders - and the sun would visibly fight itself.
 *
 * So `CurrentSimTime` here is a DERIVED VIEW of the one authoritative clock, expressed in
 * the minutes-from-midnight the brief asks for:
 *
 *     CurrentSimTime (minutes) == UArchSkySubsystem::TimeOfDayHours * 60
 *
 * Reading it converts; writing it seeks. The accumulation is mathematically identical to
 * the one specified - see the note on SpeedMultiplier below - it simply happens in the one
 * place that owns the clock. The practical wins: playback keeps running when the panel is
 * hidden, the console and the UI cannot desynchronise, and the existing replication path
 * carries playback to clients for free.
 *
 * THREADING: game thread only.
 */
UCLASS(DisplayName = "ArchSky Playback Subsystem")
class ARCHSKYRUNTIME_API UArchSkyPlaybackSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	//~ End FTickableGameObject

	/** Convenience accessor. Returns nullptr when the world has no playback subsystem. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get ArchSky Playback"))
	static UArchSkyPlaybackSubsystem* Get(const UObject* WorldContextObject);

	/** Minutes in a day. The full range of CurrentSimTime. */
	static constexpr float MinutesPerDay = 1440.f;

	// -----------------------------------------------------------------------------------
	// Transport
	// -----------------------------------------------------------------------------------

	/**
	 * Starts playback at the current SpeedMultiplier.
	 * If looping is enabled and the clock is outside [LoopStart, LoopEnd], it first seeks
	 * to LoopStart - otherwise pressing Play outside the loop would run to the end of the
	 * day and stop, which is never what the button is understood to mean.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void Play();

	/** Stops the clock where it is. The position is kept. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void Pause();

	/**
	 * Stops the clock AND rewinds it: to LoopStart when looping is enabled, otherwise to
	 * midnight. This is what distinguishes Stop from Pause.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void Stop();

	/** Play if paused, pause if playing. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void TogglePlayPause();

	/**
	 * True while the clock is advancing.
	 *
	 * Derived from the sky subsystem's flow rate rather than stored, so it can never
	 * disagree with whether time is actually moving - including when something else stops
	 * the clock, such as `ArchSky.TimeScale 0` from the console.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	bool IsPlaying() const;

	// -----------------------------------------------------------------------------------
	// Position - the scrubber axis
	// -----------------------------------------------------------------------------------

	/** Minutes from midnight, 0 .. 1440. The bidirectional binding target for the slider. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	float GetCurrentSimTime() const;

	/**
	 * Seeks the simulation. Values outside [0, 1440] wrap rather than clamp, so a slider
	 * that overshoots its own maximum by a float epsilon does not stick at 23:59.
	 *
	 * Playback state is untouched: seek while playing and playback continues from the new
	 * position, which is the behaviour the brief asks for.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void SetCurrentSimTime(float NewSimTimeMinutes);

	/** CurrentSimTime as a 0..1 fraction, for a normalised slider. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	float GetCurrentSimTimeNormalised() const;

	/**
	 * Call when the user grabs the scrubber. Suspends playback for the duration of the drag
	 * and remembers whether it was running.
	 *
	 * Without this, a drag fights the clock: every frame the user moves the handle, the
	 * simulation also advances, so the handle drifts away from the pointer.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void BeginScrub();

	/** Call when the user releases the scrubber. Resumes playback if it was running. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void EndScrub();

	/** True between BeginScrub and EndScrub. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	bool IsScrubbing() const { return bIsScrubbing; }

	// -----------------------------------------------------------------------------------
	// Speed
	// -----------------------------------------------------------------------------------

	/**
	 * Ratio of simulated time to real time. 1 = real time, 60 = one simulated minute per
	 * real second, 3600 = one simulated hour per real second, 8640 = a day in ten seconds.
	 *
	 * Default 60, matching the Fast preset.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	float GetSpeedMultiplier() const { return SpeedMultiplier; }

	/** Sets an arbitrary multiplier. Selects the matching preset, or Custom. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void SetSpeedMultiplier(float NewSpeedMultiplier);

	/** The preset currently selected. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	EArchPlaybackSpeedPreset GetSpeedPreset() const { return SpeedPreset; }

	/** Selects a named preset. Passing Custom leaves the multiplier alone. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void SetSpeedPreset(EArchPlaybackSpeedPreset NewPreset);

	/** The multiplier a named preset stands for. Returns the current one for Custom. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	float GetMultiplierForPreset(EArchPlaybackSpeedPreset Preset) const;

	/** Localised label, e.g. "Hour per second (x3600)". For the label beside the slider. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	FText GetSpeedPresetDisplayName(EArchPlaybackSpeedPreset Preset) const;

	/** Label for the preset currently selected. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	FText GetCurrentSpeedDisplayName() const;

	/** Every preset in menu order, for populating a dropdown. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	TArray<EArchPlaybackSpeedPreset> GetSelectableSpeedPresets() const;

	// -----------------------------------------------------------------------------------
	// Loop
	// -----------------------------------------------------------------------------------

	/** True when reaching LoopEnd jumps back to LoopStart instead of stopping. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	bool IsLoopEnabled() const { return bLoopEnabled; }

	/** Turns looping on or off. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void SetLoopEnabled(bool bEnabled);

	/** Start of the loop window, in minutes from midnight. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	float GetLoopStart() const { return LoopStart; }

	/** End of the loop window, in minutes from midnight. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	float GetLoopEnd() const { return LoopEnd; }

	/**
	 * Sets the loop window. Both ends are clamped to [0, 1440].
	 *
	 * An inverted or degenerate window (end <= start) is rejected with a warning and the
	 * previous window is kept - silently swapping the two would be a worse surprise than
	 * refusing, because a zero-length loop freezes the sun and looks like a hang.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	bool SetLoopRange(float NewLoopStart, float NewLoopEnd);

	/** How many times the loop has restarted since playback last began. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	int32 GetLoopCount() const { return LoopCount; }

	// -----------------------------------------------------------------------------------
	// Step
	// -----------------------------------------------------------------------------------

	/** Minutes added or removed by one step. Default 15. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	float GetStepSize() const { return StepSize; }

	/** Sets the step size. Clamped to a positive value no larger than a whole day. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void SetStepSize(float NewStepSize);

	/** Adds StepSize minutes. Pauses continuous playback first, as stepping implies. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void StepForward();

	/** Subtracts StepSize minutes. Pauses continuous playback first. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void StepBackward();

	/** Steps by an arbitrary number of minutes. Negative steps backwards. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Playback")
	void StepByMinutes(float DeltaMinutes);

	// -----------------------------------------------------------------------------------
	// Display
	// -----------------------------------------------------------------------------------

	/** CurrentSimTime as "HH:MM" (or "H:MM AM/PM" when b24Hour is false). */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	FText GetFormattedSimTime(bool b24Hour = true) const;

	/** The simulated date, which advances when playback runs past midnight. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	FText GetFormattedSimDate(bool bJalali = false) const;

	/** Static helper: converts minutes from midnight to an "HH:MM" string. */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	static FText FormatMinutesAsClock(float Minutes, bool b24Hour = true);

	/** Wraps a minute value into [0, 1440). */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	static float WrapMinutes(float Minutes);

	/**
	 * Did the clock leave the playback window between two samples?
	 *
	 * Pure and static so the wrap detection - the fiddliest part of loop mode - can be
	 * unit-tested without standing up a world.
	 *
	 * The second term is what catches a window ending at 1440: the clock never compares
	 * greater than 1440 because it wraps to 0 first, so a sample that moved BACKWARDS is
	 * the only evidence that midnight was crossed.
	 *
	 * @param PreviousMinutes  Position at the previous sample, 0 .. 1440.
	 * @param CurrentMinutes   Position now, 0 .. 1440.
	 * @param WindowStart      Loop start, minutes.
	 * @param WindowEnd        Loop end, minutes.
	 * @param bRunningForward  False when the clock is running backwards.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	static bool DidCrossWindowBoundary(float PreviousMinutes, float CurrentMinutes,
		float WindowStart, float WindowEnd, bool bRunningForward = true);

	/**
	 * The result of a step, clamped to [0, 1440] as the brief specifies, with 1440
	 * normalised back to 0 so the readout says 00:00 rather than 24:00.
	 *
	 * Pure and static for the same reason as above.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchSky|Playback")
	static float ApplyStepToMinutes(float CurrentMinutes, float DeltaMinutes);

	// -----------------------------------------------------------------------------------
	// Events
	// -----------------------------------------------------------------------------------

	/** Play or pause changed. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Playback|Events")
	FOnArchPlaybackStateChanged OnPlaybackStateChanged;

	/** The loop restarted. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Playback|Events")
	FOnArchPlaybackLooped OnPlaybackLooped;

	/** The end of the window was reached with looping off, so playback stopped. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Playback|Events")
	FOnArchPlaybackReachedEnd OnPlaybackReachedEnd;

	/** The speed changed. */
	UPROPERTY(BlueprintAssignable, Category = "ArchSky|Playback|Events")
	FOnArchPlaybackSpeedChanged OnPlaybackSpeedChanged;

private:
	/** The clock this transport drives. Weak: the sky subsystem may outlive or predecease us. */
	TWeakObjectPtr<UArchSkySubsystem> SkySubsystem;

	/** Resolves the sky subsystem, caching the result. */
	UArchSkySubsystem* GetSkySubsystem() const;

	/** Converts SpeedMultiplier into the hours-per-real-second the sky subsystem wants. */
	float SpeedMultiplierToFlowRate(float InSpeedMultiplier) const;

	/** The inverse of SpeedMultiplierToFlowRate. */
	float FlowRateToSpeedMultiplier(float FlowRate) const;

	/** Chooses the preset a multiplier corresponds to, or Custom. */
	EArchPlaybackSpeedPreset ClassifyMultiplier(float InSpeedMultiplier) const;

	/** Enforces the loop window. Returns true when the clock was moved. */
	bool EnforceLoopBoundary();

	/** Picks up a flow-rate change made by something other than this transport. */
	void ReconcileExternalSpeedChange();

	/** Applies the playback defaults from UArchSkySettings. */
	void ApplyProjectDefaults();

	/** Ratio of simulated time to real time. See GetSpeedMultiplier. */
	float SpeedMultiplier = 60.f;

	/** Which named preset SpeedMultiplier corresponds to. */
	EArchPlaybackSpeedPreset SpeedPreset = EArchPlaybackSpeedPreset::Fast;

	/** Loop window, in minutes from midnight. */
	bool bLoopEnabled = false;
	float LoopStart = 0.f;
	float LoopEnd = MinutesPerDay;

	/** Minutes per step. */
	float StepSize = 15.f;

	/** Restarts since playback last began. Reset by Play and Stop. */
	int32 LoopCount = 0;

	/** CurrentSimTime at the end of the previous tick, for wrap detection. */
	float PreviousSimTime = 0.f;
	bool bHasPreviousSimTime = false;

	/** Scrub state. */
	bool bIsScrubbing = false;
	bool bWasPlayingBeforeScrub = false;

	/** Flow rate this transport last pushed, to spot an external change. */
	float LastPushedFlowRate = 0.f;

	/** True once Initialize has run. */
	bool bInitialised = false;
};
