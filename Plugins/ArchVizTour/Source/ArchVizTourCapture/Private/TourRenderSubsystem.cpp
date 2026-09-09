// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourRenderSubsystem.h"

#include "ArchVizTourLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TourRenderBackend.h"
#include "TourRenderBackend_MoviePipeline.h"
#include "TourRenderBackend_SceneCapture.h"
#include "TourSequencePreset.h"

#define LOCTEXT_NAMESPACE "ArchVizTour"

void UTourRenderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	DefaultSettings = NewObject<UTourRenderSettings>(this, TEXT("DefaultTourRenderSettings"));

	// FTSTicker rather than an actor or a component: a render must progress even in a level with
	// nothing placed in it, and spawning a helper actor purely to obtain a Tick leaves a mystery
	// object in the outliner for every user who later opens the level.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTourRenderSubsystem::HandleTick));
}

void UTourRenderSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	if (ActiveBackend.IsValid())
	{
		// Cancelling here also restores the engine's timing mode and any hidden rail meshes; a
		// job left running through shutdown would leave the process in fixed timestep.
		ActiveBackend->Cancel();
		ActiveBackend->OnProgress.RemoveAll(this);
		ActiveBackend->OnCompleted.RemoveAll(this);
		ActiveBackend.Reset();
	}

	Super::Deinitialize();
}

UTourRenderSubsystem* UTourRenderSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr || GEngine == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (World == nullptr)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UTourRenderSubsystem>() : nullptr;
}

UWorld* UTourRenderSubsystem::GetRenderWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance != nullptr ? GameInstance->GetWorld() : nullptr;
}

bool UTourRenderSubsystem::IsRendering() const
{
	return ActiveBackend.IsValid() && ActiveBackend->IsRunning();
}

FName UTourRenderSubsystem::GetActiveBackendName() const
{
	return ActiveBackend.IsValid() ? ActiveBackend->GetBackendName() : NAME_None;
}

// ---------------------------------------------------------------------------
// Backend selection
// ---------------------------------------------------------------------------

TSharedPtr<ITourRenderBackend> UTourRenderSubsystem::CreateBackend(
	ETourRenderBackend Requested, const UTourSequencePreset* Tour, FString& OutReason) const
{
	auto MakeMoviePipeline = []() { return MakeShared<FTourRenderBackend_MoviePipeline>(); };
	auto MakeSceneCapture   = []() { return MakeShared<FTourRenderBackend_SceneCapture>(); };

	switch (Requested)
	{
	case ETourRenderBackend::MoviePipeline:
	{
		// An explicit choice is honoured or refused, never silently substituted: a user who
		// asked for MRP quality and got a live capture instead would ship the wrong thing.
		TSharedPtr<ITourRenderBackend> Backend = MakeMoviePipeline();
		if (!Backend->IsAvailable(OutReason))
		{
			return nullptr;
		}
		return Backend;
	}

	case ETourRenderBackend::SceneCapture:
	{
		TSharedPtr<ITourRenderBackend> Backend = MakeSceneCapture();
		if (!Backend->IsAvailable(OutReason))
		{
			return nullptr;
		}
		return Backend;
	}

	case ETourRenderBackend::Automatic:
	default:
	{
		// Prefer quality, fall back to availability.
		TSharedPtr<ITourRenderBackend> Preferred = MakeMoviePipeline();

		FString PreferredReason;
		// IsAvailable on the MRP backend also checks for a baked sequence, which it can only do
		// once it knows the tour; the request is not built yet, so the check is repeated inside
		// Start. Here it catches the common case of the plugin simply not being present.
		if (Preferred->IsAvailable(PreferredReason) && Tour != nullptr && !Tour->BakedSequence.IsNull())
		{
			return Preferred;
		}

		UE_LOG(LogArchVizTour, Log,
			TEXT("Automatic backend selection chose Scene Capture: %s"),
			PreferredReason.IsEmpty() ? TEXT("the tour has no baked Level Sequence.") : *PreferredReason);

		TSharedPtr<ITourRenderBackend> Fallback = MakeSceneCapture();
		if (!Fallback->IsAvailable(OutReason))
		{
			return nullptr;
		}
		return Fallback;
	}
	}
}

bool UTourRenderSubsystem::IsBackendAvailable(ETourRenderBackend Backend, FString& OutReason) const
{
	OutReason.Reset();
	return CreateBackend(Backend, /*Tour*/ nullptr, OutReason).IsValid();
}

// ---------------------------------------------------------------------------
// Job control
// ---------------------------------------------------------------------------

bool UTourRenderSubsystem::StartRender(UTourSequencePreset* Tour, UTourRenderSettings* Settings)
{
	if (IsRendering())
	{
		ReportImmediateFailure(TEXT("A render is already in progress. Cancel it before starting another."));
		return false;
	}

	if (Tour == nullptr || !Tour->IsPlayable())
	{
		ReportImmediateFailure(TEXT("The tour is missing or has no steps."));
		return false;
	}

	UWorld* World = GetRenderWorld();
	if (World == nullptr)
	{
		ReportImmediateFailure(TEXT("There is no world to render in."));
		return false;
	}

	if (Settings == nullptr)
	{
		Settings = DefaultSettings;
	}

	if (Settings == nullptr)
	{
		ReportImmediateFailure(TEXT("No render settings are available."));
		return false;
	}

	Settings->SanitizeInPlace();

	FString BackendReason;
	TSharedPtr<ITourRenderBackend> Backend = CreateBackend(Settings->Backend, Tour, BackendReason);
	if (!Backend.IsValid())
	{
		ReportImmediateFailure(BackendReason.IsEmpty() ? TEXT("No render backend is available.") : BackendReason);
		return false;
	}

	FTourRenderRequest RenderRequest;
	RenderRequest.World           = World;
	RenderRequest.Tour            = Tour;
	RenderRequest.Settings        = Settings;
	RenderRequest.OutputDirectory = Settings->GetResolvedOutputDirectory();
	RenderRequest.Timestamp       = FDateTime::Now();
	RenderRequest.DurationSeconds = (Settings->DurationSource == ETourRenderDurationSource::Explicit)
		? Settings->ExplicitDurationSeconds
		: Tour->GetTotalDuration();
	RenderRequest.TotalFrames     = Settings->ComputeFrameCount(Tour->GetTotalDuration());

	if (RenderRequest.DurationSeconds <= 0.0f)
	{
		ReportImmediateFailure(TEXT("The tour has no duration to render. Give its steps a positive length, or set an explicit duration."));
		return false;
	}

	Backend->OnProgress.AddUObject(this, &UTourRenderSubsystem::HandleBackendProgress);
	Backend->OnCompleted.AddUObject(this, &UTourRenderSubsystem::HandleBackendCompleted);

	// Assigned before Start, because a backend that fails during Start fires OnCompleted
	// synchronously and the handler expects to find the job it is releasing.
	ActiveBackend = Backend;

	if (!Backend->Start(RenderRequest))
	{
		// OnCompleted has already fired and released ActiveBackend.
		return false;
	}

	UE_LOG(LogArchVizTour, Log, TEXT("Render job started on backend '%s'."), *Backend->GetBackendName().ToString());
	return true;
}

void UTourRenderSubsystem::CancelRender()
{
	if (!IsRendering())
	{
		return;
	}

	UE_LOG(LogArchVizTour, Log, TEXT("Cancelling the active render."));
	ActiveBackend->Cancel();
}

bool UTourRenderSubsystem::HandleTick(float DeltaSeconds)
{
	if (ActiveBackend.IsValid() && ActiveBackend->IsRunning())
	{
		// Held locally: a backend that completes inside Tick releases ActiveBackend from under
		// this call, and the shared pointer keeps it alive until the call returns.
		TSharedPtr<ITourRenderBackend> Backend = ActiveBackend;
		Backend->Tick(DeltaSeconds);
	}

	// Always keep ticking; the subsystem outlives individual jobs.
	return true;
}

void UTourRenderSubsystem::HandleBackendProgress(float Alpha, int32 Frame, int32 TotalFrames)
{
	OnRenderProgress.Broadcast(Alpha, Frame, TotalFrames);
}

void UTourRenderSubsystem::HandleBackendCompleted(bool bSuccess, const FString& OutputPath, const FString& Error)
{
	if (ActiveBackend.IsValid())
	{
		ActiveBackend->OnProgress.RemoveAll(this);
		ActiveBackend->OnCompleted.RemoveAll(this);
		ActiveBackend.Reset();
	}

	if (bSuccess)
	{
		UE_LOG(LogArchVizTour, Log, TEXT("Render completed: '%s'."), *OutputPath);
	}
	else
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Render failed: %s"), *Error);
	}

	OnRenderCompleted.Broadcast(bSuccess, OutputPath, Error);
}

void UTourRenderSubsystem::ReportImmediateFailure(const FString& Error)
{
	// Failures that happen before a backend exists still travel down the same delegate, so a
	// caller binds once rather than handling "returned false" and "completed with failure" in
	// two different places.
	UE_LOG(LogArchVizTour, Warning, TEXT("Render could not start: %s"), *Error);
	OnRenderCompleted.Broadcast(/*bSuccess*/ false, FString(), Error);
}

#undef LOCTEXT_NAMESPACE
