// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class UTourRenderSettings;
class UTourSequencePreset;
class UWorld;

/** Everything a backend needs to start a job, resolved once by UTourRenderSubsystem. */
struct ARCHVIZTOURCAPTURE_API FTourRenderRequest
{
	/** World the tour plays in. Weak, because a job outliving its world must abort, not crash. */
	TWeakObjectPtr<UWorld> World;

	/** Tour to render. */
	TObjectPtr<UTourSequencePreset> Tour;

	/** Job configuration, already sanitised. */
	TObjectPtr<UTourRenderSettings> Settings;

	/** Absolute output directory, already resolved from the settings. */
	FString OutputDirectory;

	/** Timestamp used for the {date} and {time} tokens, so every frame of a job agrees. */
	FDateTime Timestamp = FDateTime::Now();

	/** Total frames the job will produce. Always at least 1. */
	int32 TotalFrames = 1;

	/** Length of the render in seconds. */
	float DurationSeconds = 0.0f;

	/** True when the request has everything a backend needs. */
	bool IsValid() const;
};

/** Fired as frames complete. Alpha is 0..1. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnTourRenderBackendProgress, float /*Alpha*/, int32 /*Frame*/, int32 /*TotalFrames*/);

/** Fired once, when the job ends for any reason. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnTourRenderBackendCompleted, bool /*bSuccess*/, const FString& /*OutputPath*/, const FString& /*Error*/);

/**
 * One way of turning a tour into image files.
 *
 * Two implementations exist and both are always compiled into the module's interface, so the
 * subsystem's logic does not branch on which is present: Movie Render Pipeline when its plugin
 * is packaged, and a SceneCapture pipeline that is available everywhere.
 *
 * Backends own UObjects (render targets, pipeline configuration) and therefore derive from
 * FGCObject: a bare C++ object holding a raw UObject pointer across frames is a collected
 * pointer waiting to happen.
 */
class ARCHVIZTOURCAPTURE_API ITourRenderBackend : public FGCObject
{
public:
	virtual ~ITourRenderBackend() = default;

	/** Short name used in logs and in the completion message. */
	virtual FName GetBackendName() const = 0;

	/**
	 * Whether this backend can run in the current build.
	 * @param OutUnavailableReason  Filled with a user-facing explanation when the answer is no.
	 */
	virtual bool IsAvailable(FString& OutUnavailableReason) const = 0;

	/**
	 * Begin a job.
	 * @param Request  Fully resolved job description.
	 * @return false when the job could not start; OnCompleted has already fired in that case.
	 */
	virtual bool Start(const FTourRenderRequest& Request) = 0;

	/**
	 * Advance the job by one frame.
	 * @param DeltaSeconds  Real seconds since the last tick.
	 * @return true while the job is still running.
	 */
	virtual bool Tick(float DeltaSeconds) = 0;

	/** Abort the job. OnCompleted fires with bSuccess false. Safe to call when not running. */
	virtual void Cancel() = 0;

	/** True between a successful Start and the completion callback. */
	virtual bool IsRunning() const = 0;

	FOnTourRenderBackendProgress OnProgress;
	FOnTourRenderBackendCompleted OnCompleted;

	// --- FGCObject --------------------------------------------------------
	virtual FString GetReferencerName() const override;
};
