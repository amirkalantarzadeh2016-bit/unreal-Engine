// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TourRenderSettings.h"

#include "TourRenderSubsystem.generated.h"

class ITourRenderBackend;
class UTourSequencePreset;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTourRenderProgress, float, Alpha, int32, Frame, int32, TotalFrames);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTourRenderCompleted, bool, bSuccess, const FString&, OutputPath, const FString&, Error);

/**
 * The single entry point for rendering a tour to disk.
 *
 * A game instance subsystem rather than a world subsystem: a render outlives level transitions
 * in principle, and there is never a reason to have two of them running at once. Only one job
 * runs at a time, and starting a second is refused rather than silently queued - two
 * concurrent renders would fight over the fixed timestep and produce two ruined outputs.
 *
 * Ticked by FTSTicker, so no actor or component has to exist for a render to progress.
 */
UCLASS(DisplayName = "Tour Render Subsystem")
class ARCHVIZTOURCAPTURE_API UTourRenderSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- USubsystem -------------------------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Blueprint-friendly accessor. Returns null outside a game instance. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Render", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Tour Render Subsystem"))
	static UTourRenderSubsystem* Get(const UObject* WorldContextObject);

	/** Fires as frames complete. Alpha is 0..1. */
	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Render")
	FOnTourRenderProgress OnRenderProgress;

	/** Fires once when a job ends, successfully or not. */
	UPROPERTY(BlueprintAssignable, Category = "ArchViz Tour|Render")
	FOnTourRenderCompleted OnRenderCompleted;

	/**
	 * Start rendering a tour.
	 *
	 * @param Tour      Tour to render. Must have at least one step.
	 * @param Settings  Job configuration. Null uses defaults; the object is sanitised in place.
	 * @return true when a job started. On false, OnRenderCompleted has already fired with the
	 *         reason, so a caller can bind once and handle both paths in the same place.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Render")
	bool StartRender(UTourSequencePreset* Tour, UTourRenderSettings* Settings);

	/** Abort the running job. No-op when nothing is rendering. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Render")
	void CancelRender();

	/** True while a job is running. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Render")
	bool IsRendering() const;

	/** Name of the backend the running job chose, or None. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Render")
	FName GetActiveBackendName() const;

	/**
	 * Whether a backend can run in this build.
	 * @param Backend  Backend to test. Automatic reports on whichever would be chosen.
	 * @param OutReason  User-facing explanation when the answer is no.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Render")
	bool IsBackendAvailable(ETourRenderBackend Backend, FString& OutReason) const;

	/** Settings object used when StartRender is passed null. Editable so a UI can bind to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ArchViz Tour|Render", Instanced)
	TObjectPtr<UTourRenderSettings> DefaultSettings;

private:
	/** FTSTicker callback that drives the active backend. */
	bool HandleTick(float DeltaSeconds);

	/** Relay a backend's native progress delegate onto the dynamic one. */
	void HandleBackendProgress(float Alpha, int32 Frame, int32 TotalFrames);

	/** Relay a backend's native completion delegate and release the job. */
	void HandleBackendCompleted(bool bSuccess, const FString& OutputPath, const FString& Error);

	/** Construct the backend a request should use, honouring Automatic and availability. */
	TSharedPtr<ITourRenderBackend> CreateBackend(ETourRenderBackend Requested, const UTourSequencePreset* Tour, FString& OutReason) const;

	/** Report a job that never started, through the same delegate a finished job uses. */
	void ReportImmediateFailure(const FString& Error);

	/** The world a render runs in: the game instance's current world. */
	UWorld* GetRenderWorld() const;

	/** The running job, or null. Shared so the tick callback can outlive a reassignment safely. */
	TSharedPtr<ITourRenderBackend> ActiveBackend;

	/** Ticker handle, removed on Deinitialize. */
	FTSTicker::FDelegateHandle TickHandle;
};
