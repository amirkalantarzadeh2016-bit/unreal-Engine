// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TourRenderBackend.h"
#include "UObject/ObjectPtr.h"

#if WITH_ARCHVIZTOUR_MRP
class UMoviePipeline;
class UMoviePipelineExecutorJob;
class UMoviePipelineQueue;
struct FMoviePipelineOutputData;
#endif

/**
 * Backend A: Movie Render Pipeline running inside a game process.
 *
 * Renders the tour's baked ULevelSequence with real accumulation-based anti-aliasing, motion
 * blur and warm-up frames - the quality bar an ArchViz deliverable is judged against, which a
 * single live frame per output frame cannot reach.
 *
 * Two hard requirements, both reported through IsAvailable rather than discovered at runtime:
 *  1. The MovieRenderPipeline plugin must be enabled AND packaged. It is optional in
 *     ArchVizTour.uplugin, so a project without it still builds; WITH_ARCHVIZTOUR_MRP is 0
 *     there and this backend reports itself unavailable.
 *  2. The tour must have a baked Level Sequence (UTourSequencePreset::BakedSequence), produced
 *     by the editor's "Create Level Sequence" action. MRP renders sequences, not procedural
 *     step lists, so there is nothing to fall back to.
 */
class FTourRenderBackend_MoviePipeline : public ITourRenderBackend
{
public:
	FTourRenderBackend_MoviePipeline() = default;
	virtual ~FTourRenderBackend_MoviePipeline() override;

	// --- ITourRenderBackend ----------------------------------------------
	virtual FName GetBackendName() const override { return TEXT("MovieRenderPipeline"); }
	virtual bool IsAvailable(FString& OutUnavailableReason) const override;
	virtual bool Start(const FTourRenderRequest& Request) override;
	virtual bool Tick(float DeltaSeconds) override;
	virtual void Cancel() override;
	virtual bool IsRunning() const override { return bRunning; }

	// --- FGCObject --------------------------------------------------------
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	/** Job description, held for the life of the job. */
	FTourRenderRequest Request;

	/** True between a successful Start and the completion callback. */
	bool bRunning = false;

	/** True once the completion delegate has fired, so it cannot broadcast twice. */
	bool bCompleted = false;

#if WITH_ARCHVIZTOUR_MRP
	/** Build the queue, job and configuration for the request. @return null on failure. */
	UMoviePipelineExecutorJob* BuildPipelineJob(FString& OutError);

	/** Populate the primary config's render pass, output and quality settings. */
	void ConfigureJobSettings(UMoviePipelineExecutorJob* Job);

	/** Pipeline completion callback. */
	void HandleWorkFinished(FMoviePipelineOutputData OutputData);

	/** Fire OnCompleted exactly once and tear the job down. */
	void FinishJob(bool bSuccess, const FString& Error);

	/** Restore any rail meshes hidden for the job. */
	void RestoreTourGizmos();

	/** Queue holding the single job; UMoviePipeline reads the job's configuration from it. */
	TObjectPtr<UMoviePipelineQueue> PipelineQueue;

	/**
	 * The pipeline itself.
	 *
	 * Driven directly rather than through an executor: UMoviePipelinePIEExecutor is
	 * editor-only, and UMoviePipelineLinearExecutorBase is abstract, so in a packaged game the
	 * pipeline is the thing to hold. It installs its own engine-tick hooks in Initialize, so
	 * this backend's Tick only reports state.
	 */
	TObjectPtr<UMoviePipeline> Pipeline;

	/** Rail meshes hidden for the job. */
	TArray<TWeakObjectPtr<class ATourPath>> HiddenRailPaths;
#endif
};
