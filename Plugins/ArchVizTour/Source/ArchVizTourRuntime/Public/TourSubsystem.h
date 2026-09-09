// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "TourTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "TourSubsystem.generated.h"

class ACineCameraActor;
class AGameModeBase;
class ALevelSequenceActor;
class APlayerController;
class ATourCameraRig;
class ATourPath;
class ULevelSequencePlayer;
class UTourInputConfig;
class UTourSequencePreset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTourStateChanged, ETourState, OldState, ETourState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTourStepChanged, int32, StepIndex, const FText&, Label);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTourProgress, float, TourAlpha, float, StepAlpha);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTourFinished, bool, bWasInterrupted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTourCustomEventTag, FGameplayTag, Tag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTourPresetLoaded, UTourSequencePreset*, Preset);

/**
 * Everything a step needs that has to be looked up rather than authored.
 *
 * Resolution is retried on step entry rather than only at load: a path or camera can be
 * streamed in, spawned by a Blueprint, or built at runtime from a save slot long after the
 * tour was loaded.
 */
USTRUCT()
struct FTourResolvedStep
{
	GENERATED_BODY()

	/** Path this step traverses. Only meaningful for SplineMove steps. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ATourPath> Path;

	/** Camera this step cuts to. Only meaningful for StaticCamera steps. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACineCameraActor> StaticCamera;

	/** Distance along Path where the step begins, in centimetres. */
	float StartDistance = 0.0f;

	/** Distance along Path where the step ends, in centimetres. */
	float EndDistance = 0.0f;

	/** Resolved step length in seconds, before GlobalTimeScale and TimeScale. */
	float Duration = 0.0f;

	/**
	 * Distances (centimetres) at which the step's motion is broken into sub-moves: the step's
	 * start, every authored point inside its range that has a dwell time, and its end.
	 *
	 * Decomposing the step this way is what lets per-point DwellTime coexist with per-point
	 * easing: each sub-move gets its own eased ramp, so the camera settles into a hold and
	 * pulls away from it, instead of one ease spanning the whole step and a hard stop at each
	 * pause. With no dwells and no easing the decomposition collapses to a single linear
	 * sub-move, which is exactly the constant cm/s traversal.
	 */
	TArray<float> SegmentBoundaries;

	/** Seconds spent moving along each sub-move. One entry fewer than SegmentBoundaries. */
	TArray<float> SegmentDurations;

	/** Seconds held at each boundary before continuing. Same length as SegmentBoundaries. */
	TArray<float> BoundaryDwells;

	/** Start-of-move damping at each boundary, 0..1. Same length as SegmentBoundaries. */
	TArray<float> BoundaryEaseIn;

	/** End-of-move damping at each boundary, 0..1. Same length as SegmentBoundaries. */
	TArray<float> BoundaryEaseOut;

	/** Tour-relative time at which this step starts, in seconds. Used for scrubbing. */
	float StartTime = 0.0f;

	/** False until the step's references have been found. */
	bool bResolved = false;

	/** True once the step has been reported unresolvable, so the warning is logged only once. */
	bool bReportedUnresolved = false;
};

/**
 * The single authoritative tour state machine, and the only object the playback UI talks to.
 *
 * Ticks as a UTickableWorldSubsystem rather than through a hidden helper actor: a tour is
 * world state, not a placeable thing, and an actor spawned purely to obtain a Tick shows up
 * in the outliner, in saves, and in every "what is this?" conversation afterwards.
 *
 * All timing is accumulated from UWorld::GetDeltaSeconds(), which already carries
 * AWorldSettings time dilation, and scaled by TimeScale and the preset's GlobalTimeScale.
 * Movement along a path is arc-length parameterised, so an authored cm/s speed is constant
 * regardless of point spacing or framerate.
 */
UCLASS(DisplayName = "Tour Subsystem")
class ARCHVIZTOURRUNTIME_API UTourSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- USubsystem -------------------------------------------------------
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- FTickableGameObject ---------------------------------------------
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	/** Blueprint-friendly accessor. Returns null outside a valid world. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Tour Subsystem"))
	static UTourSubsystem* Get(const UObject* WorldContextObject);

	// ---------------------------------------------------------------------
	// Delegates. Bind these instead of polling: every one of them fires exactly when the
	// corresponding state changes, so a UMG widget never needs a tick of its own.
	// ---------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Events")
	FOnTourStateChanged OnTourStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Events")
	FOnTourStepChanged OnStepChanged;

	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Events")
	FOnTourProgress OnTourProgress;

	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Events")
	FOnTourFinished OnTourFinished;

	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Events")
	FOnTourCustomEventTag OnCustomEventTag;

	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Events")
	FOnTourPresetLoaded OnPresetLoaded;

	// ---------------------------------------------------------------------
	// Loading
	// ---------------------------------------------------------------------

	/**
	 * Install a tour, stopping whatever was playing.
	 *
	 * The preset's soft path references are streamed in asynchronously; OnPresetLoaded fires
	 * once they are resident, which for a tour with no soft references is the same frame.
	 *
	 * @param Preset  Tour to install. Null clears the current tour.
	 * @return true when the preset was accepted (non-null and holding at least one step).
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	bool LoadTour(UTourSequencePreset* Preset);

	/**
	 * Install a tour that is not resident yet.
	 * Streams the preset in and then behaves exactly like LoadTour.
	 * @param PresetPath  Soft reference to the tour. An unset reference clears the current tour.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void LoadTourAsync(TSoftObjectPtr<UTourSequencePreset> PresetPath);

	/** The installed tour, or null when none is loaded. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	UTourSequencePreset* GetLoadedTour() const { return LoadedPreset; }

	// ---------------------------------------------------------------------
	// Transport
	// ---------------------------------------------------------------------

	/** Start (or resume) playback from the current step. No-op without a loaded tour. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void PlayTour();

	/**
	 * Stop playback and return the view to the player's own camera.
	 * Fires OnTourFinished with bWasInterrupted = true when a tour was actually running.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void StopTour();

	/** Pause or resume without changing position. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void SetPaused(bool bPaused);

	/** Pause if playing, resume if paused. No-op in any other state. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void TogglePause();

	/** Jump to the start of the next step. At the last step, finishes or loops per the preset. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void NextStep();

	/**
	 * Jump to the start of the previous step.
	 * Standard transport behaviour: more than RestartStepThreshold seconds into the current
	 * step, this restarts that step instead of leaving it.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void PreviousStep();

	/**
	 * Jump to a step by index.
	 * @param StepIndex  Index to jump to. Out-of-range indices are clamped and warned about.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void JumpToStep(int32 StepIndex);

	/** Return to step 0 and play from the beginning. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void RestartTour();

	/**
	 * Scale playback speed on top of the preset's GlobalTimeScale.
	 * @param NewTimeScale  Multiplier; clamped to a positive value. Use SetPlaybackDirection to reverse.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void SetTimeScale(float NewTimeScale);

	/** Current transport time scale. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	float GetTimeScale() const { return TimeScale; }

	/**
	 * Jump to a normalised position in the whole tour.
	 * @param Alpha  0 is the first frame, 1 the last. Clamped.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void ScrubToAlpha(float Alpha);

	/**
	 * Set the playback direction.
	 * @param Direction  Positive plays forward, negative backwards. The magnitude is ignored;
	 *                   use SetTimeScale for speed. Zero is rejected.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	void SetPlaybackDirection(float Direction);

	/** +1 when playing forwards, -1 when playing backwards. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	float GetPlaybackDirection() const { return PlaybackDirection; }

	// ---------------------------------------------------------------------
	// Queries
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	ETourState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	int32 GetCurrentStepIndex() const { return CurrentStepIndex; }

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	int32 GetStepCount() const;

	/** Label of the current step, or an empty text when no tour is loaded. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	FText GetCurrentStepLabel() const;

	/** Progress through the whole tour, 0..1, weighted by step duration. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	float GetTourProgress() const;

	/** Progress through the current step, 0..1. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	float GetStepProgress() const;

	/** Every step's label, in order, for building a step list in the UI. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	TArray<FText> GetStepLabels() const;

	/** Elapsed tour time in seconds, and the tour's total length. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour")
	void GetTourTimes(float& OutElapsedSeconds, float& OutTotalSeconds) const;

	// --- Button-enable helpers, so the widget duplicates no transport logic ---

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsPlayButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsPauseButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsStopButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsNextButtonEnabled() const;

	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|UI")
	bool IsPreviousButtonEnabled() const;

	// ---------------------------------------------------------------------
	// Runtime authoring support
	// ---------------------------------------------------------------------

	/**
	 * Spawn a path actor from plain data and publish it under a name.
	 *
	 * How a packaged build gets paths at all: it cannot create a UTourPathPreset asset, so
	 * UTourPersistenceLibrary rebuilds the geometry into real ATourPath actors, which the same
	 * FTourStep::SplinePathRef lookup then finds.
	 *
	 * @param PathName  Name and tag the actor is published under.
	 * @param PathData  Geometry to build.
	 * @return The spawned actor, or null when the world is gone or the data is degenerate.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	ATourPath* RegisterRuntimePath(FName PathName, const FTourPathData& PathData);

	/** Destroy every actor created by RegisterRuntimePath. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	void ClearRuntimePaths();

	/** The rig driven during SplineMove steps, spawning it if the tour has not needed it yet. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour")
	ATourCameraRig* GetOrSpawnCameraRig();

	// ---------------------------------------------------------------------
	// Configuration
	// ---------------------------------------------------------------------

	/**
	 * Keep advancing while gameplay is paused.
	 *
	 * A paused world reports its own delta as unusable, so this switches the accumulator to the
	 * real frame delta. Presentation tours that must keep running behind a paused simulation
	 * want it on; anything driving gameplay wants it off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchViz Tour")
	bool bUseUnpausedDeltaTime = false;

	/**
	 * How far into a step PreviousStep restarts it instead of going back one, in seconds.
	 * Matches the behaviour of every media transport control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchViz Tour", meta = (ClampMin = "0.0", Units = "s"))
	float RestartStepThreshold = 1.0f;

	/** Class spawned by GetOrSpawnCameraRig. Override to use a Blueprint rig. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchViz Tour")
	TSubclassOf<ATourCameraRig> CameraRigClass;

private:
	// --- State ------------------------------------------------------------

	UPROPERTY(Transient)
	TObjectPtr<UTourSequencePreset> LoadedPreset;

	UPROPERTY(Transient)
	TArray<FTourResolvedStep> ResolvedSteps;

	UPROPERTY(Transient)
	TWeakObjectPtr<ATourCameraRig> CameraRig;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ATourPath>> RuntimePaths;

	/** Level sequence actor and player used by ETourPlaybackBackend::Sequencer. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ALevelSequenceActor> SequenceActor;

	UPROPERTY(Transient)
	TObjectPtr<ULevelSequencePlayer> SequencePlayer;

	ETourState State = ETourState::Idle;

	int32 CurrentStepIndex = INDEX_NONE;

	/** Seconds elapsed inside the current step, in tour time. */
	float StepElapsed = 0.0f;

	/** Seconds remaining in the current view-target blend; drives the Blending state. */
	float BlendRemaining = 0.0f;

	float TimeScale = 1.0f;

	float PlaybackDirection = 1.0f;

	/** Backend actually in use; may differ from the preset's request if the bake is missing. */
	ETourPlaybackBackend ActiveBackend = ETourPlaybackBackend::Procedural;

	/** Total authored length of the tour in seconds, before TimeScale. */
	float TotalDuration = 0.0f;

	/** View target to restore when the tour stops. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> PreTourViewTarget;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> CachedController;

	/** Handle for the preset's streamed soft references; released on Deinitialize. */
	TSharedPtr<FStreamableHandle> StreamingHandle;

	/** Registration for FGameModeEvents::OnGameModePostLoginEvent. */
	FDelegateHandle PostLoginHandle;

	// --- Internals --------------------------------------------------------

	/** Change state and broadcast, ignoring no-op transitions. */
	void SetState(ETourState NewState);

	/** Delta to accumulate this frame, honouring time dilation, pause and bUseUnpausedDeltaTime. */
	float ComputeTourDeltaSeconds() const;

	/** Rebuild ResolvedSteps from the loaded preset. Unresolvable steps are marked, not dropped. */
	void RebuildResolvedSteps();

	/** Retry reference lookup for one step. @return true when it is now usable. */
	bool ResolveStep(int32 StepIndex);

	/** Length of a step in seconds before any time scaling, resolving spline steps by arc length. */
	float ResolveStepDuration(int32 StepIndex) const;

	/** Break a resolved spline step into dwell-separated sub-moves and total its duration. */
	void BuildSplineStepTimeline(int32 StepIndex);

	/**
	 * Map elapsed time inside a spline step to a distance along its path.
	 * @param Resolved  Step whose timeline to walk.
	 * @param Elapsed   Seconds since the step began, in tour time.
	 * @return Distance along the path, in centimetres.
	 */
	static float ComputeSplineDistance(const FTourResolvedStep& Resolved, float Elapsed);

	/** Enter a step: resolve it, take the view target, reset timing, broadcast. */
	void BeginStep(int32 StepIndex, bool bFromStart);

	/** Advance past the end of the current step, honouring bPauseAtEnd and bLoopTour. */
	void AdvanceStepForward();

	/** Step backwards past the start of the current step. */
	void AdvanceStepBackward();

	/** Evaluate the current step and drive the camera. Called once per tick while running. */
	void EvaluateCurrentStep(float DeltaSeconds);

	/** Complete the tour normally (not interrupted). */
	void FinishTour();

	/** Find an ATourPath by name or tag. Null when absent. */
	ATourPath* FindTourPath(FName PathName) const;

	/** Find an ACineCameraActor by name or tag. Null when absent. */
	ACineCameraActor* FindStaticCamera(FName CameraName) const;

	/** The controller the tour drives, caching it and tolerating it not existing yet. */
	APlayerController* GetTourController();

	/** Point the player at an actor with this step's blend parameters. */
	void ApplyViewTarget(AActor* NewViewTarget, const FTourStep& Step);

	/** Restore the view target that was active before the tour started. */
	void RestorePreTourViewTarget();

	/** Called when a controller joins after the tour was already loaded. */
	void HandleGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer);

	/** Finish installing a preset once its soft references are resident. */
	void FinalizeLoadTour(UTourSequencePreset* Preset);

	/** Async load completion for LoadTourAsync. */
	void HandleTourPresetStreamed(TSoftObjectPtr<UTourSequencePreset> PresetPath);

	/** Bring up the Level Sequence player. @return false when no bake is available. */
	bool StartSequencerBackend();

	/** Tear down the Level Sequence player. */
	void StopSequencerBackend();

	/** Mirror the sequence player's position into step state so the UI reads the same values. */
	void TickSequencerBackend();

	/** True when the index addresses a step of the loaded preset. */
	bool IsValidStepIndex(int32 StepIndex) const;

	// --- Enhanced Input ---------------------------------------------------

	/**
	 * Push the plugin's mapping context and bind its actions on the tour controller.
	 * Does nothing unless UTourRuntimeSettings::bEnableDefaultInput is set: a plugin that
	 * silently claims Space and the arrow keys collides with the host project.
	 */
	void SetupTourInput();

	/** Remove the mapping context and every binding made by SetupTourInput. */
	void TeardownTourInput();

	void HandleTogglePauseInput();
	void HandleNextStepInput();
	void HandlePreviousStepInput();
	void HandleStopTourInput();
	void HandleRestartTourInput();

	/** Input config resolved from settings, kept alive for as long as the bindings exist. */
	UPROPERTY(Transient)
	TObjectPtr<UTourInputConfig> ActiveInputConfig;

	/** Binding handles produced by UEnhancedInputComponent, so they can be removed exactly. */
	TArray<uint32> InputBindingHandles;

	/** Controller the input was installed on, so teardown targets the right one. */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> InputBoundController;
};
