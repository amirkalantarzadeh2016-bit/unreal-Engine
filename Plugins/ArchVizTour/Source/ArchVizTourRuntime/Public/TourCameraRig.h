// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TourTypes.h"

#include "TourCameraRig.generated.h"

class UCameraShakeBase;
class UCineCameraComponent;

/**
 * The camera the tour actually drives during a SplineMove step.
 *
 * Transform-driven directly, with no spring arm: a spring arm's lag is a second, hidden
 * smoothing filter on top of this one, and the two fight each other on slow moves. Damping
 * here is a single explicit QInterpTo whose stiffness the author controls.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = "ArchViz Tour", meta = (DisplayName = "Tour Camera Rig"))
class ARCHVIZTOURRUNTIME_API ATourCameraRig : public AActor
{
	GENERATED_BODY()

public:
	ATourCameraRig();

	/** The camera the player sees through while this rig is the view target. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tour Camera Rig")
	TObjectPtr<UCineCameraComponent> CineCamera;

	// ---------------------------------------------------------------------
	// Smoothing
	// ---------------------------------------------------------------------

	/**
	 * Damp orientation instead of snapping to it.
	 *
	 * A LookAt target passing near the camera swings the aim through a large angle in a single
	 * frame; damping turns that pop into a lead-in. Disabled means the rig reproduces the
	 * authored path exactly, which is what an offline render wants.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Camera Rig|Smoothing")
	bool bSmoothRotation = false;

	/**
	 * QInterpTo stiffness, in units of 1/second.
	 * Higher converges faster; below about 2 the camera visibly trails the path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Camera Rig|Smoothing", meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "30.0", EditCondition = "bSmoothRotation"))
	float RotationStiffness = 8.0f;

	/**
	 * Damp focal length changes as well, in units of 1/second.
	 * Values <= 0 apply focal length immediately.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Camera Rig|Smoothing", meta = (ClampMin = "0.0", UIMax = "30.0"))
	float FocalLengthStiffness = 0.0f;

	// ---------------------------------------------------------------------
	// Shake
	// ---------------------------------------------------------------------

	/** Optional handheld shake played for as long as the rig is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Camera Rig|Shake")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	/** Shake amplitude multiplier. 0 disables the shake without clearing the class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tour Camera Rig|Shake", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float CameraShakeScale = 1.0f;

	// ---------------------------------------------------------------------
	// API
	// ---------------------------------------------------------------------

	/**
	 * Drive the rig to a sampled camera state.
	 *
	 * @param State          Target pose and lens. An invalid state is ignored and the rig holds.
	 * @param DeltaSeconds   Seconds since the last call; drives the smoothing filters. Pass 0 to
	 *                       apply the state exactly, which is what scrubbing and the first frame
	 *                       of a step want.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Camera Rig")
	void ApplyState(const FTourCameraState& State, float DeltaSeconds);

	/**
	 * Forget the smoothing history so the next ApplyState lands exactly on its target.
	 * Call across a cut; without it the rig eases out of the previous step's pose.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Camera Rig")
	void ResetSmoothing();

	/** Start the configured shake on the given controller's camera manager. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Camera Rig")
	void StartCameraShake(APlayerController* Controller);

	/** Stop a shake started by StartCameraShake. Safe to call when nothing is playing. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Camera Rig")
	void StopCameraShake(APlayerController* Controller, bool bImmediately = false);

	/** The last state applied, for UI readouts and for resuming after a pause. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Camera Rig")
	const FTourCameraState& GetLastAppliedState() const { return LastAppliedState; }

private:
	/** Last state passed to ApplyState. bValid is false until the first successful call. */
	UPROPERTY(Transient)
	FTourCameraState LastAppliedState;

	/** Shake instance started by StartCameraShake, so the same one can be stopped later. */
	UPROPERTY(Transient)
	TObjectPtr<UCameraShakeBase> ActiveShake;

	/** False until the first ApplyState, so smoothing cannot ease out of an uninitialised pose. */
	bool bHasSmoothingHistory = false;
};
