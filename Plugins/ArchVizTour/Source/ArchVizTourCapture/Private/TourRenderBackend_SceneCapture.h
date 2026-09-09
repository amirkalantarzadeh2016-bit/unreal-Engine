// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TourRenderBackend.h"
#include "TourRenderSettings.h"
#include "UObject/ObjectPtr.h"

class AActor;
class ATourPath;
class UTourCaptureComponent;
class UTourSubsystem;

/**
 * Backend B: SceneCapture2D into a render target, read back on the render thread and written
 * by ImageWriteQueue, optionally encoded by an external ffmpeg.
 *
 * Always available: no optional plugin, no baked Level Sequence, no editor. Quality is
 * whatever the live renderer produces, which is the trade for working everywhere.
 *
 * Two details make the output frame-accurate rather than merely fast:
 *  - The engine is put into fixed timestep at exactly the output frame rate for the duration
 *    of the job, so one game frame is one output frame regardless of how long it took to
 *    render. Without it a slow frame advances the tour further and the result stutters.
 *  - Readback is enqueued on the render thread and the pixels are handed to ImageWriteQueue,
 *    so the game thread never stalls on a GPU fence or on file I/O.
 */
class FTourRenderBackend_SceneCapture : public ITourRenderBackend
{
public:
	FTourRenderBackend_SceneCapture() = default;
	virtual ~FTourRenderBackend_SceneCapture() override;

	// --- ITourRenderBackend ----------------------------------------------
	virtual FName GetBackendName() const override { return TEXT("SceneCapture"); }
	virtual bool IsAvailable(FString& OutUnavailableReason) const override;
	virtual bool Start(const FTourRenderRequest& Request) override;
	virtual bool Tick(float DeltaSeconds) override;
	virtual void Cancel() override;
	virtual bool IsRunning() const override { return bRunning; }

	// --- FGCObject --------------------------------------------------------
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	/** Put the engine into fixed timestep at the job's frame rate, remembering what to restore. */
	void EnterFixedTimeStep();

	/** Restore the timing mode the engine was in before the job. */
	void RestoreTimeStep();

	/** Hide every tour rail mesh, remembering which were visible. */
	void HideTourGizmos();

	/** Restore the rail meshes hidden by HideTourGizmos. */
	void RestoreTourGizmos();

	/** Point the capture at the player's current view and render one frame. */
	void CaptureCurrentFrame();

	/** Enqueue an asynchronous readback of the render target for a frame index. */
	void EnqueueReadback(int32 FrameIndex);

	/** Finish the job: encode if asked, then fire OnCompleted exactly once. */
	void FinishJob(bool bSuccess, const FString& Error);

	/** Run ffmpeg over the written frame sequence. @return true when a video was produced. */
	bool EncodeVideo(FString& OutVideoPath, FString& OutError);

	/** Locate an ffmpeg executable: the settings' path, then the editor default, then PATH. */
	FString ResolveFFmpegExecutable() const;

	/** Absolute path, with extension, of one frame's image file. */
	FString BuildFramePath(int32 FrameIndex) const;

	/** Job description, held for the life of the job. */
	FTourRenderRequest Request;

	/** Capture rig spawned for the job and destroyed with it. */
	TObjectPtr<AActor> CaptureActor;

	/** The capture component on CaptureActor. */
	TObjectPtr<UTourCaptureComponent> CaptureComponent;

	/** Frames written so far. */
	int32 FramesCaptured = 0;

	/** True between Start and FinishJob. */
	bool bRunning = false;

	/** Set by Cancel so the next tick tears the job down on the game thread. */
	bool bCancelRequested = false;

	/** True once FinishJob has fired, so OnCompleted cannot broadcast twice. */
	bool bCompleted = false;

	/** Engine timing state captured by EnterFixedTimeStep. */
	bool bRestoreFixedTimeStep = false;
	bool bPreviousUseFixedTimeStep = false;
	double PreviousFixedDeltaTime = 0.0;

	/** Rail meshes hidden for the job, so exactly those can be restored. */
	TArray<TWeakObjectPtr<ATourPath>> HiddenRailPaths;

	/** Outstanding readbacks, so the job does not finish before the last frame is written. */
	TAtomic<int32> PendingWrites{ 0 };

	/** Frames whose write failed; a non-zero count fails the job. */
	TAtomic<int32> FailedWrites{ 0 };
};
