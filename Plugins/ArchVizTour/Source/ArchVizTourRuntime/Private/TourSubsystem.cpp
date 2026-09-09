// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourSubsystem.h"

#include "ArchVizTourLog.h"
#include "CineCameraActor.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"   // also declares FGameModeEvents
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/App.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "MovieSceneSequencePlayer.h"
#include "TourCameraRig.h"
#include "TourGeometryLibrary.h"
#include "TourPath.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "TourInputConfig.h"
#include "TourPathPreset.h"
#include "TourRuntimeSettings.h"
#include "TourSequencePreset.h"

#define LOCTEXT_NAMESPACE "ArchVizTour"

namespace ArchVizTour::SubsystemPrivate
{
	/**
	 * Shortest step the transport will accept, in seconds.
	 * A zero-duration step is an authoring mistake rather than an intent to skip: clamping to a
	 * single frame at 60 Hz keeps its camera cut visible and keeps every alpha division finite.
	 */
	static constexpr float MinStepDuration = 1.0f / 60.0f;

	/** Guards every division by a duration or a length. */
	static constexpr float MinDivisor = UE_KINDA_SMALL_NUMBER;
}

// ---------------------------------------------------------------------------
// USubsystem
// ---------------------------------------------------------------------------

bool UTourSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (World == nullptr)
	{
		return false;
	}

	// Game, PIE and the editor world all need it: the editor world so a path can be previewed
	// without entering play. Preview and inactive worlds (thumbnails, asset editors) do not.
	switch (World->WorldType)
	{
	case EWorldType::Game:
	case EWorldType::PIE:
	case EWorldType::Editor:
		return true;
	default:
		return false;
	}
}

void UTourSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CameraRigClass = ATourCameraRig::StaticClass();
	bUseUnpausedDeltaTime = UTourRuntimeSettings::Get().bUseUnpausedDeltaTime;

	// A tour can be loaded before any player exists - on a dedicated listen host, or simply
	// because a Blueprint ran LoadTour in BeginPlay before the controller was created. Binding
	// here means the tour picks up the controller the moment it appears instead of silently
	// never taking the view.
	PostLoginHandle = FGameModeEvents::OnGameModePostLoginEvent().AddUObject(this, &UTourSubsystem::HandleGameModePostLogin);
}

void UTourSubsystem::Deinitialize()
{
	if (PostLoginHandle.IsValid())
	{
		FGameModeEvents::OnGameModePostLoginEvent().Remove(PostLoginHandle);
		PostLoginHandle.Reset();
	}

	if (StreamingHandle.IsValid())
	{
		StreamingHandle->CancelHandle();
		StreamingHandle.Reset();
	}

	StopSequencerBackend();
	TeardownTourInput();

	// The world is going away, so no view target restore and no delegate broadcast: anything
	// listening is being torn down in the same pass.
	State = ETourState::Idle;
	LoadedPreset = nullptr;
	ResolvedSteps.Reset();
	CurrentStepIndex = INDEX_NONE;

	// Actors must not be destroyed once the world is tearing down; the world disposes of them
	// itself, and touching them here is a use-after-free waiting to happen.
	const UWorld* World = GetWorld();
	if (World != nullptr && !World->bIsTearingDown)
	{
		ClearRuntimePaths();

		if (ATourCameraRig* Rig = CameraRig.Get())
		{
			Rig->Destroy();
		}
	}

	RuntimePaths.Reset();
	CameraRig.Reset();

	Super::Deinitialize();
}

UTourSubsystem* UTourSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World != nullptr ? World->GetSubsystem<UTourSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// FTickableGameObject
// ---------------------------------------------------------------------------

bool UTourSubsystem::IsTickable() const
{
	// Ticking while merely loaded (not playing) costs one branch and keeps the controller
	// re-acquisition path alive for a tour that was loaded before the player existed.
	return !IsTemplate() && LoadedPreset != nullptr;
}

TStatId UTourSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTourSubsystem, STATGROUP_Tickables);
}

void UTourSubsystem::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ArchVizTour_SubsystemTick);

	Super::Tick(DeltaTime);

	if (State != ETourState::Playing && State != ETourState::Blending)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr || World->bIsTearingDown)
	{
		// World teardown can land between a blend starting and this tick; bail rather than
		// dereference a half-destroyed world.
		return;
	}

	// Belt and braces alongside the post-login delegate: a controller that already existed when
	// the tour was loaded never fires a login event at all, and a PIE client can be created
	// outside the game mode path. Re-acquiring here costs one weak-pointer test per frame.
	if (!CachedController.IsValid() && GetTourController() != nullptr && IsValidStepIndex(CurrentStepIndex))
	{
		BeginStep(CurrentStepIndex, /*bFromStart*/ false);
	}

	if (ActiveBackend == ETourPlaybackBackend::Sequencer)
	{
		TickSequencerBackend();
		return;
	}

	// The tick delta argument is deliberately ignored: it is the tickable manager's delta,
	// which carries neither the world's time dilation nor the bUseUnpausedDeltaTime choice.
	const float TourDelta = ComputeTourDeltaSeconds();
	if (TourDelta <= 0.0f)
	{
		return;
	}

	if (BlendRemaining > 0.0f)
	{
		BlendRemaining = FMath::Max(0.0f, BlendRemaining - TourDelta);
		if (BlendRemaining <= 0.0f && State == ETourState::Blending)
		{
			SetState(ETourState::Playing);
		}
	}

	const float GlobalScale = (LoadedPreset != nullptr)
		? FMath::Max(LoadedPreset->GlobalTimeScale, ArchVizTour::SubsystemPrivate::MinDivisor)
		: 1.0f;

	const float ScaledDelta = TourDelta * TimeScale * GlobalScale * PlaybackDirection;

	StepElapsed += ScaledDelta;

	// A loop, not an if: at a high time scale, or after a long hitch, a single frame can cross
	// several short steps, and collapsing them into one transition would drop the intermediate
	// steps' custom event tags. The guard bounds it at one pass over the tour so a zero-length
	// step cannot spin here.
	int32 Guard = 0;
	const int32 MaxTransitionsPerTick = FMath::Max(GetStepCount(), 1) + 1;

	while (IsValidStepIndex(CurrentStepIndex) && Guard++ < MaxTransitionsPerTick)
	{
		if (State != ETourState::Playing && State != ETourState::Blending)
		{
			// A step with bPauseAtEnd, or the end of the tour, ends the transition walk.
			break;
		}

		const float StepDuration = FMath::Max(ResolvedSteps[CurrentStepIndex].Duration, ArchVizTour::SubsystemPrivate::MinStepDuration);

		if (PlaybackDirection > 0.0f && StepElapsed >= StepDuration)
		{
			// Carry the overshoot into the next step *after* entering it: BeginStep resets the
			// elapsed time, so subtracting first would throw the remainder away and make
			// playback drift slower than real time on every transition.
			const float Overshoot = StepElapsed - StepDuration;
			AdvanceStepForward();

			if (State != ETourState::Playing && State != ETourState::Blending)
			{
				break;
			}

			StepElapsed += Overshoot;
			continue;
		}

		if (PlaybackDirection < 0.0f && StepElapsed < 0.0f)
		{
			const float Undershoot = -StepElapsed;
			AdvanceStepBackward();

			if (State != ETourState::Playing && State != ETourState::Blending)
			{
				break;
			}

			StepElapsed -= Undershoot;
			continue;
		}

		break;
	}

	if (State != ETourState::Playing && State != ETourState::Blending)
	{
		return;
	}

	EvaluateCurrentStep(TourDelta);

	OnTourProgress.Broadcast(GetTourProgress(), GetStepProgress());
}

float UTourSubsystem::ComputeTourDeltaSeconds() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0.0f;
	}

	if (bUseUnpausedDeltaTime)
	{
		// FApp's delta is the real frame time: unaffected by both the pause flag and
		// AWorldSettings time dilation, which is exactly what "keep running regardless" means.
		return static_cast<float>(FApp::GetDeltaTime());
	}

	if (World->IsPaused())
	{
		return 0.0f;
	}

	// UWorld::GetDeltaSeconds() is already multiplied by the world settings' effective time
	// dilation, so slow-motion and a Matinee-style global slowdown apply to the tour for free.
	return World->GetDeltaSeconds();
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool UTourSubsystem::LoadTour(UTourSequencePreset* Preset)
{
	StopTour();

	if (StreamingHandle.IsValid())
	{
		StreamingHandle->CancelHandle();
		StreamingHandle.Reset();
	}

	if (Preset == nullptr)
	{
		LoadedPreset = nullptr;
		ResolvedSteps.Reset();
		CurrentStepIndex = INDEX_NONE;
		TotalDuration = 0.0f;
		UE_LOG(LogArchVizTour, Log, TEXT("Tour cleared."));
		OnPresetLoaded.Broadcast(nullptr);
		return false;
	}

	if (!Preset->IsPlayable())
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour preset '%s' has no steps; refusing to load it."), *Preset->GetName());
		return false;
	}

	// Path presets are soft references so an unplayed tour costs nothing resident. Streaming
	// them before the first step avoids a hitch on the opening cut of the tour.
	TArray<FSoftObjectPath> PathsToLoad;
	PathsToLoad.Reserve(Preset->ReferencedPaths.Num());
	for (const TSoftObjectPtr<UTourPathPreset>& SoftPath : Preset->ReferencedPaths)
	{
		if (!SoftPath.IsNull() && !SoftPath.IsValid())
		{
			PathsToLoad.Add(SoftPath.ToSoftObjectPath());
		}
	}

	if (PathsToLoad.Num() > 0)
	{
		TWeakObjectPtr<UTourSubsystem> WeakThis(this);
		TWeakObjectPtr<UTourSequencePreset> WeakPreset(Preset);

		StreamingHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			PathsToLoad,
			FStreamableDelegate::CreateLambda([WeakThis, WeakPreset]()
			{
				// Both the subsystem and the preset can be gone by the time the load lands, for
				// example if the level was unloaded mid-stream.
				UTourSubsystem* Subsystem = WeakThis.Get();
				UTourSequencePreset* StreamedPreset = WeakPreset.Get();

				if (Subsystem != nullptr && StreamedPreset != nullptr && Subsystem->LoadedPreset == StreamedPreset)
				{
					// Re-resolve rather than re-finalise: the tour is already installed and the
					// UI has already been told about it, so broadcasting OnPresetLoaded a second
					// time would make every listener rebuild itself for nothing.
					Subsystem->RebuildResolvedSteps();
				}
			}));

		// The tour is installed immediately so the UI can populate; the streamed paths only
		// matter once a spline step is entered, and step resolution is retried on entry anyway.
	}

	FinalizeLoadTour(Preset);
	return true;
}

void UTourSubsystem::FinalizeLoadTour(UTourSequencePreset* Preset)
{
	check(Preset != nullptr);

	LoadedPreset = Preset;
	CurrentStepIndex = 0;
	StepElapsed = 0.0f;
	BlendRemaining = 0.0f;
	PlaybackDirection = 1.0f;

	ActiveBackend = Preset->PlaybackBackend;

	RebuildResolvedSteps();

	SetState(ETourState::Idle);

	UE_LOG(LogArchVizTour, Log, TEXT("Loaded tour '%s': %d steps, %.2f s."),
		*Preset->GetName(), Preset->Steps.Num(), TotalDuration);

	OnPresetLoaded.Broadcast(Preset);
	OnStepChanged.Broadcast(CurrentStepIndex, GetCurrentStepLabel());
}

void UTourSubsystem::LoadTourAsync(TSoftObjectPtr<UTourSequencePreset> PresetPath)
{
	if (PresetPath.IsNull())
	{
		LoadTour(nullptr);
		return;
	}

	if (UTourSequencePreset* Resident = PresetPath.Get())
	{
		LoadTour(Resident);
		return;
	}

	if (StreamingHandle.IsValid())
	{
		StreamingHandle->CancelHandle();
		StreamingHandle.Reset();
	}

	TWeakObjectPtr<UTourSubsystem> WeakThis(this);
	StreamingHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		PresetPath.ToSoftObjectPath(),
		FStreamableDelegate::CreateLambda([WeakThis, PresetPath]()
		{
			if (UTourSubsystem* Subsystem = WeakThis.Get())
			{
				Subsystem->HandleTourPresetStreamed(PresetPath);
			}
		}));
}

void UTourSubsystem::HandleTourPresetStreamed(TSoftObjectPtr<UTourSequencePreset> PresetPath)
{
	UTourSequencePreset* Preset = PresetPath.Get();
	if (Preset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Async load of tour preset '%s' completed but the asset is not resident."),
			*PresetPath.ToString());
		return;
	}

	LoadTour(Preset);
}

// ---------------------------------------------------------------------------
// Step resolution
// ---------------------------------------------------------------------------

void UTourSubsystem::RebuildResolvedSteps()
{
	ResolvedSteps.Reset();
	TotalDuration = 0.0f;

	if (LoadedPreset == nullptr)
	{
		return;
	}

	ResolvedSteps.SetNum(LoadedPreset->Steps.Num());

	float AccumulatedTime = 0.0f;
	for (int32 Index = 0; Index < ResolvedSteps.Num(); ++Index)
	{
		ResolveStep(Index);

		ResolvedSteps[Index].StartTime = AccumulatedTime;
		AccumulatedTime += FMath::Max(ResolvedSteps[Index].Duration, ArchVizTour::SubsystemPrivate::MinStepDuration);
	}

	TotalDuration = AccumulatedTime;
}

bool UTourSubsystem::ResolveStep(int32 StepIndex)
{
	if (LoadedPreset == nullptr || !ResolvedSteps.IsValidIndex(StepIndex) || !LoadedPreset->Steps.IsValidIndex(StepIndex))
	{
		return false;
	}

	const FTourStep& Step = LoadedPreset->Steps[StepIndex];
	FTourResolvedStep& Resolved = ResolvedSteps[StepIndex];

	Resolved.bResolved = false;

	switch (Step.StepType)
	{
	case ETourStepType::SplineMove:
	{
		ATourPath* Path = FindTourPath(Step.SplinePathRef);
		Resolved.Path = Path;

		if (Path == nullptr || !Path->IsTraversable())
		{
			if (!Resolved.bReportedUnresolved)
			{
				Resolved.bReportedUnresolved = true;
				UE_LOG(LogArchVizTour, Warning,
					TEXT("Tour step %d ('%s') references tour path '%s', which is missing or has fewer than two points. The step will be skipped."),
					StepIndex, *Step.Label.ToString(), *Step.SplinePathRef.ToString());
			}
			Resolved.Duration = FMath::Max(Step.Duration, 0.0f);
			return false;
		}

		const float PathLength = Path->GetPathLength();
		Resolved.StartDistance = FMath::Clamp(Path->GetDistanceAtInputKey(Step.StartInputKey), 0.0f, PathLength);

		// An end key at or below the start key means "run to the end of the spline", which is
		// what a step authored before sub-ranges existed expects.
		Resolved.EndDistance = (Step.EndInputKey > Step.StartInputKey)
			? FMath::Clamp(Path->GetDistanceAtInputKey(Step.EndInputKey), 0.0f, PathLength)
			: PathLength;

		Resolved.bResolved = true;
		Resolved.bReportedUnresolved = false;
		break;
	}

	case ETourStepType::StaticCamera:
	{
		ACineCameraActor* Camera = FindStaticCamera(Step.StaticCameraRef);
		Resolved.StaticCamera = Camera;

		if (Camera == nullptr)
		{
			if (!Resolved.bReportedUnresolved)
			{
				Resolved.bReportedUnresolved = true;
				UE_LOG(LogArchVizTour, Warning,
					TEXT("Tour step %d ('%s') references cine camera '%s', which is not in the level. The step will be skipped."),
					StepIndex, *Step.Label.ToString(), *Step.StaticCameraRef.ToString());
			}
			Resolved.Duration = FMath::Max(Step.Duration, 0.0f);
			return false;
		}

		Resolved.bResolved = true;
		Resolved.bReportedUnresolved = false;
		break;
	}

	case ETourStepType::Dwell:
	case ETourStepType::Custom:
		// Nothing to look up; these steps hold whatever the previous one left on screen.
		Resolved.bResolved = true;
		break;

	default:
		break;
	}

	if (Step.StepType == ETourStepType::SplineMove && Resolved.bResolved)
	{
		// Spline steps need the dwell/ease decomposition, which also produces their duration.
		BuildSplineStepTimeline(StepIndex);
	}
	else
	{
		Resolved.Duration = ResolveStepDuration(StepIndex);
	}

	return Resolved.bResolved;
}

float UTourSubsystem::ResolveStepDuration(int32 StepIndex) const
{
	using namespace ArchVizTour::SubsystemPrivate;

	if (LoadedPreset == nullptr || !LoadedPreset->Steps.IsValidIndex(StepIndex))
	{
		return MinStepDuration;
	}

	const FTourStep& Step = LoadedPreset->Steps[StepIndex];

	if (Step.Duration > 0.0f)
	{
		return Step.Duration;
	}

	// A spline step with no authored duration derives one from the authored speed, which is the
	// whole point of storing speed in cm/s: the move takes as long as the geometry says.
	if (Step.StepType == ETourStepType::SplineMove && ResolvedSteps.IsValidIndex(StepIndex))
	{
		const FTourResolvedStep& Resolved = ResolvedSteps[StepIndex];
		if (const ATourPath* Path = Resolved.Path.Get())
		{
			const float Distance = FMath::Abs(Resolved.EndDistance - Resolved.StartDistance);
			const float MidDistance = (Resolved.StartDistance + Resolved.EndDistance) * 0.5f;
			const float Speed = FMath::Max(Path->GetSpeedAtDistance(MidDistance), MinDivisor);
			return FMath::Max(Distance / Speed, MinStepDuration);
		}
	}

	UE_LOG(LogArchVizTour, Warning,
		TEXT("Tour step %d ('%s') has a duration of %.3f s, which is not playable. Clamping to %.4f s."),
		StepIndex, *Step.Label.ToString(), Step.Duration, MinStepDuration);

	return MinStepDuration;
}

bool UTourSubsystem::IsValidStepIndex(int32 StepIndex) const
{
	return LoadedPreset != nullptr
		&& LoadedPreset->Steps.IsValidIndex(StepIndex)
		&& ResolvedSteps.IsValidIndex(StepIndex);
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void UTourSubsystem::SetState(ETourState NewState)
{
	if (State == NewState)
	{
		return;
	}

	const ETourState OldState = State;
	State = NewState;
	OnTourStateChanged.Broadcast(OldState, NewState);
}

void UTourSubsystem::PlayTour()
{
	if (LoadedPreset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("PlayTour called with no tour loaded."));
		return;
	}

	if (!LoadedPreset->IsPlayable())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("PlayTour: tour '%s' has no steps."), *LoadedPreset->GetName());
		return;
	}

	if (State == ETourState::Paused)
	{
		SetPaused(false);
		return;
	}

	if (State == ETourState::Playing || State == ETourState::Blending)
	{
		return;
	}

	// Remember where the player was looking so StopTour can hand the view back rather than
	// leaving them staring through an abandoned rig.
	if (const APlayerController* Controller = GetTourController())
	{
		PreTourViewTarget = Controller->GetViewTarget();
	}

	// Captured before SetState, because a replay of a finished tour has to restart from the top
	// and the state is about to become Playing.
	const bool bRestartFromTop = (State == ETourState::Finished) || !IsValidStepIndex(CurrentStepIndex);

	if (bRestartFromTop)
	{
		CurrentStepIndex = 0;
		StepElapsed = 0.0f;
	}

	if (ActiveBackend == ETourPlaybackBackend::Sequencer && !StartSequencerBackend())
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour '%s' requested the Level Sequence backend but has no baked sequence; falling back to procedural playback."),
			*LoadedPreset->GetName());
		ActiveBackend = ETourPlaybackBackend::Procedural;
	}

	SetupTourInput();

	SetState(ETourState::Playing);
	BeginStep(FMath::Max(CurrentStepIndex, 0), bRestartFromTop);
}

void UTourSubsystem::StopTour()
{
	const bool bWasRunning = (State == ETourState::Playing || State == ETourState::Paused || State == ETourState::Blending);

	StopSequencerBackend();
	TeardownTourInput();

	if (ATourCameraRig* Rig = CameraRig.Get())
	{
		Rig->StopCameraShake(GetTourController(), /*bImmediately*/ true);
		Rig->ResetSmoothing();
	}

	RestorePreTourViewTarget();

	StepElapsed = 0.0f;
	BlendRemaining = 0.0f;
	CurrentStepIndex = (LoadedPreset != nullptr && LoadedPreset->IsPlayable()) ? 0 : INDEX_NONE;

	SetState(ETourState::Idle);

	if (bWasRunning)
	{
		OnTourFinished.Broadcast(/*bWasInterrupted*/ true);
	}
}

void UTourSubsystem::SetPaused(bool bPaused)
{
	if (bPaused)
	{
		if (State == ETourState::Playing || State == ETourState::Blending)
		{
			if (SequencePlayer != nullptr)
			{
				SequencePlayer->Pause();
			}
			SetState(ETourState::Paused);
		}
		return;
	}

	if (State == ETourState::Paused)
	{
		if (SequencePlayer != nullptr)
		{
			SequencePlayer->Play();
		}

		// Resuming after any gap must not let the smoothing filter ease out of a stale pose.
		if (ATourCameraRig* Rig = CameraRig.Get())
		{
			Rig->ResetSmoothing();
		}

		SetState(BlendRemaining > 0.0f ? ETourState::Blending : ETourState::Playing);
	}
}

void UTourSubsystem::TogglePause()
{
	switch (State)
	{
	case ETourState::Playing:
	case ETourState::Blending:
		SetPaused(true);
		break;

	case ETourState::Paused:
		SetPaused(false);
		break;

	default:
		break;
	}
}

void UTourSubsystem::NextStep()
{
	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable())
	{
		return;
	}

	const int32 NextIndex = CurrentStepIndex + 1;

	if (!IsValidStepIndex(NextIndex))
	{
		if (LoadedPreset->bLoopTour)
		{
			JumpToStep(0);
			return;
		}

		// At the last step Next completes the tour rather than doing nothing: the transport
		// should always leave the user somewhere new.
		FinishTour();
		return;
	}

	JumpToStep(NextIndex);
}

void UTourSubsystem::PreviousStep()
{
	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable())
	{
		return;
	}

	// Media-transport convention: a Previous press well into a step restarts that step, and
	// only a second press within the threshold actually goes back one.
	if (StepElapsed > RestartStepThreshold)
	{
		JumpToStep(CurrentStepIndex);
		return;
	}

	const int32 PreviousIndex = CurrentStepIndex - 1;

	if (!IsValidStepIndex(PreviousIndex))
	{
		if (LoadedPreset->bLoopTour)
		{
			JumpToStep(GetStepCount() - 1);
			return;
		}

		// At the first step, Previous restarts it. Clamping is what the user expects; wrapping
		// to the end of a non-looping tour is not.
		JumpToStep(0);
		return;
	}

	JumpToStep(PreviousIndex);
}

void UTourSubsystem::JumpToStep(int32 StepIndex)
{
	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("JumpToStep(%d) called with no playable tour loaded."), StepIndex);
		return;
	}

	const int32 ClampedIndex = FMath::Clamp(StepIndex, 0, GetStepCount() - 1);
	if (ClampedIndex != StepIndex)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("JumpToStep(%d) is out of range for a tour with %d steps; clamped to %d."),
			StepIndex, GetStepCount(), ClampedIndex);
	}

	if (State == ETourState::Finished || State == ETourState::Idle)
	{
		// Jumping is an implicit play request: a transport that silently moves the playhead
		// without resuming reads as broken.
		CurrentStepIndex = ClampedIndex;
		PlayTour();
		return;
	}

	BeginStep(ClampedIndex, /*bFromStart*/ true);
}

void UTourSubsystem::RestartTour()
{
	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable())
	{
		return;
	}

	CurrentStepIndex = 0;
	StepElapsed = 0.0f;
	PlaybackDirection = 1.0f;

	if (State == ETourState::Idle || State == ETourState::Finished)
	{
		PlayTour();
		return;
	}

	BeginStep(0, /*bFromStart*/ true);
}

void UTourSubsystem::SetTimeScale(float NewTimeScale)
{
	if (NewTimeScale <= 0.0f)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("SetTimeScale(%.3f): the time scale must be positive. Use SetPlaybackDirection to play backwards."),
			NewTimeScale);
		return;
	}

	TimeScale = NewTimeScale;

	if (SequencePlayer != nullptr)
	{
		SequencePlayer->SetPlayRate(TimeScale * PlaybackDirection);
	}
}

void UTourSubsystem::SetPlaybackDirection(float Direction)
{
	if (FMath::IsNearlyZero(Direction))
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SetPlaybackDirection(0) is not a direction; ignoring. Use SetPaused to stop."));
		return;
	}

	const float NewDirection = (Direction > 0.0f) ? 1.0f : -1.0f;
	if (FMath::IsNearlyEqual(NewDirection, PlaybackDirection))
	{
		return;
	}

	PlaybackDirection = NewDirection;

	// Reversing mid-step: the elapsed time has to be mirrored inside the step, otherwise the
	// playhead jumps to the far end of it on the next tick.
	if (IsValidStepIndex(CurrentStepIndex))
	{
		const float StepDuration = FMath::Max(ResolvedSteps[CurrentStepIndex].Duration, ArchVizTour::SubsystemPrivate::MinStepDuration);
		StepElapsed = FMath::Clamp(StepElapsed, 0.0f, StepDuration);
	}

	if (SequencePlayer != nullptr)
	{
		SequencePlayer->SetPlayRate(TimeScale * PlaybackDirection);
	}

	if (ATourCameraRig* Rig = CameraRig.Get())
	{
		Rig->ResetSmoothing();
	}
}

void UTourSubsystem::ScrubToAlpha(float Alpha)
{
	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable() || TotalDuration <= ArchVizTour::SubsystemPrivate::MinDivisor)
	{
		return;
	}

	const float TargetTime = FMath::Clamp(Alpha, 0.0f, 1.0f) * TotalDuration;

	int32 TargetStep = 0;
	for (int32 Index = ResolvedSteps.Num() - 1; Index >= 0; --Index)
	{
		if (TargetTime >= ResolvedSteps[Index].StartTime)
		{
			TargetStep = Index;
			break;
		}
	}

	const bool bStepChanged = (TargetStep != CurrentStepIndex);

	CurrentStepIndex = TargetStep;
	StepElapsed = FMath::Clamp(
		TargetTime - ResolvedSteps[TargetStep].StartTime,
		0.0f,
		FMath::Max(ResolvedSteps[TargetStep].Duration, ArchVizTour::SubsystemPrivate::MinStepDuration));

	if (SequencePlayer != nullptr)
	{
		SequencePlayer->SetPlaybackPosition(
			FMovieSceneSequencePlaybackParams(TargetTime, EUpdatePositionMethod::Scrub));
	}

	if (bStepChanged)
	{
		// Take the new step's view target, but without resetting its elapsed time - the scrub
		// already decided where inside the step the playhead is.
		BeginStep(TargetStep, /*bFromStart*/ false);
	}
	else
	{
		// Scrubbing is a discontinuity: smoothing must not ease across it.
		if (ATourCameraRig* Rig = CameraRig.Get())
		{
			Rig->ResetSmoothing();
		}
		EvaluateCurrentStep(0.0f);
	}

	OnTourProgress.Broadcast(GetTourProgress(), GetStepProgress());
}

void UTourSubsystem::FinishTour()
{
	StopSequencerBackend();
	TeardownTourInput();

	if (ATourCameraRig* Rig = CameraRig.Get())
	{
		Rig->StopCameraShake(GetTourController(), /*bImmediately*/ false);
	}

	// The playhead is left parked on the final step rather than reset, so a Finished tour still
	// shows its last frame and its last label.
	CurrentStepIndex = FMath::Max(GetStepCount() - 1, 0);
	if (IsValidStepIndex(CurrentStepIndex))
	{
		StepElapsed = FMath::Max(ResolvedSteps[CurrentStepIndex].Duration, ArchVizTour::SubsystemPrivate::MinStepDuration);
	}

	SetState(ETourState::Finished);
	OnTourFinished.Broadcast(/*bWasInterrupted*/ false);
}

// ---------------------------------------------------------------------------
// Spline step timeline
// ---------------------------------------------------------------------------

void UTourSubsystem::BuildSplineStepTimeline(int32 StepIndex)
{
	using namespace ArchVizTour::SubsystemPrivate;

	check(ResolvedSteps.IsValidIndex(StepIndex));
	check(LoadedPreset != nullptr && LoadedPreset->Steps.IsValidIndex(StepIndex));

	FTourResolvedStep& Resolved = ResolvedSteps[StepIndex];
	const FTourStep& Step = LoadedPreset->Steps[StepIndex];

	Resolved.SegmentBoundaries.Reset();
	Resolved.SegmentDurations.Reset();
	Resolved.BoundaryDwells.Reset();
	Resolved.BoundaryEaseIn.Reset();
	Resolved.BoundaryEaseOut.Reset();

	const ATourPath* Path = Resolved.Path.Get();
	if (Path == nullptr)
	{
		Resolved.Duration = FMath::Max(Step.Duration, MinStepDuration);
		return;
	}

	const float StartDistance = Resolved.StartDistance;
	const float EndDistance   = Resolved.EndDistance;
	const float SpanLength    = FMath::Abs(EndDistance - StartDistance);
	const float Direction     = (EndDistance >= StartDistance) ? 1.0f : -1.0f;

	// Collect the authored points that lie strictly inside the traversed range. Only those
	// contribute a boundary; the range's own endpoints are always boundaries.
	struct FBoundaryCandidate
	{
		float Distance = 0.0f;
		float Dwell = 0.0f;
		float EaseIn = 0.0f;
		float EaseOut = 0.0f;
	};

	TArray<FBoundaryCandidate> Boundaries;
	Boundaries.Reserve(Path->Points.Num() + 2);

	auto MakeBoundaryAtDistance = [Path](float Distance) -> FBoundaryCandidate
	{
		FBoundaryCandidate Candidate;
		Candidate.Distance = Distance;

		// Endpoints usually fall between authored points, so their easing comes from the nearer
		// one rather than being invented.
		if (Path->Points.Num() > 0)
		{
			const float Key = Path->GetPathLength() > UE_KINDA_SMALL_NUMBER
				? static_cast<float>(Path->Points.Num() - 1) * (Distance / Path->GetPathLength())
				: 0.0f;
			const int32 Nearest = FMath::Clamp(FMath::RoundToInt(Key), 0, Path->Points.Num() - 1);
			Candidate.EaseIn  = Path->Points[Nearest].EaseIn;
			Candidate.EaseOut = Path->Points[Nearest].EaseOut;
		}

		return Candidate;
	};

	Boundaries.Add(MakeBoundaryAtDistance(StartDistance));

	for (int32 PointIndex = 0; PointIndex < Path->Points.Num(); ++PointIndex)
	{
		const FTourPoint& Point = Path->Points[PointIndex];
		if (Point.DwellTime <= 0.0f)
		{
			continue;
		}

		const float PointDistance = Path->GetDistanceAtInputKey(static_cast<float>(PointIndex));

		const float Low  = FMath::Min(StartDistance, EndDistance);
		const float High = FMath::Max(StartDistance, EndDistance);
		if (PointDistance <= Low + UE_KINDA_SMALL_NUMBER || PointDistance >= High - UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		FBoundaryCandidate Candidate;
		Candidate.Distance = PointDistance;
		Candidate.Dwell    = Point.DwellTime;
		Candidate.EaseIn   = Point.EaseIn;
		Candidate.EaseOut  = Point.EaseOut;
		Boundaries.Add(Candidate);
	}

	Boundaries.Add(MakeBoundaryAtDistance(EndDistance));

	// Sort into traversal order, which is descending when the step runs the path backwards.
	Boundaries.Sort([Direction](const FBoundaryCandidate& A, const FBoundaryCandidate& B)
	{
		return Direction > 0.0f ? (A.Distance < B.Distance) : (A.Distance > B.Distance);
	});

	// Step.Duration is the time spent moving; dwells are added on top, so adding a hold never
	// silently speeds the rest of the move up to compensate.
	float MoveDuration = Step.Duration;
	if (MoveDuration <= 0.0f)
	{
		const float MidDistance = (StartDistance + EndDistance) * 0.5f;
		const float Speed = FMath::Max(Path->GetSpeedAtDistance(MidDistance), MinDivisor);
		MoveDuration = SpanLength / Speed;
	}
	MoveDuration = FMath::Max(MoveDuration, MinStepDuration);

	float TotalDwell = 0.0f;

	Resolved.SegmentBoundaries.Reserve(Boundaries.Num());
	Resolved.BoundaryDwells.Reserve(Boundaries.Num());
	Resolved.BoundaryEaseIn.Reserve(Boundaries.Num());
	Resolved.BoundaryEaseOut.Reserve(Boundaries.Num());

	for (const FBoundaryCandidate& Candidate : Boundaries)
	{
		Resolved.SegmentBoundaries.Add(Candidate.Distance);
		Resolved.BoundaryDwells.Add(Candidate.Dwell);
		Resolved.BoundaryEaseIn.Add(Candidate.EaseIn);
		Resolved.BoundaryEaseOut.Add(Candidate.EaseOut);
		TotalDwell += Candidate.Dwell;
	}

	const int32 SegmentCount = Resolved.SegmentBoundaries.Num() - 1;
	Resolved.SegmentDurations.Reserve(FMath::Max(SegmentCount, 0));

	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		const float SegmentLength = FMath::Abs(Resolved.SegmentBoundaries[Index + 1] - Resolved.SegmentBoundaries[Index]);
		// Each sub-move takes its share of the move budget in proportion to its arc length,
		// which is what keeps cm/s constant across a step that contains holds.
		const float SegmentDuration = (SpanLength > MinDivisor)
			? MoveDuration * (SegmentLength / SpanLength)
			: MoveDuration / static_cast<float>(FMath::Max(SegmentCount, 1));

		Resolved.SegmentDurations.Add(FMath::Max(SegmentDuration, 0.0f));
	}

	Resolved.Duration = FMath::Max(MoveDuration + TotalDwell, MinStepDuration);
}

float UTourSubsystem::ComputeSplineDistance(const FTourResolvedStep& Resolved, float Elapsed)
{
	if (Resolved.SegmentBoundaries.Num() == 0)
	{
		return Resolved.StartDistance;
	}

	if (Resolved.SegmentBoundaries.Num() == 1)
	{
		return Resolved.SegmentBoundaries[0];
	}

	float Remaining = FMath::Max(Elapsed, 0.0f);

	// Dwell at the first boundary, before any movement.
	if (Remaining < Resolved.BoundaryDwells[0])
	{
		return Resolved.SegmentBoundaries[0];
	}
	Remaining -= Resolved.BoundaryDwells[0];

	const int32 SegmentCount = Resolved.SegmentDurations.Num();
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		const float SegmentDuration = Resolved.SegmentDurations[Index];

		if (SegmentDuration > UE_KINDA_SMALL_NUMBER && Remaining < SegmentDuration)
		{
			const float RawAlpha = Remaining / SegmentDuration;
			const float EasedAlpha = UTourGeometryLibrary::EvaluateEase(
				RawAlpha,
				Resolved.BoundaryEaseIn.IsValidIndex(Index) ? Resolved.BoundaryEaseIn[Index] : 0.0f,
				Resolved.BoundaryEaseOut.IsValidIndex(Index + 1) ? Resolved.BoundaryEaseOut[Index + 1] : 0.0f);

			return FMath::Lerp(Resolved.SegmentBoundaries[Index], Resolved.SegmentBoundaries[Index + 1], EasedAlpha);
		}

		Remaining -= SegmentDuration;

		const float Dwell = Resolved.BoundaryDwells.IsValidIndex(Index + 1) ? Resolved.BoundaryDwells[Index + 1] : 0.0f;
		if (Remaining < Dwell)
		{
			return Resolved.SegmentBoundaries[Index + 1];
		}
		Remaining -= Dwell;
	}

	return Resolved.SegmentBoundaries.Last();
}

// ---------------------------------------------------------------------------
// Step lifecycle
// ---------------------------------------------------------------------------

void UTourSubsystem::BeginStep(int32 StepIndex, bool bFromStart)
{
	using namespace ArchVizTour::SubsystemPrivate;

	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable())
	{
		return;
	}

	const int32 StepCount = GetStepCount();

	// Walk over steps that cannot be resolved rather than stalling on them. The guard bounds the
	// walk at one full pass, so a tour whose references are all missing finishes instead of
	// spinning.
	int32 Candidate = FMath::Clamp(StepIndex, 0, StepCount - 1);
	for (int32 Attempt = 0; Attempt < StepCount; ++Attempt)
	{
		if (!IsValidStepIndex(Candidate))
		{
			break;
		}

		// Retry resolution every time a step is entered: a path or camera can stream in, or be
		// spawned by RegisterRuntimePath, long after the tour was loaded.
		if (!ResolvedSteps[Candidate].bResolved)
		{
			ResolveStep(Candidate);
		}

		if (ResolvedSteps[Candidate].bResolved)
		{
			break;
		}

		Candidate += (PlaybackDirection >= 0.0f) ? 1 : -1;

		if (Candidate < 0 || Candidate >= StepCount)
		{
			if (LoadedPreset->bLoopTour)
			{
				Candidate = (Candidate < 0) ? StepCount - 1 : 0;
			}
			else
			{
				UE_LOG(LogArchVizTour, Warning,
					TEXT("Tour '%s': ran out of playable steps while skipping unresolvable ones."),
					*LoadedPreset->GetName());
				FinishTour();
				return;
			}
		}
	}

	if (!IsValidStepIndex(Candidate) || !ResolvedSteps[Candidate].bResolved)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour '%s': no step could be resolved; stopping."), *LoadedPreset->GetName());
		FinishTour();
		return;
	}

	const bool bIndexChanged = (CurrentStepIndex != Candidate);
	CurrentStepIndex = Candidate;

	const FTourStep& Step = LoadedPreset->Steps[CurrentStepIndex];
	FTourResolvedStep& Resolved = ResolvedSteps[CurrentStepIndex];

	// Durations depend on resolved geometry, so they are refreshed on entry rather than trusted
	// from load time - the path may have been edited, or only just spawned.
	if (Step.StepType == ETourStepType::SplineMove)
	{
		BuildSplineStepTimeline(CurrentStepIndex);
	}
	else
	{
		Resolved.Duration = ResolveStepDuration(CurrentStepIndex);
	}

	if (bFromStart)
	{
		StepElapsed = (PlaybackDirection >= 0.0f)
			? 0.0f
			: FMath::Max(Resolved.Duration, MinStepDuration);
	}

	switch (Step.StepType)
	{
	case ETourStepType::SplineMove:
	{
		ATourCameraRig* Rig = GetOrSpawnCameraRig();
		if (Rig != nullptr)
		{
			// Place the rig before it becomes the view target, otherwise the blend starts from
			// wherever the rig happened to be left by the previous step.
			FTourCameraState EntryState;
			if (const ATourPath* Path = Resolved.Path.Get())
			{
				Path->EvaluateAtDistance(ComputeSplineDistance(Resolved, StepElapsed), EntryState);
			}

			Rig->ResetSmoothing();
			Rig->ApplyState(EntryState, 0.0f);
			Rig->StartCameraShake(GetTourController());

			ApplyViewTarget(Rig, Step);
		}
		break;
	}

	case ETourStepType::StaticCamera:
		if (ACineCameraActor* Camera = Resolved.StaticCamera.Get())
		{
			ApplyViewTarget(Camera, Step);
		}
		break;

	case ETourStepType::Dwell:
	case ETourStepType::Custom:
		// The view target carries over from the previous step by design: a Dwell is a hold on
		// whatever is already on screen.
		break;

	default:
		break;
	}

	if (Step.StepType == ETourStepType::Custom && Step.CustomEventTag.IsValid())
	{
		OnCustomEventTag.Broadcast(Step.CustomEventTag);
	}

	if (bIndexChanged || bFromStart)
	{
		OnStepChanged.Broadcast(CurrentStepIndex, Step.Label);
	}
}

void UTourSubsystem::AdvanceStepForward()
{
	check(LoadedPreset != nullptr);

	const FTourStep& CompletedStep = LoadedPreset->Steps[CurrentStepIndex];

	if (CompletedStep.bPauseAtEnd)
	{
		// Park exactly on the boundary so resuming continues from the end of this step rather
		// than from wherever the overshoot left the playhead.
		StepElapsed = FMath::Max(ResolvedSteps[CurrentStepIndex].Duration, ArchVizTour::SubsystemPrivate::MinStepDuration);
		SetPaused(true);
		return;
	}

	const int32 NextIndex = CurrentStepIndex + 1;

	if (!IsValidStepIndex(NextIndex))
	{
		if (LoadedPreset->bLoopTour)
		{
			BeginStep(0, /*bFromStart*/ true);
			return;
		}

		FinishTour();
		return;
	}

	BeginStep(NextIndex, /*bFromStart*/ true);
}

void UTourSubsystem::AdvanceStepBackward()
{
	check(LoadedPreset != nullptr);

	const int32 PreviousIndex = CurrentStepIndex - 1;

	if (!IsValidStepIndex(PreviousIndex))
	{
		if (LoadedPreset->bLoopTour)
		{
			BeginStep(GetStepCount() - 1, /*bFromStart*/ true);
			return;
		}

		// Reversing off the front of a non-looping tour stops at the first frame rather than
		// finishing: "finished" at the start would be nonsense in the UI.
		StepElapsed = 0.0f;
		SetPaused(true);
		return;
	}

	BeginStep(PreviousIndex, /*bFromStart*/ true);
}

void UTourSubsystem::EvaluateCurrentStep(float DeltaSeconds)
{
	if (!IsValidStepIndex(CurrentStepIndex))
	{
		return;
	}

	const FTourStep& Step = LoadedPreset->Steps[CurrentStepIndex];
	const FTourResolvedStep& Resolved = ResolvedSteps[CurrentStepIndex];

	if (Step.StepType != ETourStepType::SplineMove)
	{
		// Static, dwell and custom steps have nothing to drive per frame: the CineCameraActor
		// owns its own transform, and a hold is a hold.
		return;
	}

	const ATourPath* Path = Resolved.Path.Get();
	ATourCameraRig* Rig = CameraRig.Get();

	if (Path == nullptr || Rig == nullptr)
	{
		// The path can be destroyed mid-step (level streaming, an undo in PIE). Skipping the
		// evaluation leaves the camera where it was instead of snapping it to the origin.
		return;
	}

	const float Distance = ComputeSplineDistance(Resolved, StepElapsed);

	FTourCameraState CameraState;
	if (Path->EvaluateAtDistance(Distance, CameraState))
	{
		Rig->ApplyState(CameraState, DeltaSeconds);
	}
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

int32 UTourSubsystem::GetStepCount() const
{
	return LoadedPreset != nullptr ? LoadedPreset->Steps.Num() : 0;
}

FText UTourSubsystem::GetCurrentStepLabel() const
{
	if (!IsValidStepIndex(CurrentStepIndex))
	{
		return FText::GetEmpty();
	}

	const FTourStep& Step = LoadedPreset->Steps[CurrentStepIndex];
	if (!Step.Label.IsEmpty())
	{
		return Step.Label;
	}

	// An unlabelled step still needs something for the UI to display.
	return FText::Format(LOCTEXT("UnnamedTourStep", "Step {0}"), FText::AsNumber(CurrentStepIndex + 1));
}

TArray<FText> UTourSubsystem::GetStepLabels() const
{
	TArray<FText> Labels;

	if (LoadedPreset == nullptr)
	{
		return Labels;
	}

	Labels.Reserve(LoadedPreset->Steps.Num());
	for (int32 Index = 0; Index < LoadedPreset->Steps.Num(); ++Index)
	{
		const FTourStep& Step = LoadedPreset->Steps[Index];
		Labels.Add(Step.Label.IsEmpty()
			? FText::Format(LOCTEXT("UnnamedTourStep", "Step {0}"), FText::AsNumber(Index + 1))
			: Step.Label);
	}

	return Labels;
}

float UTourSubsystem::GetStepProgress() const
{
	if (!IsValidStepIndex(CurrentStepIndex))
	{
		return 0.0f;
	}

	const float Duration = FMath::Max(ResolvedSteps[CurrentStepIndex].Duration, ArchVizTour::SubsystemPrivate::MinStepDuration);
	return FMath::Clamp(StepElapsed / Duration, 0.0f, 1.0f);
}

float UTourSubsystem::GetTourProgress() const
{
	if (!IsValidStepIndex(CurrentStepIndex) || TotalDuration <= ArchVizTour::SubsystemPrivate::MinDivisor)
	{
		return 0.0f;
	}

	const float Elapsed = ResolvedSteps[CurrentStepIndex].StartTime + FMath::Max(StepElapsed, 0.0f);
	return FMath::Clamp(Elapsed / TotalDuration, 0.0f, 1.0f);
}

void UTourSubsystem::GetTourTimes(float& OutElapsedSeconds, float& OutTotalSeconds) const
{
	OutTotalSeconds = TotalDuration;
	OutElapsedSeconds = IsValidStepIndex(CurrentStepIndex)
		? ResolvedSteps[CurrentStepIndex].StartTime + FMath::Max(StepElapsed, 0.0f)
		: 0.0f;
}

bool UTourSubsystem::IsPlayButtonEnabled() const
{
	return LoadedPreset != nullptr
		&& LoadedPreset->IsPlayable()
		&& State != ETourState::Playing
		&& State != ETourState::Blending;
}

bool UTourSubsystem::IsPauseButtonEnabled() const
{
	return State == ETourState::Playing || State == ETourState::Blending || State == ETourState::Paused;
}

bool UTourSubsystem::IsStopButtonEnabled() const
{
	return State != ETourState::Idle;
}

bool UTourSubsystem::IsNextButtonEnabled() const
{
	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable())
	{
		return false;
	}

	// A looping tour always has a next step; a linear one runs out at the end.
	return LoadedPreset->bLoopTour || CurrentStepIndex < GetStepCount() - 1;
}

bool UTourSubsystem::IsPreviousButtonEnabled() const
{
	if (LoadedPreset == nullptr || !LoadedPreset->IsPlayable())
	{
		return false;
	}

	// Previous is still useful on the first step, where it restarts it.
	return true;
}

// ---------------------------------------------------------------------------
// Actors and view targets
// ---------------------------------------------------------------------------

ATourPath* UTourSubsystem::FindTourPath(FName PathName) const
{
	UWorld* World = GetWorld();
	if (World == nullptr || PathName == NAME_None)
	{
		return nullptr;
	}

	for (TActorIterator<ATourPath> It(World); It; ++It)
	{
		ATourPath* Path = *It;
		if (!IsValid(Path))
		{
			continue;
		}

		// Both the actor's name and its tags are accepted, so a designer can rename an actor
		// without breaking every step that referenced it, by tagging it instead.
		if (Path->GetFName() == PathName || Path->ActorHasTag(PathName))
		{
			return Path;
		}
	}

	return nullptr;
}

ACineCameraActor* UTourSubsystem::FindStaticCamera(FName CameraName) const
{
	UWorld* World = GetWorld();
	if (World == nullptr || CameraName == NAME_None)
	{
		return nullptr;
	}

	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
	{
		ACineCameraActor* Camera = *It;
		if (!IsValid(Camera))
		{
			continue;
		}

		if (Camera->GetFName() == CameraName || Camera->ActorHasTag(CameraName))
		{
			return Camera;
		}
	}

	return nullptr;
}

APlayerController* UTourSubsystem::GetTourController()
{
	if (APlayerController* Cached = CachedController.Get())
	{
		return Cached;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// GetFirstPlayerController is per-world, so in PIE with several clients each world's
	// subsystem drives its own client's view rather than fighting over one controller.
	APlayerController* Controller = World->GetFirstPlayerController();
	CachedController = Controller;
	return Controller;
}

void UTourSubsystem::HandleGameModePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (NewPlayer == nullptr || NewPlayer->GetWorld() != GetWorld())
	{
		// The delegate is global, so logins in other PIE worlds arrive here too.
		return;
	}

	if (!CachedController.IsValid())
	{
		CachedController = NewPlayer;
		UE_LOG(LogArchVizTour, Log, TEXT("Tour subsystem picked up player controller '%s'."), *NewPlayer->GetName());

		// A tour already mid-step never took the view because there was nobody to take it from.
		if ((State == ETourState::Playing || State == ETourState::Blending) && IsValidStepIndex(CurrentStepIndex))
		{
			BeginStep(CurrentStepIndex, /*bFromStart*/ false);
		}
	}
}

void UTourSubsystem::ApplyViewTarget(AActor* NewViewTarget, const FTourStep& Step)
{
	if (NewViewTarget == nullptr)
	{
		return;
	}

	APlayerController* Controller = GetTourController();
	if (Controller == nullptr)
	{
		UE_LOG(LogArchVizTour, Verbose,
			TEXT("No player controller yet; the tour will take the view as soon as one exists."));
		return;
	}

	if (!PreTourViewTarget.IsValid())
	{
		PreTourViewTarget = Controller->GetViewTarget();
	}

	const float BlendTime = FMath::Max(Step.BlendTime, 0.0f);

	Controller->SetViewTargetWithBlendParams(
		NewViewTarget,
		BlendTime,
		Step.BlendFunction,
		Step.BlendExp,
		Step.bLockOutgoing);

	BlendRemaining = BlendTime;

	if (BlendTime > 0.0f && State == ETourState::Playing)
	{
		SetState(ETourState::Blending);
	}
}

void UTourSubsystem::RestorePreTourViewTarget()
{
	APlayerController* Controller = GetTourController();
	if (Controller == nullptr)
	{
		PreTourViewTarget.Reset();
		return;
	}

	AActor* Restore = PreTourViewTarget.Get();
	if (Restore == nullptr)
	{
		// The pre-tour target is gone (or was never captured); the pawn is the only sensible
		// thing left to hand the camera back to.
		Restore = Controller->GetPawn();
	}

	if (Restore != nullptr)
	{
		Controller->SetViewTarget(Restore);
	}

	PreTourViewTarget.Reset();
}

ATourCameraRig* UTourSubsystem::GetOrSpawnCameraRig()
{
	if (ATourCameraRig* Existing = CameraRig.Get())
	{
		return Existing;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	const TSubclassOf<ATourCameraRig> SpawnClass = (CameraRigClass != nullptr) ? CameraRigClass : ATourCameraRig::StaticClass();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	ATourCameraRig* Rig = World->SpawnActor<ATourCameraRig>(SpawnClass, FTransform::Identity, SpawnParams);
	if (Rig == nullptr)
	{
		UE_LOG(LogArchVizTour, Error, TEXT("Failed to spawn the tour camera rig."));
		return nullptr;
	}

#if WITH_EDITOR
	// Transient actors still appear in the outliner; labelling it explains what it is when
	// someone inevitably finds it there during PIE.
	Rig->SetActorLabel(TEXT("Tour Camera Rig (runtime)"));
#endif

	CameraRig = Rig;
	return Rig;
}

ATourPath* UTourSubsystem::RegisterRuntimePath(FName PathName, const FTourPathData& PathData)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("RegisterRuntimePath('%s'): no world."), *PathName.ToString());
		return nullptr;
	}

	if (!PathData.IsTraversable())
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("RegisterRuntimePath('%s'): the supplied data holds %d points, which is not traversable."),
			*PathName.ToString(), PathData.Points.Num());
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	ATourPath* Path = World->SpawnActor<ATourPath>(ATourPath::StaticClass(), PathData.SplineWorldTransform, SpawnParams);
	if (Path == nullptr)
	{
		UE_LOG(LogArchVizTour, Error, TEXT("RegisterRuntimePath('%s'): spawn failed."), *PathName.ToString());
		return nullptr;
	}

	// Steps reference paths by name or tag; a spawned actor's name is generated, so the tag is
	// what makes FTourStep::SplinePathRef resolve.
	Path->Tags.AddUnique(PathName);
	Path->ApplyPathData(PathData, /*bApplyTransform*/ false);

	RuntimePaths.Add(Path);

	UE_LOG(LogArchVizTour, Log, TEXT("Registered runtime tour path '%s' (%d points)."),
		*PathName.ToString(), PathData.Points.Num());

	// Steps that failed to resolve earlier may resolve now.
	if (LoadedPreset != nullptr)
	{
		for (int32 Index = 0; Index < ResolvedSteps.Num(); ++Index)
		{
			if (!ResolvedSteps[Index].bResolved)
			{
				ResolveStep(Index);
			}
		}
	}

	return Path;
}

void UTourSubsystem::ClearRuntimePaths()
{
	for (const TWeakObjectPtr<ATourPath>& WeakPath : RuntimePaths)
	{
		if (ATourPath* Path = WeakPath.Get())
		{
			Path->Destroy();
		}
	}

	RuntimePaths.Reset();
}

// ---------------------------------------------------------------------------
// Level Sequence backend
// ---------------------------------------------------------------------------

bool UTourSubsystem::StartSequencerBackend()
{
	if (LoadedPreset == nullptr)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	ULevelSequence* Sequence = LoadedPreset->BakedSequence.LoadSynchronous();
	if (Sequence == nullptr)
	{
		return false;
	}

	StopSequencerBackend();

	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bAutoPlay = false;
	Settings.LoopCount.Value = LoadedPreset->bLoopTour ? -1 : 0;
	Settings.PlayRate = TimeScale * PlaybackDirection;
	// The sequence owns the camera cuts, so the player must be allowed to take the view target.
	Settings.bDisableCameraCuts = false;

	ALevelSequenceActor* OutActor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, OutActor);

	if (Player == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Could not create a level sequence player for '%s'."), *Sequence->GetName());
		return false;
	}

	SequencePlayer = Player;
	SequenceActor = OutActor;

	// Start from the current step rather than the top: the transport's position is authoritative
	// regardless of which backend is doing the moving.
	if (IsValidStepIndex(CurrentStepIndex))
	{
		const float StartTime = ResolvedSteps[CurrentStepIndex].StartTime + FMath::Max(StepElapsed, 0.0f);
		Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(StartTime, EUpdatePositionMethod::Jump));
	}

	Player->Play();

	UE_LOG(LogArchVizTour, Log, TEXT("Tour '%s' is playing back through level sequence '%s'."),
		*LoadedPreset->GetName(), *Sequence->GetName());
	return true;
}

void UTourSubsystem::StopSequencerBackend()
{
	if (SequencePlayer != nullptr)
	{
		SequencePlayer->Stop();
		SequencePlayer = nullptr;
	}

	if (ALevelSequenceActor* Actor = SequenceActor.Get())
	{
		Actor->Destroy();
	}
	SequenceActor.Reset();
}

void UTourSubsystem::TickSequencerBackend()
{
	if (SequencePlayer == nullptr)
	{
		// The player was torn down under us (level streaming, an editor stop); fall back rather
		// than leaving the tour stuck in Playing with nothing moving.
		UE_LOG(LogArchVizTour, Warning, TEXT("Level sequence player disappeared mid-tour; reverting to procedural playback."));
		ActiveBackend = ETourPlaybackBackend::Procedural;
		return;
	}

	// Sequencer owns the playhead in this mode, so the transport mirrors it rather than
	// accumulating its own time; that is exactly why the same motion is used for playback and
	// for an offline render.
	const float CurrentSeconds = static_cast<float>(SequencePlayer->GetCurrentTime().AsSeconds());

	int32 SequencerStep = CurrentStepIndex;
	for (int32 Index = ResolvedSteps.Num() - 1; Index >= 0; --Index)
	{
		if (CurrentSeconds >= ResolvedSteps[Index].StartTime)
		{
			SequencerStep = Index;
			break;
		}
	}

	if (SequencerStep != CurrentStepIndex && IsValidStepIndex(SequencerStep))
	{
		CurrentStepIndex = SequencerStep;
		OnStepChanged.Broadcast(CurrentStepIndex, GetCurrentStepLabel());
	}

	if (IsValidStepIndex(CurrentStepIndex))
	{
		StepElapsed = FMath::Max(CurrentSeconds - ResolvedSteps[CurrentStepIndex].StartTime, 0.0f);
	}

	OnTourProgress.Broadcast(GetTourProgress(), GetStepProgress());

	if (!SequencePlayer->IsPlaying() && State == ETourState::Playing)
	{
		FinishTour();
	}
}

// ---------------------------------------------------------------------------
// Enhanced Input
// ---------------------------------------------------------------------------

void UTourSubsystem::SetupTourInput()
{
	const UTourRuntimeSettings& Settings = UTourRuntimeSettings::Get();
	if (!Settings.bEnableDefaultInput || Settings.InputConfig.IsNull())
	{
		return;
	}

	APlayerController* Controller = GetTourController();
	if (Controller == nullptr)
	{
		return;
	}

	if (InputBoundController.Get() == Controller && ActiveInputConfig != nullptr)
	{
		return;
	}

	TeardownTourInput();

	// Loaded synchronously: this runs on a button press, the asset is a handful of bytes, and
	// an async load would make the first Space press after Play silently do nothing.
	UTourInputConfig* Config = Settings.InputConfig.LoadSynchronous();
	if (Config == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour input is enabled but the configured input config could not be loaded; keyboard transport will be unavailable."));
		return;
	}

	UEnhancedInputComponent* InputComponent = Cast<UEnhancedInputComponent>(Controller->InputComponent);
	if (InputComponent == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour input is enabled but the player controller does not use an Enhanced Input component; keyboard transport will be unavailable."));
		return;
	}

	if (const ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (Config->MappingContext != nullptr)
			{
				InputSubsystem->AddMappingContext(Config->MappingContext, Config->MappingPriority);
			}
		}
	}

	auto BindIfSet = [this, InputComponent](UInputAction* Action, void (UTourSubsystem::*Handler)())
	{
		if (Action != nullptr)
		{
			InputBindingHandles.Add(InputComponent->BindAction(Action, ETriggerEvent::Started, this, Handler).GetHandle());
		}
	};

	BindIfSet(Config->TogglePauseAction,  &UTourSubsystem::HandleTogglePauseInput);
	BindIfSet(Config->NextStepAction,     &UTourSubsystem::HandleNextStepInput);
	BindIfSet(Config->PreviousStepAction, &UTourSubsystem::HandlePreviousStepInput);
	BindIfSet(Config->StopTourAction,     &UTourSubsystem::HandleStopTourInput);
	BindIfSet(Config->RestartTourAction,  &UTourSubsystem::HandleRestartTourInput);

	ActiveInputConfig = Config;
	InputBoundController = Controller;
}

void UTourSubsystem::TeardownTourInput()
{
	APlayerController* Controller = InputBoundController.Get();

	if (Controller != nullptr)
	{
		if (UEnhancedInputComponent* InputComponent = Cast<UEnhancedInputComponent>(Controller->InputComponent))
		{
			for (const uint32 Handle : InputBindingHandles)
			{
				InputComponent->RemoveBindingByHandle(Handle);
			}
		}

		if (ActiveInputConfig != nullptr && ActiveInputConfig->MappingContext != nullptr)
		{
			if (const ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer())
			{
				if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
				{
					InputSubsystem->RemoveMappingContext(ActiveInputConfig->MappingContext);
				}
			}
		}
	}

	InputBindingHandles.Reset();
	ActiveInputConfig = nullptr;
	InputBoundController.Reset();
}

void UTourSubsystem::HandleTogglePauseInput()
{
	TogglePause();
}

void UTourSubsystem::HandleNextStepInput()
{
	NextStep();
}

void UTourSubsystem::HandlePreviousStepInput()
{
	PreviousStep();
}

void UTourSubsystem::HandleStopTourInput()
{
	StopTour();
}

void UTourSubsystem::HandleRestartTourInput()
{
	RestartTour();
}

#undef LOCTEXT_NAMESPACE
