#include "MinimapViewComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MinimapFunctionLibrary.h"
#include "MinimapModule.h"
#include "MinimapSubsystem.h"

UMinimapViewComponent::UMinimapViewComponent()
{
	// The subsystem owns the only *recurring* tick. This component's tick exists solely to
	// ease the compass and zoom, and switches itself off the moment both have settled, so
	// at rest the system still does no per-frame work.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void UMinimapViewComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoRegister)
	{
		RegisterView();
	}

	// Start settled rather than easing in from zero on the first frame.
	TargetZoomMultiplier = ZoomMultiplier;
	SmoothedCompassAngle = GetCompassAngle();
	bCompassInitialized = true;
}

void UMinimapViewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterView();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void UMinimapViewComponent::RegisterView()
{
	if (bRegistered)
	{
		return;
	}

	const UWorld* World = GetWorld();
	UMinimapSubsystem* Subsystem = World ? World->GetSubsystem<UMinimapSubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogMinimap, Verbose,
			TEXT("MinimapViewComponent on '%s' could not find a UMinimapSubsystem; view not registered."),
			*GetNameSafe(GetOwner()));
		return;
	}

	Subsystem->RegisterView(this);
	bRegistered = true;
}

void UMinimapViewComponent::UnregisterView()
{
	if (!bRegistered)
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		if (UMinimapSubsystem* Subsystem = World->GetSubsystem<UMinimapSubsystem>())
		{
			Subsystem->UnregisterView(this);
		}
	}

	bRegistered = false;
	Snapshots.Reset();
}

void UMinimapViewComponent::SetViewRenderingEnabled(bool bEnabled)
{
	if (bRenderingEnabled == bEnabled)
	{
		return;
	}

	bRenderingEnabled = bEnabled;

	// Coming back on screen must not show stale positions, so force a full rebuild.
	if (bEnabled)
	{
		bStateInitialized = false;
	}
}

bool UMinimapViewComponent::IsViewActive() const
{
	return bRegistered && bRenderingEnabled && IsValid(this) && IsValid(GetOwner());
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void UMinimapViewComponent::SetOrientationMode(EMinimapOrientationMode NewMode)
{
	if (OrientationMode != NewMode)
	{
		OrientationMode = NewMode;
		bStateInitialized = false; // ViewYaw semantics changed - rebuild the context.
	}
}

void UMinimapViewComponent::SetZoomMultiplier(float NewZoom)
{
	// Immediate set. SetZoomTarget is the eased entry point.
	const float Clamped = FMath::Clamp(NewZoom, MinZoomMultiplier, MaxZoomMultiplier);
	if (!FMath::IsNearlyEqual(ZoomMultiplier, Clamped))
	{
		ZoomMultiplier = Clamped;
		bStateInitialized = false; // Force the projection context to rebuild.
	}
	TargetZoomMultiplier = Clamped;
}

void UMinimapViewComponent::SetExplicitViewActor(AActor* NewViewActor)
{
	if (ExplicitViewActor.Get() == NewViewActor)
	{
		return;
	}

	ExplicitViewActor = NewViewActor;
	bStateInitialized = false;
}

// ---------------------------------------------------------------------------
// Derived outputs
// ---------------------------------------------------------------------------

float UMinimapViewComponent::GetMapRotationTurns() const
{
	return UMinimapFunctionLibrary::GetMapRotationTurns(ViewYaw, MapYawOffset, bNegateMapRotation);
}

float UMinimapViewComponent::GetCompassAngle() const
{
	return UMinimapFunctionLibrary::GetCompassAngle(ViewYaw);
}

FVector2D UMinimapViewComponent::GetMaterialPlayerParams() const
{
	return UMinimapFunctionLibrary::NormalizedToMaterialParams(ViewerNormalizedOnFixedMap);
}

// ---------------------------------------------------------------------------
// View-actor resolution (respawn / repossession safety)
// ---------------------------------------------------------------------------

AActor* UMinimapViewComponent::ResolveViewActorFromCandidates(
	AActor* ExplicitOverride,
	APawn* ControlledPawn,
	AActor* ViewTarget,
	APawn* OwningPawn)
{
	// Priority order, first valid wins. IsValid() rather than a raw null check so that
	// an actor already marked PendingKill by a respawn is skipped, not followed.
	if (IsValid(ExplicitOverride))
	{
		return ExplicitOverride;
	}
	if (IsValid(ControlledPawn))
	{
		return ControlledPawn;
	}
	// Covers spectating and the death camera, where the controller has no pawn.
	if (IsValid(ViewTarget))
	{
		return ViewTarget;
	}
	if (IsValid(OwningPawn))
	{
		return OwningPawn;
	}
	return nullptr;
}

APlayerController* UMinimapViewComponent::ResolveOwningPlayerController() const
{
	AActor* Owner = GetOwner();

	if (APlayerController* OwnerAsPC = Cast<APlayerController>(Owner))
	{
		return OwnerAsPC;
	}

	if (const APawn* OwnerAsPawn = Cast<APawn>(Owner))
	{
		if (APlayerController* PawnPC = Cast<APlayerController>(OwnerAsPawn->GetController()))
		{
			return PawnPC;
		}
	}

	// Multiplayer-safe fallback: on a listen server GetPlayerController(0) can return a
	// remote client's controller, so filter explicitly on IsLocalController().
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = Cast<APlayerController>(It->Get()))
			{
				if (PC->IsLocalController())
				{
					return PC;
				}
			}
		}
	}

	return nullptr;
}

AActor* UMinimapViewComponent::ResolveViewActor()
{
	APlayerController* PC = ResolveOwningPlayerController();

	APawn* ControlledPawn = PC ? PC->GetPawn() : nullptr;
	AActor* ViewTarget    = PC ? PC->GetViewTarget() : nullptr;
	APawn* OwningPawn     = Cast<APawn>(GetOwner());

	return ResolveViewActorFromCandidates(ExplicitViewActor.Get(), ControlledPawn, ViewTarget, OwningPawn);
}

float UMinimapViewComponent::ResolveViewYaw(AActor* ViewActor) const
{
	// Control rotation is what the player is looking along, which is what the map should
	// align to. It is also valid for a frame or two after a pawn dies, avoiding a snap.
	if (const APlayerController* PC = ResolveOwningPlayerController())
	{
		return static_cast<float>(PC->GetControlRotation().Yaw);
	}

	if (const APawn* Pawn = Cast<APawn>(ViewActor))
	{
		if (const AController* Controller = Pawn->GetController())
		{
			return static_cast<float>(Controller->GetControlRotation().Yaw);
		}
	}

	return IsValid(ViewActor) ? static_cast<float>(ViewActor->GetActorRotation().Yaw) : 0.0f;
}

// ---------------------------------------------------------------------------
// Per-update state refresh
// ---------------------------------------------------------------------------

bool UMinimapViewComponent::RefreshViewState(const FMinimapCalibration& Calibration, bool bCalibrationChanged)
{
	// Re-resolve every update. Caching a pawn permanently is exactly the bug the legacy
	// Blueprint had: PlayerRef was captured once at Construct and went stale on respawn.
	AActor* NewViewActor = ResolveViewActor();

	const bool bViewActorChanged = (CachedViewActor.Get() != NewViewActor);
	if (bViewActorChanged)
	{
		CachedViewActor = NewViewActor;
		OnViewActorChanged.Broadcast(this, NewViewActor);
	}

	// --- Anchor -----------------------------------------------------------
	FVector2D NewAnchor;
	float NewAnchorZ;

	if (AnchorMode == EMinimapAnchorMode::ViewerCentered && IsValid(NewViewActor))
	{
		const FVector Location = NewViewActor->GetActorLocation();
		NewAnchor  = FVector2D(Location.X, Location.Y);
		NewAnchorZ = static_cast<float>(Location.Z);
	}
	else
	{
		// Fixed map, or viewer-centred with no resolvable viewer yet (pre-possession).
		NewAnchor  = Calibration.WorldCenter;
		NewAnchorZ = (Calibration.MinZ + Calibration.MaxZ) * 0.5f;
	}

	// --- View yaw ---------------------------------------------------------
	const float NewViewYaw = (OrientationMode == EMinimapOrientationMode::RotatingMap)
		? ResolveViewYaw(NewViewActor)
		: 0.0f;

	// --- Dirty test -------------------------------------------------------
	const float AnchorToleranceSq = AnchorMoveTolerance * AnchorMoveTolerance;
	const bool bAnchorMoved = FVector2D::DistSquared(NewAnchor, Anchor) > AnchorToleranceSq;
	const bool bYawChanged  = FMath::Abs(FRotator::NormalizeAxis(NewViewYaw - ViewYaw)) > ViewAngleTolerance;

	const bool bDirty = !bStateInitialized
		|| bCalibrationChanged
		|| bViewActorChanged
		|| bAnchorMoved
		|| bYawChanged;

	if (!bDirty)
	{
		return false;
	}

	Anchor  = NewAnchor;
	AnchorZ = NewAnchorZ;
	ViewYaw = NewViewYaw;

	// Fold the per-view zoom into a local copy so the shared calibration is never mutated
	// by one view - that would corrupt every other view sharing the registry.
	FMinimapCalibration EffectiveCalibration = Calibration;
	EffectiveCalibration.Zoom = FMath::Max(Calibration.Zoom * ZoomMultiplier, 0.01f);

	ProjectionContext.Build(EffectiveCalibration, Anchor, AnchorZ, ViewYaw);

	// The material's PlayerX/PlayerY describe where the viewer is on the WHOLE map, so this
	// second projection deliberately uses the fixed centre and no view rotation - the
	// material applies the rotation itself via MapRotation.
	if (IsValid(NewViewActor))
	{
		FMinimapProjectionContext FixedContext;
		FixedContext.Build(EffectiveCalibration, Calibration.WorldCenter,
			(Calibration.MinZ + Calibration.MaxZ) * 0.5f, 0.0f);
		ViewerNormalizedOnFixedMap = FixedContext.Project(NewViewActor->GetActorLocation());
	}
	else
	{
		ViewerNormalizedOnFixedMap = FVector2D::ZeroVector;
	}

	bStateInitialized = true;

	OnViewTransformChanged.Broadcast(this, Anchor, ViewYaw);

	// The view moved, so the compass has a new target to chase.
	UpdateSmoothingTickState();

	return true;
}

void UMinimapViewComponent::BroadcastViewUpdated()
{
	OnMinimapViewUpdated.Broadcast(this, Snapshots);
}


// ---------------------------------------------------------------------------
// Compass smoothing and zoom easing
// ---------------------------------------------------------------------------

void UMinimapViewComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bStillEasing = false;

	// --- Compass ----------------------------------------------------------
	if (bSmoothCompass)
	{
		const float TargetAngle = GetCompassAngle();

		if (!bCompassInitialized)
		{
			SmoothedCompassAngle = TargetAngle;
			bCompassInitialized = true;
		}
		else
		{
			// Interpolate the shortest signed DELTA, not the raw angles: lerping 179 -> -179
			// directly would spin the indicator all the way round instead of 2 degrees.
			const float Delta = FRotator::NormalizeAxis(TargetAngle - SmoothedCompassAngle);

			if (FMath::Abs(Delta) > CompassSettleTolerance)
			{
				const float Alpha = FMath::Clamp(DeltaTime * CompassInterpSpeed, 0.0f, 1.0f);
				SmoothedCompassAngle = FRotator::NormalizeAxis(SmoothedCompassAngle + Delta * Alpha);
				bStillEasing = true;
			}
			else
			{
				SmoothedCompassAngle = TargetAngle;
			}
		}
	}

	// --- Zoom -------------------------------------------------------------
	if (bSmoothZoom && !FMath::IsNearlyEqual(ZoomMultiplier, TargetZoomMultiplier, 0.001f))
	{
		const float NewZoom = FMath::FInterpTo(ZoomMultiplier, TargetZoomMultiplier, DeltaTime, ZoomInterpSpeed);

		ZoomMultiplier = FMath::Clamp(NewZoom, MinZoomMultiplier, MaxZoomMultiplier);

		// The projection context caches 1/extent, so it must rebuild for the new zoom.
		bStateInitialized = false;
		bStillEasing = true;
	}

	if (!bStillEasing)
	{
		SetComponentTickEnabled(false);
	}
}

void UMinimapViewComponent::UpdateSmoothingTickState()
{
	// Only ever turns the tick ON; TickComponent turns it off once everything settles.
	if (!IsValid(this) || !GetOwner())
	{
		return;
	}

	const bool bZoomPending = bSmoothZoom && !FMath::IsNearlyEqual(ZoomMultiplier, TargetZoomMultiplier, 0.001f);
	const bool bCompassPending = bSmoothCompass;

	if (bZoomPending || bCompassPending)
	{
		SetComponentTickEnabled(true);
	}
}

float UMinimapViewComponent::GetCardinalScreenAngle(int32 CardinalIndex) const
{
	// 0 = N, 1 = E, 2 = S, 3 = W. Each cardinal sits 90 degrees further round the ring,
	// and the whole ring carries the smoothed compass angle.
	const float BaseAngle = 90.0f * static_cast<float>(((CardinalIndex % 4) + 4) % 4);
	return FRotator::NormalizeAxis(BaseAngle + GetSmoothedCompassAngle());
}

FVector2D UMinimapViewComponent::GetCardinalRingOffset(int32 CardinalIndex, float RingRadius) const
{
	const float AngleDegrees = GetCardinalScreenAngle(CardinalIndex);
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);

	// 0 degrees = up. Screen Y grows downward, hence the negated cosine.
	return FVector2D(
		RingRadius * FMath::Sin(AngleRadians),
		-RingRadius * FMath::Cos(AngleRadians));
}

void UMinimapViewComponent::SetZoomTarget(float NewZoom)
{
	const float Clamped = FMath::Clamp(NewZoom, MinZoomMultiplier, MaxZoomMultiplier);
	if (FMath::IsNearlyEqual(TargetZoomMultiplier, Clamped, 0.0001f))
	{
		return;
	}

	TargetZoomMultiplier = Clamped;

	if (!bSmoothZoom)
	{
		SetZoomMultiplier(Clamped);
		return;
	}

	UpdateSmoothingTickState();
}

void UMinimapViewComponent::ZoomIn()
{
	SetZoomTarget(TargetZoomMultiplier * FMath::Max(ZoomStep, 1.01f));
}

void UMinimapViewComponent::ZoomOut()
{
	SetZoomTarget(TargetZoomMultiplier / FMath::Max(ZoomStep, 1.01f));
}

float UMinimapViewComponent::GetZoomAlpha() const
{
	const float Range = MaxZoomMultiplier - MinZoomMultiplier;
	if (Range <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp((TargetZoomMultiplier - MinZoomMultiplier) / Range, 0.0f, 1.0f);
}

void UMinimapViewComponent::SetZoomAlpha(float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
	SetZoomTarget(FMath::Lerp(MinZoomMultiplier, MaxZoomMultiplier, Clamped));
}
