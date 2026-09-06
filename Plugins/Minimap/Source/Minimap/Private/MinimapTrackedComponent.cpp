#include "MinimapTrackedComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MinimapModule.h"
#include "MinimapSubsystem.h"

UMinimapTrackedComponent::UMinimapTrackedComponent()
{
	// Off by default: the subsystem drives every marker in one batched pass.
	// Registering N tick functions for N markers is pure overhead in the tick manager.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Markers are a purely local presentation concern. Replicating them would be wasted
	// bandwidth - each client projects from the actor transforms it already receives.
	SetIsReplicatedByDefault(false);
}

void UMinimapTrackedComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoRegister)
	{
		RegisterWithMinimap();
	}

	if (bAllowIndividualTick)
	{
		SetComponentTickEnabled(true);
	}
}

void UMinimapTrackedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister before Super so the subsystem never sees a half-destroyed component.
	UnregisterFromMinimap();
	Super::EndPlay(EndPlayReason);
}

void UMinimapTrackedComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Individual ticking only ever forces an earlier recompute; the subsystem still owns
	// the actual projection so there is exactly one code path producing snapshots.
	if (bAllowIndividualTick)
	{
		MarkDirty();
	}
}

UMinimapSubsystem* UMinimapTrackedComponent::GetMinimapSubsystem() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	return World->GetSubsystem<UMinimapSubsystem>();
}

void UMinimapTrackedComponent::RegisterWithMinimap()
{
	if (bRegistered)
	{
		return;
	}

	UMinimapSubsystem* Subsystem = GetMinimapSubsystem();
	if (!Subsystem)
	{
		// Common and benign during CDO construction or in worlds without the subsystem
		// (e.g. certain preview worlds). Log at Verbose so shipping logs stay clean.
		UE_LOG(LogMinimap, Verbose,
			TEXT("MinimapTrackedComponent on '%s' could not find a UMinimapSubsystem; marker not registered."),
			*GetNameSafe(GetOwner()));
		return;
	}

	Subsystem->RegisterMarker(this);
	bRegistered = true;

	// Guarantee the first batched pass computes a snapshot even if the owner never moves.
	bForceUpdate = true;
}

void UMinimapTrackedComponent::UnregisterFromMinimap()
{
	if (!bRegistered)
	{
		return;
	}

	if (UMinimapSubsystem* Subsystem = GetMinimapSubsystem())
	{
		Subsystem->UnregisterMarker(this);
	}

	bRegistered = false;
	bHasSnapshot = false;
}

void UMinimapTrackedComponent::SetMarkerVisible(bool bNewVisible)
{
	if (bMarkerVisible == bNewVisible)
	{
		return;
	}

	bMarkerVisible = bNewVisible;

	// The visibility delegate is broadcast from ApplySnapshot so that every state change
	// travels through exactly one path and stays consistent with the snapshot contents.
	bForceUpdate = true;
}

bool UMinimapTrackedComponent::IsMarkerEnabled() const
{
	// Deliberately does NOT test bMarkerVisible: a marker that was just hidden still needs
	// one final pass so OnVisibilityChanged can fire with bVisible == false.
	return IsValid(this) && IsValid(GetOwner()) && bRegistered;
}

bool UMinimapTrackedComponent::CheckAndConsumeDirty(const FVector& WorldLocation, float WorldYaw)
{
	if (bForceUpdate)
	{
		bForceUpdate = false;
		LastTrackedLocation = WorldLocation;
		LastTrackedYaw = WorldYaw;
		return true;
	}

	// Squared distance keeps the hot path free of a square root.
	const float MoveToleranceSq = MoveTolerance * MoveTolerance;
	const bool bMoved = FVector::DistSquared(WorldLocation, LastTrackedLocation) > MoveToleranceSq;

	// Only yaw matters, and only when the icon actually uses it.
	const bool bRotated = bUseActorYaw
		&& FMath::Abs(FRotator::NormalizeAxis(WorldYaw - LastTrackedYaw)) > AngleTolerance;

	if (!bMoved && !bRotated)
	{
		return false;
	}

	LastTrackedLocation = WorldLocation;
	LastTrackedYaw = WorldYaw;
	return true;
}

void UMinimapTrackedComponent::ApplySnapshot(const FMinimapMarkerSnapshot& NewSnapshot)
{
	const bool bHadSnapshot   = bHasSnapshot;
	const bool bWasOutOfBounds = bHadSnapshot && LastSnapshot.bOutOfBounds;
	const bool bWasVisible     = bHadSnapshot ? LastSnapshot.bVisible : false;

	LastSnapshot = NewSnapshot;
	bHasSnapshot = true;

	// Visibility first: a listener that hides its widget should not then receive a
	// position update for a frame in which the marker is not drawn.
	if (!bHadSnapshot || bWasVisible != NewSnapshot.bVisible)
	{
		OnVisibilityChanged.Broadcast(this, NewSnapshot.bVisible);
	}

	if (!bHadSnapshot || bWasOutOfBounds != NewSnapshot.bOutOfBounds)
	{
		OnOutOfBoundsChanged.Broadcast(this, NewSnapshot.bOutOfBounds, NewSnapshot.EdgeAngle);
	}

	// Position is only interesting while the marker is actually shown.
	if (NewSnapshot.bVisible)
	{
		OnMinimapPositionUpdated.Broadcast(this, LastSnapshot);
	}
}
