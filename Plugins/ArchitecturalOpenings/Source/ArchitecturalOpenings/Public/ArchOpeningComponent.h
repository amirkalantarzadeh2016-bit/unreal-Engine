// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ArchOpeningTypes.h"

#include "ArchOpeningComponent.generated.h"

class AActor;
class UArchOpeningPreset;
class UAudioComponent;
class UBoxComponent;
class UPrimitiveComponent;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArchOpeningEvent, UArchOpeningComponent*, Opening);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FArchOpeningObstructionEvent, UArchOpeningComponent*, Opening, AActor*, Obstructor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FArchOpeningInteractionEvent, UArchOpeningComponent*, Opening, AActor*, Interactor);

/**
 * Drives one architectural opening: a single hinged leaf or a single sliding panel, any number of
 * meshes moving rigidly as that leaf, any number of handle groups, plus the stationary parts the
 * opening knows about but never moves.
 *
 * Ownership model
 * ---------------
 * The component REFERENCES the parts it drives. It never reparents them, never merges them and
 * never touches their assets. Each part keeps its own actor, its own materials and its own place in
 * the level. What the component owns is the calibration: one rest transform per part, expressed in
 * the component's own local frame (see FArchOpeningSolver for the maths). Animated poses are
 * recomputed from that rest every frame, never accumulated, so the setup cannot drift.
 *
 * The consequences of that choice, stated plainly:
 *  - Moving or rotating the opening component carries the whole configured assembly with it.
 *  - Moving an assigned mesh in the level while the opening is closed does NOT recalibrate it; use
 *    "Set Current Pose As Closed" for that.
 *  - Nothing about a Blueprint's construction script or component template is modified, because no
 *    attachment is ever changed.
 *
 * Component ownership restrictions are listed in Docs/SetupGuide.md; the short version is that an
 * assigned component must be a USceneComponent in the same world, must not be the opening component
 * itself or one of its ancestors (that would be a cyclic drive), and must be Movable to animate.
 */
UCLASS(ClassGroup = (ArchitecturalOpenings), meta = (BlueprintSpawnableComponent, DisplayName = "Architectural Opening"))
class ARCHITECTURALOPENINGS_API UArchOpeningComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UArchOpeningComponent();

	// ----------------------------------------------------------------------------------------
	// Configuration
	// ----------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening Type")
	EArchOpeningMotionType MotionType = EArchOpeningMotionType::Hinged;

	/** Meshes the opening knows about but deliberately never moves: outer frame, fixed glazing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assigned Parts")
	TArray<FArchOpeningPartRef> StationaryParts;

	/** Meshes that move rigidly as one leaf: leaf PVC profiles, its glass, its trim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assigned Parts")
	TArray<FArchOpeningPartRef> LeafParts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	FArchOpeningCalibration Calibration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hinged Motion",
		meta = (EditCondition = "MotionType == EArchOpeningMotionType::Hinged", EditConditionHides))
	FArchOpeningHingedSettings Hinged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sliding Motion",
		meta = (EditCondition = "MotionType == EArchOpeningMotionType::Sliding", EditConditionHides))
	FArchOpeningSlidingSettings Sliding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	FArchOpeningTimingSettings Timing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles")
	TArray<FArchOpeningHandleGroup> HandleGroups;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	FArchOpeningInteractionSettings Interaction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity")
	FArchOpeningProximitySettings Proximity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction")
	FArchOpeningObstructionSettings Obstruction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FArchOpeningAudioSettings Audio;

	/**
	 * Promote assigned leaf and handle components to Movable mobility when calibrating.
	 * Stationary parts are never touched. See Docs/SetupGuide.md for the lighting consequences.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	bool bPromoteDrivenPartsToMovable = true;

	/**
	 * Editor only. When the opening component is moved in the level after calibration, rigidly
	 * carry every assigned part (stationary included) with it and re-anchor the calibration, so the
	 * configured assembly keeps its world-space relationship to the opening.
	 *
	 * With this off, moving the opening leaves the meshes behind and validation reports the stale
	 * frame instead.
	 */
	UPROPERTY(EditAnywhere, Category = "Calibration")
	bool bMovePartsWithOpening = true;

	// ----------------------------------------------------------------------------------------
	// Events
	// ----------------------------------------------------------------------------------------

	/** Fired once when a transition toward open actually begins moving the leaf. */
	UPROPERTY(BlueprintAssignable, Category = "Opening|Events")
	FArchOpeningEvent OnOpeningStarted;

	UPROPERTY(BlueprintAssignable, Category = "Opening|Events")
	FArchOpeningEvent OnFullyOpened;

	UPROPERTY(BlueprintAssignable, Category = "Opening|Events")
	FArchOpeningEvent OnClosingStarted;

	UPROPERTY(BlueprintAssignable, Category = "Opening|Events")
	FArchOpeningEvent OnFullyClosed;

	/** Fired when motion ends without reaching an endpoint, for example after Stop(). */
	UPROPERTY(BlueprintAssignable, Category = "Opening|Events")
	FArchOpeningEvent OnMotionStopped;

	UPROPERTY(BlueprintAssignable, Category = "Opening|Events")
	FArchOpeningObstructionEvent OnObstructionDetected;

	UPROPERTY(BlueprintAssignable, Category = "Opening|Events")
	FArchOpeningInteractionEvent OnInteractionAccepted;

	// ----------------------------------------------------------------------------------------
	// Blueprint API
	// ----------------------------------------------------------------------------------------

	/** Requests a transition to fully open. Safe to call repeatedly; repeats are ignored. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Commands")
	void Open(EArchOpeningCommandSource Source = EArchOpeningCommandSource::Script);

	/** Requests a transition to fully closed. Safe to call repeatedly; repeats are ignored. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Commands")
	void Close(EArchOpeningCommandSource Source = EArchOpeningCommandSource::Script);

	/** Opens if currently closed or closing, closes otherwise. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Commands")
	void Toggle(EArchOpeningCommandSource Source = EArchOpeningCommandSource::Script);

	/** Halts motion at the current pose and cancels any pending delay. Fires OnMotionStopped. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Commands")
	void Stop();

	/**
	 * Jumps straight to an openness with no animation, no delays, no sounds and no transition
	 * events. Use for authoring a starting pose; use SetOpennessAnimated for gameplay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Opening|Commands")
	void SetOpennessImmediate(float NewOpenness);

	/**
	 * Animates to an openness using the configured timing and easing, starting from the current
	 * pose. Reaching 0 or 1 fires the matching Fully Closed / Fully Opened event.
	 */
	UFUNCTION(BlueprintCallable, Category = "Opening|Commands")
	void SetOpennessAnimated(float TargetOpenness);

	UFUNCTION(BlueprintPure, Category = "Opening|State")
	float GetOpenness() const { return Openness; }

	UFUNCTION(BlueprintPure, Category = "Opening|State")
	EArchOpeningState GetOpeningState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Opening|State")
	bool IsMoving() const;

	/** True when the leaf is fully closed and no transition is pending. */
	UFUNCTION(BlueprintPure, Category = "Opening|State")
	bool IsClosed() const { return State == EArchOpeningState::Closed; }

	UFUNCTION(BlueprintCallable, Category = "Opening|Interaction")
	void SetInteractionEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Opening|Interaction")
	bool IsInteractionEnabled() const { return Interaction.bInteractionEnabled; }

	/**
	 * Entry point for click interaction. Applies the enabled check and the interaction mode, fires
	 * OnInteractionAccepted, then toggles. Returns true if the interaction was accepted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Opening|Interaction")
	bool HandleClickInteraction(AActor* Interactor);

	/** True if the component would resolve a click on this scene component to this opening. */
	UFUNCTION(BlueprintPure, Category = "Opening|Interaction")
	bool IsComponentClickable(const USceneComponent* Component) const;

	/** Applies a preset's motion, timing, handle behaviour, interaction, audio and obstruction settings. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Configuration")
	void ApplyPreset(const UArchOpeningPreset* Preset, bool bApplyHandleBehaviour = true);

	/** Runs every validation rule and returns the report. Cheap enough for editor UI refreshes. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Diagnostics")
	FArchOpeningValidationReport Validate() const;

	// ----------------------------------------------------------------------------------------
	// Calibration (available at runtime, but intended as an authoring operation)
	// ----------------------------------------------------------------------------------------

	/**
	 * Captures the current world pose of every assigned part as the closed configuration.
	 *
	 * Refuses, and returns false, while an editor preview is showing a non-closed pose, so a
	 * preview can never be mistaken for the authored closed pose.
	 */
	UFUNCTION(BlueprintCallable, Category = "Opening|Calibration")
	bool SetCurrentPoseAsClosed();

	/** Snaps every assigned part back to its calibrated closed pose and sets openness to 0. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Calibration")
	void ResetToClosedPose();

	/** The calibration frame: component rotation and location, uniform scale only when uniform. */
	UFUNCTION(BlueprintPure, Category = "Opening|Calibration")
	FTransform GetCalibrationFrame() const;

	/** Union of the calibrated leaf bounds, in the calibration frame. Invalid if uncalibrated. */
	FBox GetLeafLocalBounds() const;

	/** True when the component has moved away from the frame the rest poses were captured in. */
	UFUNCTION(BlueprintPure, Category = "Opening|Calibration")
	bool IsCalibrationFrameStale() const;

	// ----------------------------------------------------------------------------------------
	// Editor-only authoring and preview
	// ----------------------------------------------------------------------------------------

#if WITH_EDITOR
	/** Assigns a component to a role, removing it from any other role on this opening first. */
	bool AssignPart(USceneComponent* Component, EArchOpeningPartRole Role, int32 HandleGroupIndex, FText& OutError);

	/** Removes a component from every role on this opening. Returns true if anything changed. */
	bool UnassignPart(USceneComponent* Component);

	/** Empties one role. HandleGroupIndex < 0 clears every handle group's parts. */
	void ClearRole(EArchOpeningPartRole Role, int32 HandleGroupIndex = INDEX_NONE);

	/** Places the hinge on the calibrated leaf bounds edge implied by the current presets. */
	void SnapHingeToLeafEdge();

	/** Sets the slide direction and travel distance from the calibrated leaf bounds. */
	void SnapSlideToLeafBounds();

	/** Places a handle group's pivot at the centre of that group's calibrated bounds. */
	void SnapHandlePivotToGroupBounds(int32 GroupIndex);

	/**
	 * Rigidly moves every assigned part by the change in the calibration frame and re-anchors the
	 * calibration to the component's current transform. Callers are responsible for the transaction.
	 */
	void TransportPartsWithFrameChange();

	//~ Begin USceneComponent Interface
	virtual void PostEditComponentMove(bool bFinished) override;
	//~ End USceneComponent Interface

	bool IsPreviewActive() const { return bPreviewActive; }

	/** Begins previewing and remembers the pre-preview openness so it can be restored exactly. */
	void PreviewOpen();
	void PreviewClose();
	void PreviewToggle();

	/** Ends preview animation but leaves the leaf where it is. */
	void StopPreview();

	/** Scrub control. Begins a preview session if one is not already running. */
	void SetPreviewOpenness(float NewOpenness);

	/** Ends the preview session and restores the openness captured when it began. */
	void RestorePrePreviewPose();

	/** Advanced by the editor module's ticker. Never uses the gameplay world tick. */
	void TickPreview(float DeltaSeconds);

	/** True while a preview session is running or has left the leaf away from its pre-preview pose. */
	bool HasPendingPreviewState() const { return bPreviewActive || bPrePreviewCaptured; }

	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif // WITH_EDITOR

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	//~ Begin UObject Interface
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostLoad() override;
	//~ End UObject Interface

	/** Iterates every part reference across every role. Predicate returns false to stop. */
	void ForEachPart(TFunctionRef<bool(const FArchOpeningPartRef&, EArchOpeningPartRole, int32)> Predicate) const;

	/** The trigger volume created at BeginPlay when proximity is enabled. Null in the editor. */
	UBoxComponent* GetProximityVolume() const { return ProximityVolume; }

protected:
	// ----------------------------------------------------------------------------------------
	// Runtime state
	// ----------------------------------------------------------------------------------------

	/** Geometric openness, 0 closed to 1 fully open. The single source of truth for the pose. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Diagnostics")
	float Openness = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Diagnostics")
	EArchOpeningState State = EArchOpeningState::Closed;

	/** Normalised progress along the active transition's easing curve. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Diagnostics")
	float TransitionProgress = 0.0f;

	/** Openness the active transition starts from and ends at. */
	UPROPERTY(Transient)
	float TransitionStartOpenness = 0.0f;

	UPROPERTY(Transient)
	float TransitionEndOpenness = 0.0f;

	/** True while the active transition is heading toward a larger openness. */
	UPROPERTY(Transient)
	bool bTransitionOpening = false;

	/** Seconds remaining in the current delay or handle preparation state. */
	UPROPERTY(Transient)
	float StateTimer = 0.0f;

	/** Single deferred-close timer shared by auto-close and proximity close. Negative = inactive. */
	UPROPERTY(Transient)
	float PendingCloseTimer = -1.0f;

	UPROPERTY(Transient)
	EArchOpeningCommandSource PendingCloseSource = EArchOpeningCommandSource::Script;

	UPROPERTY(Transient)
	EArchOpeningCommandSource LastCommandSource = EArchOpeningCommandSource::Script;

	/** Proximity occupancy, counted per actor so several overlapping components cannot corrupt it. */
	TMap<TWeakObjectPtr<AActor>, int32> ProximityOccupancy;

	/**
	 * Set when a qualifying occupant closes the opening by hand while still inside the trigger.
	 * Blocks proximity re-opening until the trigger is empty again.
	 */
	UPROPERTY(Transient)
	bool bProximityReopenSuppressed = false;

	UPROPERTY(Transient)
	float ObstructionCheckAccumulator = 0.0f;

	UPROPERTY(Transient)
	float ObstructionClearTimer = 0.0f;

	/** Openness the Reopen policy is backing off to. Negative when not backing off. */
	UPROPERTY(Transient)
	float ObstructionReopenTarget = -1.0f;

	/** Transition that was interrupted by an obstruction, resumed once the way is clear. */
	UPROPERTY(Transient)
	bool bResumeClosingAfterObstruction = false;

	TWeakObjectPtr<AActor> CurrentObstructor;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MovementLoopComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> ProximityVolume = nullptr;

	/** Cached leaf bounds in calibration space, rebuilt whenever calibration changes. */
	FBox CachedLeafLocalBounds = FBox(ForceInit);

	UPROPERTY(Transient)
	bool bLeafBoundsCached = false;

	/** Curve validation results, refreshed on load, on property edit and at BeginPlay. */
	UPROPERTY(Transient)
	bool bOpeningCurveValid = false;

	UPROPERTY(Transient)
	bool bClosingCurveValid = false;

	/** Keys already logged, so a persistent misconfiguration cannot spam the log. */
	TSet<FName> LoggedIssueKeys;

	UPROPERTY(Transient)
	bool bPreviewActive = false;

	UPROPERTY(Transient)
	bool bPrePreviewCaptured = false;

	UPROPERTY(Transient)
	float PrePreviewOpenness = 0.0f;

	// ----------------------------------------------------------------------------------------
	// Internals
	// ----------------------------------------------------------------------------------------

	void RefreshCurveValidity();
	void RebuildLeafBoundsCache();

	/** Applies the pose implied by Openness and the handle alphas to every driven part. */
	void ApplyPose();

	/** Drives one part list with a delta. Parents are written before their children. */
	void ApplyDeltaToParts(const TArray<FArchOpeningPartRef>& Parts, const FTransform& Delta, const FTransform& Frame);

	/** Duration of a full open or close, honouring the timing mode. Always > 0. */
	float ResolveTransitionDuration(bool bOpening) const;

	EArchOpeningEasing ResolveEasing(bool bOpening) const;
	const UCurveFloat* ResolveCurve(bool bOpening) const;
	bool ResolveCurveValidity(bool bOpening) const;

	/**
	 * Starts (or retargets) a transition toward TargetOpenness.
	 *
	 * The transition is always re-anchored on the current pose and its duration scaled by the
	 * fraction of full travel it covers, so a mid-motion reversal is positionally continuous and a
	 * short remaining distance takes a correspondingly short time. Velocity is not continuous
	 * across a reversal; see the implementation comment for why that is deliberate.
	 */
	void BeginTransition(float TargetOpenness, bool bAllowHandleSequence, EArchOpeningCommandSource Source);

	/**
	 * Transitions to a new state and runs its entry side effects.
	 * @param bArrivedByMotion  True only when the leaf reached this state by finishing a transition.
	 *                          Endpoint sounds and the Fully Opened / Fully Closed events are gated
	 *                          on it, so a Stop() that lands near an endpoint cannot fake an arrival.
	 */
	void EnterState(EArchOpeningState NewState, bool bArrivedByMotion = false);
	void UpdateTickEnabled();
	bool NeedsTick() const;

	void TickDelays(float DeltaTime);
	void TickTransition(float DeltaTime);
	void TickHandles(float DeltaTime);
	void TickObstruction(float DeltaTime);

	/** Longest DelayBeforeLeafMovement across the groups actuating for this transition. */
	float ComputeHandlePreparationTime(bool bOpening) const;
	void SetHandleTargets(bool bActuated, bool bOnlyGroupsThatActuateOnClosing);
	bool AnyHandleActuatesFor(bool bOpening) const;
	bool AnyHandleStillAnimating() const;

	void RequestDeferredClose(float Delay, EArchOpeningCommandSource Source);
	void CancelDeferredClose();

	// Audio
	void PlayOneShot(USoundBase* Sound);
	void StartMovementLoop();
	void StopMovementLoop();
	USceneComponent* GetAudioAnchor() const;
	bool AreSoundsAllowed() const;

	// Proximity
	void CreateProximityVolume();
	void DestroyProximityVolume();
	bool IsQualifyingOccupant(const AActor* Actor) const;
	void RefreshInitialOccupancy();

	UFUNCTION()
	void HandleProximityBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleProximityEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Fires when an occupant is destroyed inside the trigger, where no EndOverlap will arrive. */
	UFUNCTION()
	void HandleOccupantDestroyed(AActor* DestroyedActor);

	void OnOccupantEntered(AActor* Actor);
	void OnOccupantExited(AActor* Actor);
	void PruneStaleOccupants();
	int32 GetOccupantCount() const;

	// Obstruction
	bool QueryObstruction(float TestOpenness, AActor*& OutObstructor) const;
	void EnterObstructedState(AActor* Obstructor);

	// Registration
	void RegisterWithSubsystem();
	void UnregisterFromSubsystem();

	/** Logs once per key per component instance. */
	void LogOnce(FName Key, EArchOpeningIssueSeverity Severity, const FText& Message);

	void CapturePartRest(FArchOpeningPartRef& Part, const FTransform& Frame, bool bPromoteMobility);
};
