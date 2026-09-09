// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourCameraRig.h"

#include "ArchVizTourLog.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "CineCameraComponent.h"
#include "GameFramework/PlayerController.h"

ATourCameraRig::ATourCameraRig()
{
	// The subsystem calls ApplyState; the rig has nothing of its own to do per frame.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	CineCamera = CreateDefaultSubobject<UCineCameraComponent>(TEXT("CineCamera"));
	check(CineCamera != nullptr);
	SetRootComponent(CineCamera);

	// AActor::CalcCamera finds this component automatically once the rig is the view target,
	// so no CalcCamera override or dedicated camera actor is needed.
	bFindCameraComponentWhenViewTarget = true;
}

void ATourCameraRig::ApplyState(const FTourCameraState& State, float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_ArchVizTour_RigApplyState);

	if (!State.bValid)
	{
		// A failed evaluation must not teleport the camera to the origin mid-shot; holding the
		// previous pose degrades gracefully while the warning explains why.
		return;
	}

	if (!IsValid(CineCamera))
	{
		return;
	}

	FQuat TargetRotation = State.Rotation.Quaternion();

	const bool bCanSmooth = bSmoothRotation
		&& bHasSmoothingHistory
		&& DeltaSeconds > UE_KINDA_SMALL_NUMBER
		&& RotationStiffness > 0.0f;

	if (bCanSmooth)
	{
		TargetRotation = FMath::QInterpTo(
			LastAppliedState.Rotation.Quaternion(), TargetRotation, DeltaSeconds, RotationStiffness);
	}

	SetActorLocationAndRotation(State.Location, TargetRotation, /*bSweep*/ false, /*OutSweepHitResult*/ nullptr, ETeleportType::TeleportPhysics);

	float TargetFocalLength = State.FocalLength;
	if (FocalLengthStiffness > 0.0f && bHasSmoothingHistory && DeltaSeconds > UE_KINDA_SMALL_NUMBER)
	{
		TargetFocalLength = FMath::FInterpTo(
			LastAppliedState.FocalLength, TargetFocalLength, DeltaSeconds, FocalLengthStiffness);
	}

	CineCamera->SetCurrentFocalLength(TargetFocalLength);
	CineCamera->CurrentAperture = State.Aperture;

	if (State.FocusDistance > 0.0f)
	{
		CineCamera->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
		CineCamera->FocusSettings.ManualFocusDistance = State.FocusDistance;
	}

	LastAppliedState = State;
	LastAppliedState.Rotation    = TargetRotation.Rotator();
	LastAppliedState.FocalLength = TargetFocalLength;
	bHasSmoothingHistory = true;
}

void ATourCameraRig::ResetSmoothing()
{
	bHasSmoothingHistory = false;
}

void ATourCameraRig::StartCameraShake(APlayerController* Controller)
{
	if (CameraShakeClass == nullptr || CameraShakeScale <= 0.0f)
	{
		return;
	}

	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		UE_LOG(LogArchVizTour, Verbose, TEXT("Tour camera rig '%s': no camera manager to play a shake on yet."), *GetName());
		return;
	}

	if (IsValid(ActiveShake))
	{
		// Already running: restarting would double the amplitude for a frame.
		return;
	}

	ActiveShake = Controller->PlayerCameraManager->StartCameraShake(CameraShakeClass, CameraShakeScale);
}

void ATourCameraRig::StopCameraShake(APlayerController* Controller, bool bImmediately)
{
	if (!IsValid(ActiveShake))
	{
		ActiveShake = nullptr;
		return;
	}

	if (Controller != nullptr && Controller->PlayerCameraManager != nullptr)
	{
		Controller->PlayerCameraManager->StopCameraShake(ActiveShake, bImmediately);
	}

	ActiveShake = nullptr;
}
