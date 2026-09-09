// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourRenderBackend_MoviePipeline.h"

#include "ArchVizTourLog.h"
#include "Engine/World.h"
#include "TourRenderSettings.h"
#include "TourSequencePreset.h"

#if WITH_ARCHVIZTOUR_MRP
#include "EngineUtils.h"
#include "LevelSequence.h"
#include "MoviePipeline.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineCommandLineEncoder.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineQueue.h"
#include "TourPath.h"
#endif

#define LOCTEXT_NAMESPACE "ArchVizTour"

FTourRenderBackend_MoviePipeline::~FTourRenderBackend_MoviePipeline()
{
#if WITH_ARCHVIZTOUR_MRP
	RestoreTourGizmos();
#endif
}

void FTourRenderBackend_MoviePipeline::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Request.Tour);
	Collector.AddReferencedObject(Request.Settings);

#if WITH_ARCHVIZTOUR_MRP
	Collector.AddReferencedObject(PipelineQueue);
	Collector.AddReferencedObject(Pipeline);
#endif
}

bool FTourRenderBackend_MoviePipeline::IsAvailable(FString& OutUnavailableReason) const
{
#if !WITH_ARCHVIZTOUR_MRP
	OutUnavailableReason = TEXT(
		"The Movie Render Pipeline plugin was not found when ArchVizTour was compiled. "
		"Enable it in Edit > Plugins, make sure it is included in the packaged build, and rebuild. "
		"The Scene Capture backend is used instead.");
	return false;
#else
	if (Request.Tour != nullptr && Request.Tour->BakedSequence.IsNull())
	{
		OutUnavailableReason = TEXT(
			"This tour has no baked Level Sequence. Right-click the Tour Sequence Preset and choose "
			"'Create Level Sequence' first: the Movie Render Pipeline renders sequences, not step lists.");
		return false;
	}

	OutUnavailableReason.Reset();
	return true;
#endif
}

#if !WITH_ARCHVIZTOUR_MRP

// ---------------------------------------------------------------------------
// Stub implementation used when the MovieRenderPipeline plugin is absent.
//
// The backend still exists and still reports itself through the same interface, so the
// subsystem's backend selection needs no compile-time branching of its own.
// ---------------------------------------------------------------------------

bool FTourRenderBackend_MoviePipeline::Start(const FTourRenderRequest& InRequest)
{
	Request = InRequest;

	FString Reason;
	IsAvailable(Reason);

	UE_LOG(LogArchVizTour, Warning, TEXT("%s"), *Reason);
	OnCompleted.Broadcast(/*bSuccess*/ false, FString(), Reason);
	return false;
}

bool FTourRenderBackend_MoviePipeline::Tick(float DeltaSeconds)
{
	return false;
}

void FTourRenderBackend_MoviePipeline::Cancel()
{
}

#else

// ---------------------------------------------------------------------------
// Real implementation
// ---------------------------------------------------------------------------

bool FTourRenderBackend_MoviePipeline::Start(const FTourRenderRequest& InRequest)
{
	if (bRunning)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("The Movie Render Pipeline backend is already rendering."));
		return false;
	}

	Request = InRequest;
	bCompleted = false;

	if (!Request.IsValid())
	{
		FinishJob(false, TEXT("The render request is incomplete."));
		return false;
	}

	FString UnavailableReason;
	if (!IsAvailable(UnavailableReason))
	{
		FinishJob(false, UnavailableReason);
		return false;
	}

	UWorld* World = Request.World.Get();
	check(World != nullptr);

	if (Request.Settings->bHideTourRailMeshes)
	{
		for (TActorIterator<ATourPath> It(World); It; ++It)
		{
			ATourPath* Path = *It;
			if (IsValid(Path) && Path->bShowRailMesh)
			{
				Path->SetRailVisible(false);
				HiddenRailPaths.Add(Path);
			}
		}
	}

	FString BuildError;
	UMoviePipelineExecutorJob* Job = BuildPipelineJob(BuildError);
	if (Job == nullptr)
	{
		FinishJob(false, BuildError);
		return false;
	}

	// The pipeline is driven directly rather than through an executor. UMoviePipelinePIEExecutor
	// is editor-only and UMoviePipelineLinearExecutorBase is abstract, so neither exists to be
	// instantiated in a packaged game; UMoviePipeline::Initialize installs the engine-tick hooks
	// the render needs on its own, which is exactly what an executor would have arranged.
	Pipeline = NewObject<UMoviePipeline>(World, UMoviePipeline::StaticClass());
	if (Pipeline == nullptr)
	{
		FinishJob(false, TEXT("Could not create the movie pipeline."));
		return false;
	}

	Pipeline->OnMoviePipelineWorkFinished().AddRaw(this, &FTourRenderBackend_MoviePipeline::HandleWorkFinished);
	Pipeline->Initialize(Job);

	bRunning = true;

	UE_LOG(LogArchVizTour, Log,
		TEXT("Movie Render Pipeline render started: %d frames at %.2f fps, %d x %d, into '%s'."),
		Request.TotalFrames, Request.Settings->FrameRate,
		Request.Settings->Resolution.X, Request.Settings->Resolution.Y,
		*Request.OutputDirectory);

	return true;
}

UMoviePipelineExecutorJob* FTourRenderBackend_MoviePipeline::BuildPipelineJob(FString& OutError)
{
	ULevelSequence* Sequence = Request.Tour->BakedSequence.LoadSynchronous();
	if (Sequence == nullptr)
	{
		OutError = TEXT("The tour's baked Level Sequence could not be loaded.");
		return nullptr;
	}

	UWorld* World = Request.World.Get();
	if (World == nullptr)
	{
		OutError = TEXT("The render world is no longer valid.");
		return nullptr;
	}

	PipelineQueue = NewObject<UMoviePipelineQueue>(GetTransientPackage());
	check(PipelineQueue != nullptr);

	UMoviePipelineExecutorJob* Job = PipelineQueue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
	if (Job == nullptr)
	{
		OutError = TEXT("Could not allocate a Movie Render Pipeline job.");
		return nullptr;
	}

	Job->JobName = Request.Tour->TourTitle.IsEmpty() ? Request.Tour->GetName() : Request.Tour->TourTitle.ToString();
	Job->Sequence = FSoftObjectPath(Sequence);
	// The map is the one already loaded: this backend renders inside the running game process
	// rather than launching a new one, so there is nothing to travel to.
	Job->Map = FSoftObjectPath(World);

	ConfigureJobSettings(Job);
	return Job;
}

void FTourRenderBackend_MoviePipeline::ConfigureJobSettings(UMoviePipelineExecutorJob* Job)
{
	check(Job != nullptr);
	check(Request.Settings != nullptr);

	UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
	check(Config != nullptr);

	// The deferred pass is the standard lit render; without an explicit render pass the pipeline
	// produces no image at all.
	Config->FindOrAddSettingByClass(UMoviePipelineDeferredPassBase::StaticClass());

	if (UMoviePipelineOutputSetting* Output = Cast<UMoviePipelineOutputSetting>(
		Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass())))
	{
		Output->OutputDirectory.Path    = Request.OutputDirectory;
		Output->OutputResolution        = Request.Settings->Resolution;
		Output->bUseCustomFrameRate     = true;
		Output->OutputFrameRate         = FFrameRate(FMath::RoundToInt(Request.Settings->FrameRate), 1);
		Output->bOverrideExistingOutput = true;
		Output->ZeroPadFrameNumbers     = Request.Settings->FrameNumberPadding;

		// MRP has its own token vocabulary; translating the plugin's pattern into it keeps one
		// filename convention across both backends.
		FString Pattern = Request.Settings->FileNamePattern;
		Pattern.ReplaceInline(TEXT("{tour}"),  TEXT("{sequence_name}"), ESearchCase::IgnoreCase);
		Pattern.ReplaceInline(TEXT("{frame}"), TEXT("{frame_number}"), ESearchCase::IgnoreCase);
		Output->FileNameFormat = Pattern;
	}

	if (UMoviePipelineAntiAliasingSetting* AntiAliasing = Cast<UMoviePipelineAntiAliasingSetting>(
		Config->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass())))
	{
		AntiAliasing->SpatialSampleCount  = Request.Settings->SpatialSampleCount;
		AntiAliasing->TemporalSampleCount = Request.Settings->TemporalSampleCount;
		AntiAliasing->EngineWarmUpCount   = Request.Settings->EngineWarmUpFrameCount;
		// Warm-up frames have to be rendered, not merely ticked, or temporal history (TAA,
		// auto-exposure, screen-space effects) is still settling on the first output frame.
		AntiAliasing->bRenderWarmUpFrames = Request.Settings->EngineWarmUpFrameCount > 0;
	}

	if (UMoviePipelineGameOverrideSetting* GameOverride = Cast<UMoviePipelineGameOverrideSetting>(
		Config->FindOrAddSettingByClass(UMoviePipelineGameOverrideSetting::StaticClass())))
	{
		GameOverride->bCinematicQualitySettings = Request.Settings->bUseCinematicQuality;
		GameOverride->bFlushGrassStreaming      = Request.Settings->bFlushGrass;
		GameOverride->bFlushStreamingManagers   = true;
	}

	// Image output. EXR gets its own writer; PNG and JPEG share the image-sequence output.
	switch (Request.Settings->ImageFormat)
	{
	case ETourImageFormat::EXR:
		Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_EXR::StaticClass());
		break;

	case ETourImageFormat::JPEG:
		Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_JPG::StaticClass());
		break;

	case ETourImageFormat::PNG:
	default:
		Config->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass());
		break;
	}

	if (Request.Settings->bEncodeVideo)
	{
		if (UMoviePipelineCommandLineEncoder* Encoder = Cast<UMoviePipelineCommandLineEncoder>(
			Config->FindOrAddSettingByClass(UMoviePipelineCommandLineEncoder::StaticClass())))
		{
			// MRP's encoder reads its executable path and argument template from project settings
			// (Project Settings > Plugins > Movie Pipeline CLI Encoder) rather than from the job,
			// so only the parts it does expose per job are set here. The plugin's own ffmpeg
			// settings apply to the Scene Capture backend instead.
			Encoder->bDeleteSourceFiles = Request.Settings->bDeleteFramesAfterEncode;
			Encoder->bSkipEncodeOnRenderCanceled = true;
		}
	}
}

bool FTourRenderBackend_MoviePipeline::Tick(float DeltaSeconds)
{
	if (!bRunning)
	{
		return false;
	}

	if (Pipeline == nullptr)
	{
		FinishJob(false, TEXT("The movie pipeline disappeared."));
		return false;
	}

	UWorld* World = Request.World.Get();
	if (World == nullptr || World->bIsTearingDown)
	{
		Cancel();
		FinishJob(false, TEXT("The world was torn down during the render."));
		return false;
	}

	// Progress is coarse for this backend by design. The pipeline owns its own frame loop,
	// accumulating an arbitrary number of spatial and temporal samples plus warm-up frames per
	// output frame, so counting engine ticks here would report a number that has nothing to do
	// with how much of the render is done. MRP's own on-screen widget and log report per-frame
	// progress; this delegate reports the states this backend actually knows.
	OnProgress.Broadcast(0.0f, 0, Request.TotalFrames);

	return bRunning;
}

void FTourRenderBackend_MoviePipeline::Cancel()
{
	if (Pipeline != nullptr && bRunning)
	{
		// Requesting rather than forcing lets the pipeline finish writing the frame it is on.
		Pipeline->RequestShutdown(/*bIsError*/ false);
	}
}

void FTourRenderBackend_MoviePipeline::HandleWorkFinished(FMoviePipelineOutputData OutputData)
{
	FinishJob(OutputData.bSuccess,
		OutputData.bSuccess ? FString() : TEXT("The Movie Render Pipeline job reported a failure. See the log for details."));
}

void FTourRenderBackend_MoviePipeline::RestoreTourGizmos()
{
	for (const TWeakObjectPtr<ATourPath>& WeakPath : HiddenRailPaths)
	{
		if (ATourPath* Path = WeakPath.Get())
		{
			Path->SetRailVisible(true);
		}
	}

	HiddenRailPaths.Reset();
}

void FTourRenderBackend_MoviePipeline::FinishJob(bool bSuccess, const FString& Error)
{
	if (bCompleted)
	{
		return;
	}
	bCompleted = true;
	bRunning = false;

	if (Pipeline != nullptr)
	{
		Pipeline->OnMoviePipelineWorkFinished().RemoveAll(this);
		Pipeline = nullptr;
	}

	PipelineQueue = nullptr;

	RestoreTourGizmos();

	OnProgress.Broadcast(bSuccess ? 1.0f : 0.0f, Request.TotalFrames, Request.TotalFrames);

	UE_LOG(LogArchVizTour, Log, TEXT("Movie Render Pipeline render %s."), bSuccess ? TEXT("completed") : TEXT("failed"));

	OnCompleted.Broadcast(bSuccess, Request.OutputDirectory, Error);
}

#endif // WITH_ARCHVIZTOUR_MRP

#undef LOCTEXT_NAMESPACE
