// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourRenderBackend_SceneCapture.h"

#include "ArchVizTourLog.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapper.h"
#include "ImagePixelData.h"
#include "ImageWriteQueue.h"
#include "ImageWriteTask.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "TourCaptureComponent.h"
#include "TourPath.h"
#include "TourSequencePreset.h"
#include "TourSubsystem.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "ArchVizTour"

namespace ArchVizTour::SceneCaptureBackend
{
	/** Frames the readback queue is allowed to run behind before the capture loop waits. */
	static constexpr int32 MaxOutstandingWrites = 32;

	/** Buffer size used when draining ffmpeg's stderr pipe. */
	static constexpr int32 EncoderPollIntervalMs = 100;

	/** Fallback executable name, resolved through PATH. */
	static const TCHAR* DefaultFFmpegName = TEXT("ffmpeg");

	/** Longest the job waits for outstanding frame writes once capture has finished, in seconds. */
	static constexpr double WriteDrainTimeoutSeconds = 60.0;
}

FTourRenderBackend_SceneCapture::~FTourRenderBackend_SceneCapture()
{
	// A backend destroyed mid-job would otherwise leave the engine in fixed timestep forever.
	RestoreTimeStep();
	RestoreTourGizmos();
}

void FTourRenderBackend_SceneCapture::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Request.Tour);
	Collector.AddReferencedObject(Request.Settings);
	Collector.AddReferencedObject(CaptureActor);
	Collector.AddReferencedObject(CaptureComponent);
}

bool FTourRenderBackend_SceneCapture::IsAvailable(FString& OutUnavailableReason) const
{
	// Nothing optional is involved: SceneCapture2D, render targets and ImageWriteQueue are all
	// core engine, on every platform the engine supports.
	OutUnavailableReason.Reset();
	return true;
}

// ---------------------------------------------------------------------------
// Job lifecycle
// ---------------------------------------------------------------------------

bool FTourRenderBackend_SceneCapture::Start(const FTourRenderRequest& InRequest)
{
	if (bRunning)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Scene capture backend is already rendering."));
		return false;
	}

	Request = InRequest;
	FramesCaptured = 0;
	bCancelRequested = false;
	bCompleted = false;
	PendingWrites.Store(0);
	FailedWrites.Store(0);

	if (!Request.IsValid())
	{
		FinishJob(false, TEXT("The render request is incomplete."));
		return false;
	}

	UWorld* World = Request.World.Get();
	check(World != nullptr);

	UTourSubsystem* TourSubsystem = World->GetSubsystem<UTourSubsystem>();
	if (TourSubsystem == nullptr)
	{
		FinishJob(false, TEXT("The world has no tour subsystem."));
		return false;
	}

	// Spawn the capture rig. It is transient and destroyed with the job, so a cancelled render
	// leaves nothing behind in the level.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	CaptureActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!IsValid(CaptureActor))
	{
		FinishJob(false, TEXT("Could not spawn the capture rig."));
		return false;
	}

	CaptureComponent = NewObject<UTourCaptureComponent>(CaptureActor, TEXT("TourCapture"));
	check(CaptureComponent != nullptr);
	CaptureActor->SetRootComponent(CaptureComponent);
	CaptureComponent->RegisterComponent();

	if (!CaptureComponent->Configure(Request.Settings->Resolution, Request.Settings->ImageFormat))
	{
		FinishJob(false, TEXT("Could not create the capture render target."));
		return false;
	}

	if (Request.Settings->bIncludeUI)
	{
		// SceneCapture2D renders the scene only; there is no composition path for UMG here.
		// Saying so beats silently producing frames with no UI in them.
		UE_LOG(LogArchVizTour, Warning,
			TEXT("The scene capture backend cannot include UI in its output. Use the Movie Render Pipeline backend with a UI render pass, or composite the UI afterwards."));
	}

	if (Request.Settings->bHideTourRailMeshes)
	{
		HideTourGizmos();
	}

	// Install and start the tour, then hand the engine a fixed timestep so the tour advances by
	// exactly one output frame per rendered frame.
	if (!TourSubsystem->LoadTour(Request.Tour))
	{
		FinishJob(false, TEXT("The tour could not be loaded for rendering."));
		return false;
	}

	TourSubsystem->SetTimeScale(1.0f);
	TourSubsystem->RestartTour();
	TourSubsystem->PlayTour();

	EnterFixedTimeStep();

	bRunning = true;

	UE_LOG(LogArchVizTour, Log,
		TEXT("Scene capture render started: %d frames at %.2f fps, %d x %d, into '%s'."),
		Request.TotalFrames, Request.Settings->FrameRate,
		Request.Settings->Resolution.X, Request.Settings->Resolution.Y,
		*Request.OutputDirectory);

	return true;
}

bool FTourRenderBackend_SceneCapture::Tick(float DeltaSeconds)
{
	if (!bRunning)
	{
		return false;
	}

	if (bCancelRequested)
	{
		FinishJob(false, TEXT("The render was cancelled."));
		return false;
	}

	UWorld* World = Request.World.Get();
	if (World == nullptr || World->bIsTearingDown)
	{
		// The level was unloaded under the job; abort cleanly rather than capturing a
		// half-destroyed world.
		FinishJob(false, TEXT("The world was torn down during the render."));
		return false;
	}

	if (!IsValid(CaptureComponent) || !CaptureComponent->IsConfigured())
	{
		FinishJob(false, TEXT("The capture render target was released during the render."));
		return false;
	}

	// Backpressure: if the write queue is falling behind, skip this frame's capture rather than
	// queueing unboundedly. The fixed timestep means no tour time is lost by waiting.
	if (PendingWrites.Load() >= ArchVizTour::SceneCaptureBackend::MaxOutstandingWrites)
	{
		return true;
	}

	CaptureCurrentFrame();
	EnqueueReadback(FramesCaptured);

	++FramesCaptured;

	OnProgress.Broadcast(
		static_cast<float>(FramesCaptured) / static_cast<float>(FMath::Max(Request.TotalFrames, 1)),
		FramesCaptured,
		Request.TotalFrames);

	if (FramesCaptured >= Request.TotalFrames)
	{
		using namespace ArchVizTour::SceneCaptureBackend;

		// The remaining readbacks are still in flight; flushing here is the one place a stall is
		// correct, because the job is over and the frames have to exist before ffmpeg runs.
		FlushRenderingCommands();

		// Then wait for the write queue to catch up, bounded so a stuck disk cannot hang the
		// process. This is the only place the game thread waits, and the job is already over.
		const double Deadline = FPlatformTime::Seconds() + WriteDrainTimeoutSeconds;
		while (PendingWrites.Load() > 0 && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.01f);
		}

		if (PendingWrites.Load() > 0)
		{
			FinishJob(false, FString::Printf(
				TEXT("%d frame writes did not complete within %.0f seconds."),
				PendingWrites.Load(), WriteDrainTimeoutSeconds));
			return false;
		}

		const int32 Failures = FailedWrites.Load();
		if (Failures > 0)
		{
			FinishJob(false, FString::Printf(TEXT("%d of %d frames could not be written."), Failures, Request.TotalFrames));
			return false;
		}

		FinishJob(true, FString());
		return false;
	}

	return true;
}

void FTourRenderBackend_SceneCapture::Cancel()
{
	if (!bRunning)
	{
		return;
	}

	// Deferred to the next tick so cancellation always tears down on the game thread, whatever
	// thread the request came from.
	bCancelRequested = true;
}

void FTourRenderBackend_SceneCapture::FinishJob(bool bSuccess, const FString& Error)
{
	if (bCompleted)
	{
		return;
	}
	bCompleted = true;
	bRunning = false;

	RestoreTimeStep();
	RestoreTourGizmos();

	if (UWorld* World = Request.World.Get())
	{
		if (!World->bIsTearingDown)
		{
			if (UTourSubsystem* TourSubsystem = World->GetSubsystem<UTourSubsystem>())
			{
				TourSubsystem->StopTour();
			}
		}
	}

	if (IsValid(CaptureComponent))
	{
		CaptureComponent->ReleaseResources();
	}

	if (IsValid(CaptureActor))
	{
		if (UWorld* World = CaptureActor->GetWorld())
		{
			if (!World->bIsTearingDown)
			{
				CaptureActor->Destroy();
			}
		}
	}

	CaptureComponent = nullptr;
	CaptureActor = nullptr;

	FString OutputPath = Request.OutputDirectory;
	FString FinalError = Error;

	if (bSuccess && Request.Settings != nullptr && Request.Settings->bEncodeVideo)
	{
		FString VideoPath;
		FString EncodeError;
		if (EncodeVideo(VideoPath, EncodeError))
		{
			OutputPath = VideoPath;
		}
		else
		{
			// A missing or failing encoder must not throw away a completed frame sequence: the
			// frames are the expensive part and are perfectly usable on their own.
			UE_LOG(LogArchVizTour, Warning,
				TEXT("The frame sequence rendered successfully but encoding failed: %s. The images remain in '%s'."),
				*EncodeError, *Request.OutputDirectory);
			FinalError = EncodeError;
		}
	}

	UE_LOG(LogArchVizTour, Log, TEXT("Scene capture render %s (%d frames)."),
		bSuccess ? TEXT("completed") : TEXT("failed"), FramesCaptured);

	OnCompleted.Broadcast(bSuccess, OutputPath, FinalError);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

void FTourRenderBackend_SceneCapture::EnterFixedTimeStep()
{
	if (bRestoreFixedTimeStep || Request.Settings == nullptr)
	{
		return;
	}

	bPreviousUseFixedTimeStep = FApp::UseFixedTimeStep();
	PreviousFixedDeltaTime = FApp::GetFixedDeltaTime();

	// One game frame becomes exactly one output frame. Without this the tour advances by real
	// elapsed time, so a frame that took 200 ms to render moves the camera six output frames
	// forward and the result judders no matter how carefully the path was authored.
	FApp::SetFixedDeltaTime(1.0 / static_cast<double>(FMath::Max(Request.Settings->FrameRate, 1.0f)));
	FApp::SetUseFixedTimeStep(true);

	bRestoreFixedTimeStep = true;
}

void FTourRenderBackend_SceneCapture::RestoreTimeStep()
{
	if (!bRestoreFixedTimeStep)
	{
		return;
	}

	FApp::SetUseFixedTimeStep(bPreviousUseFixedTimeStep);
	FApp::SetFixedDeltaTime(PreviousFixedDeltaTime);
	bRestoreFixedTimeStep = false;
}

// ---------------------------------------------------------------------------
// Scene composition
// ---------------------------------------------------------------------------

void FTourRenderBackend_SceneCapture::HideTourGizmos()
{
	UWorld* World = Request.World.Get();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<ATourPath> It(World); It; ++It)
	{
		ATourPath* Path = *It;
		if (IsValid(Path) && Path->bShowRailMesh)
		{
			// Only paths that were actually showing a rail are recorded, so restoring cannot
			// switch on a rail the user had deliberately hidden.
			Path->SetRailVisible(false);
			HiddenRailPaths.Add(Path);
		}
	}

	if (HiddenRailPaths.Num() > 0)
	{
		UE_LOG(LogArchVizTour, Log, TEXT("Hid %d tour rail(s) for the render."), HiddenRailPaths.Num());
	}
}

void FTourRenderBackend_SceneCapture::RestoreTourGizmos()
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

// ---------------------------------------------------------------------------
// Capture and readback
// ---------------------------------------------------------------------------

void FTourRenderBackend_SceneCapture::CaptureCurrentFrame()
{
	UWorld* World = Request.World.Get();
	if (World == nullptr || !IsValid(CaptureComponent))
	{
		return;
	}

	// The tour drives the player's view target, so following the camera manager reproduces the
	// tour exactly - including view-target blends, which a direct rig query would miss.
	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		if (const APlayerCameraManager* CameraManager = Controller->PlayerCameraManager)
		{
			const FMinimalViewInfo& POV = CameraManager->ViewTarget.POV;
			CaptureComponent->SetView(POV.Location, POV.Rotation, POV.FOV);
			// Without the view's post process the capture misses exposure, depth of field and
			// colour grading, and looks nothing like what the tour shows on screen.
			CaptureComponent->ApplyPostProcess(POV.PostProcessSettings, POV.PostProcessBlendWeight);
		}
	}

	CaptureComponent->CaptureFrame();
}

void FTourRenderBackend_SceneCapture::EnqueueReadback(int32 FrameIndex)
{
	if (!IsValid(CaptureComponent) || !CaptureComponent->IsConfigured())
	{
		return;
	}

	UTextureRenderTarget2D* Target = CaptureComponent->RenderTarget;
	FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
	if (Resource == nullptr)
	{
		FailedWrites.IncrementExchange();
		return;
	}

	// Resolve the write queue on the game thread: FModuleManager is not something to reach into
	// from a render command, and the queue itself outlives every job.
	IImageWriteQueueModule& WriteQueueModule = FModuleManager::LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue");
	IImageWriteQueue* WriteQueue = &WriteQueueModule.GetWriteQueue();

	const FIntPoint Size(Target->SizeX, Target->SizeY);
	const bool bFloat = CaptureComponent->IsFloatFormat();
	const FString Filename = BuildFramePath(FrameIndex);
	const EImageFormat Format = (Request.Settings->ImageFormat == ETourImageFormat::JPEG)
		? EImageFormat::JPEG
		: (Request.Settings->ImageFormat == ETourImageFormat::EXR ? EImageFormat::EXR : EImageFormat::PNG);
	const int32 Quality = Request.Settings->CompressionQuality;

	PendingWrites.IncrementExchange();

	// Everything the render command needs is captured by value; nothing reaches back into the
	// backend, so a job that ends while a readback is in flight cannot touch freed state. The
	// two atomics are the only shared state and they outlive the job by construction.
	TAtomic<int32>* PendingCounter = &PendingWrites;
	TAtomic<int32>* FailureCounter = &FailedWrites;

	ENQUEUE_RENDER_COMMAND(ArchVizTourReadback)(
		[Resource, Size, bFloat, Filename, Format, Quality, WriteQueue, PendingCounter, FailureCounter](FRHICommandListImmediate& RHICmdList)
		{
			TUniquePtr<FImageWriteTask> Task = MakeUnique<FImageWriteTask>();
			Task->Filename = Filename;
			Task->Format = Format;
			Task->CompressionQuality = Quality;
			Task->bOverwriteFile = true;

			const FIntRect Rect(0, 0, Size.X, Size.Y);

			// The read happens here, on the render thread. The game thread is never blocked on
			// a GPU fence, which is the whole point of doing it this way rather than calling
			// UTextureRenderTarget2D::ReadPixels from Tick.
			if (bFloat)
			{
				TArray<FFloat16Color> Raw;
				RHICmdList.ReadSurfaceFloatData(Resource->GetRenderTargetTexture(), Rect, Raw, CubeFace_PosX, 0, 0);

				TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(Size);
				PixelData->Pixels = TArray64<FFloat16Color>(Raw.GetData(), Raw.Num());
				Task->PixelData = MoveTemp(PixelData);
			}
			else
			{
				TArray<FColor> Raw;
				FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
				ReadFlags.SetLinearToGamma(false);
				RHICmdList.ReadSurfaceData(Resource->GetRenderTargetTexture(), Rect, Raw, ReadFlags);

				// The scene capture leaves alpha at whatever the render target held; a PNG with
				// a zero alpha channel reads as a fully transparent frame in every viewer.
				for (FColor& Pixel : Raw)
				{
					Pixel.A = 255;
				}

				TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(Size);
				PixelData->Pixels = TArray64<FColor>(Raw.GetData(), Raw.Num());
				Task->PixelData = MoveTemp(PixelData);
			}

			Task->OnCompleted = [PendingCounter, FailureCounter](bool bWriteSucceeded)
			{
				if (!bWriteSucceeded)
				{
					FailureCounter->IncrementExchange();
				}
				PendingCounter->DecrementExchange();
			};

			WriteQueue->Enqueue(MoveTemp(Task));
		});
}

FString FTourRenderBackend_SceneCapture::BuildFramePath(int32 FrameIndex) const
{
	check(Request.Settings != nullptr);

	const FString TourName = (Request.Tour != nullptr && !Request.Tour->TourTitle.IsEmpty())
		? Request.Tour->TourTitle.ToString()
		: (Request.Tour != nullptr ? Request.Tour->GetName() : TEXT("Tour"));

	const FString Relative = Request.Settings->BuildRelativeFileName(TourName, FrameIndex, Request.Timestamp);

	return FPaths::SetExtension(Request.OutputDirectory / Relative, Request.Settings->GetImageExtension());
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

FString FTourRenderBackend_SceneCapture::ResolveFFmpegExecutable() const
{
	using namespace ArchVizTour::SceneCaptureBackend;

	if (Request.Settings != nullptr && !Request.Settings->FFmpegExecutablePath.IsEmpty())
	{
		const FString Configured = Request.Settings->FFmpegExecutablePath;
		if (FPaths::FileExists(Configured))
		{
			return Configured;
		}

		UE_LOG(LogArchVizTour, Warning,
			TEXT("The configured ffmpeg path '%s' does not exist; falling back to '%s' on PATH."),
			*Configured, DefaultFFmpegName);
	}

	// Leaving it as a bare name defers resolution to the OS, which is the only portable way to
	// find a tool the user installed through a package manager.
	return DefaultFFmpegName;
}

bool FTourRenderBackend_SceneCapture::EncodeVideo(FString& OutVideoPath, FString& OutError)
{
	using namespace ArchVizTour::SceneCaptureBackend;

	check(Request.Settings != nullptr);

	if (FramesCaptured <= 0)
	{
		OutError = TEXT("There are no frames to encode.");
		return false;
	}

	const FString TourName = (Request.Tour != nullptr && !Request.Tour->TourTitle.IsEmpty())
		? Request.Tour->TourTitle.ToString()
		: TEXT("Tour");

	// ffmpeg consumes a printf-style pattern, so the {frame} token is replaced by %0Nd rather
	// than by a number.
	FString InputPattern = Request.Settings->BuildRelativeFileName(TourName, 0, Request.Timestamp);
	{
		const FString FirstFrameText = FString::Printf(TEXT("%0*d"), FMath::Clamp(Request.Settings->FrameNumberPadding, 1, 10), 0);
		const FString PrintfToken = FString::Printf(TEXT("%%0%dd"), FMath::Clamp(Request.Settings->FrameNumberPadding, 1, 10));
		InputPattern.ReplaceInline(*FirstFrameText, *PrintfToken, ESearchCase::CaseSensitive);
	}

	const FString InputPath = FPaths::SetExtension(Request.OutputDirectory / InputPattern, Request.Settings->GetImageExtension());
	const FString OutputPath = FPaths::SetExtension(
		Request.OutputDirectory / Request.Settings->BuildRelativeFileName(TourName, 0, Request.Timestamp),
		Request.Settings->VideoExtension);

	const FString Executable = ResolveFFmpegExecutable();

	FString Arguments = Request.Settings->FFmpegArgumentTemplate;
	Arguments.ReplaceInline(TEXT("{ffmpeg}"),  *Executable, ESearchCase::IgnoreCase);
	Arguments.ReplaceInline(TEXT("{fps}"),     *FString::SanitizeFloat(Request.Settings->FrameRate), ESearchCase::IgnoreCase);
	Arguments.ReplaceInline(TEXT("{input}"),   *InputPath, ESearchCase::IgnoreCase);
	Arguments.ReplaceInline(TEXT("{output}"),  *OutputPath, ESearchCase::IgnoreCase);
	Arguments.ReplaceInline(TEXT("{quality}"), *FString::FromInt(Request.Settings->EncoderQuality), ESearchCase::IgnoreCase);

	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		OutError = TEXT("Could not create a pipe for the encoder's output.");
		return false;
	}

	// This drains the child's stderr synchronously, so the game thread is blocked for the
	// duration of the encode. That is deliberate: encoding runs once, after the last frame has
	// been written and the tour has already been stopped, so there is nothing left to keep
	// responsive - and an asynchronous encode would have to keep the whole job alive across an
	// arbitrary number of frames just to report a single result.
	UE_LOG(LogArchVizTour, Log, TEXT("Encoding: %s %s"), *Executable, *Arguments);

	FProcHandle Process = FPlatformProcess::CreateProc(
		*Executable, *Arguments,
		/*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
		/*OutProcessID*/ nullptr, /*PriorityModifier*/ 0, /*OptionalWorkingDirectory*/ nullptr,
		/*PipeWriteChild*/ WritePipe, /*PipeReadChild*/ ReadPipe);

	if (!Process.IsValid())
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		OutError = FString::Printf(
			TEXT("Could not launch '%s'. Install ffmpeg or set its path in Project Settings > Plugins > ArchViz Tour; the rendered image sequence has been kept."),
			*Executable);
		return false;
	}

	// ffmpeg reports progress on stderr as "frame=  123 ...". Draining the pipe as it runs both
	// surfaces progress and prevents the child blocking on a full pipe buffer.
	FString Output;
	while (FPlatformProcess::IsProcRunning(Process))
	{
		const FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
		if (!Chunk.IsEmpty())
		{
			Output += Chunk;

			const int32 FrameKeyIndex = Chunk.Find(TEXT("frame="), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (FrameKeyIndex != INDEX_NONE)
			{
				const FString Tail = Chunk.Mid(FrameKeyIndex + 6).TrimStart();
				const int32 EncodedFrame = FCString::Atoi(*Tail);
				if (EncodedFrame > 0)
				{
					OnProgress.Broadcast(
						FMath::Clamp(static_cast<float>(EncodedFrame) / static_cast<float>(FMath::Max(FramesCaptured, 1)), 0.0f, 1.0f),
						EncodedFrame,
						FramesCaptured);
				}
			}
		}

		FPlatformProcess::Sleep(static_cast<float>(EncoderPollIntervalMs) / 1000.0f);
	}

	Output += FPlatformProcess::ReadPipe(ReadPipe);

	int32 ReturnCode = 0;
	FPlatformProcess::GetProcReturnCode(Process, &ReturnCode);
	FPlatformProcess::CloseProc(Process);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

	if (ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("ffmpeg exited with code %d. Output:\n%s"), ReturnCode, *Output.Right(2048));
		return false;
	}

	OutVideoPath = OutputPath;

	if (Request.Settings->bDeleteFramesAfterEncode)
	{
		for (int32 FrameIndex = 0; FrameIndex < FramesCaptured; ++FrameIndex)
		{
			IFileManager::Get().Delete(*BuildFramePath(FrameIndex), /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true);
		}
	}

	UE_LOG(LogArchVizTour, Log, TEXT("Encoded '%s'."), *OutputPath);
	return true;
}

#undef LOCTEXT_NAMESPACE
