// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningComponent.h"

#include "ArchOpeningLog.h"
#include "ArchOpeningSolver.h"
#include "ArchOpeningSubsystem.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "ArchOpenings"

// -------------------------------------------------------------------------------------------
// Registry
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::RegisterWithSubsystem()
{
	if (const UWorld* World = GetWorld())
	{
		if (UArchOpeningSubsystem* Subsystem = World->GetSubsystem<UArchOpeningSubsystem>())
		{
			Subsystem->RegisterOpening(this);
		}
	}
}

void UArchOpeningComponent::UnregisterFromSubsystem()
{
	if (const UWorld* World = GetWorld())
	{
		if (UArchOpeningSubsystem* Subsystem = World->GetSubsystem<UArchOpeningSubsystem>())
		{
			Subsystem->UnregisterOpening(this);
		}
	}
}

// -------------------------------------------------------------------------------------------
// Click interaction
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::SetInteractionEnabled(bool bEnabled)
{
	Interaction.bInteractionEnabled = bEnabled;
}

bool UArchOpeningComponent::IsComponentClickable(const USceneComponent* Component) const
{
	if (Component == nullptr)
	{
		return false;
	}

	for (const TObjectPtr<USceneComponent>& Proxy : Interaction.InteractionProxies)
	{
		if (Proxy.Get() == Component)
		{
			return true;
		}
	}

	bool bClickable = false;

	ForEachPart([&](const FArchOpeningPartRef& Part, EArchOpeningPartRole Role, int32)
	{
		if (Part.Component.Get() != Component)
		{
			return true;
		}

		switch (Role)
		{
		case EArchOpeningPartRole::Leaf:		bClickable = Interaction.bLeafMeshesClickable; break;
		case EArchOpeningPartRole::Handle:		bClickable = Interaction.bHandleMeshesClickable; break;
		case EArchOpeningPartRole::Stationary:	bClickable = Interaction.bStationaryMeshesClickable; break;
		}

		return false;	// Found it; stop iterating.
	});

	return bClickable;
}

bool UArchOpeningComponent::HandleClickInteraction(AActor* Interactor)
{
	if (!Interaction.bInteractionEnabled)
	{
		return false;
	}

	if (Interaction.Mode != EArchOpeningInteractionMode::ClickOnly &&
		Interaction.Mode != EArchOpeningInteractionMode::ClickAndProximity)
	{
		return false;
	}

	OnInteractionAccepted.Broadcast(this, Interactor);
	Toggle(EArchOpeningCommandSource::Click);

	return true;
}

// -------------------------------------------------------------------------------------------
// Proximity
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::CreateProximityVolume()
{
	AActor* OwnerActor = GetOwner();
	if (OwnerActor == nullptr || ::IsValid(ProximityVolume))
	{
		return;
	}

	// Created at BeginPlay rather than kept as a persistent component: the trigger is pure runtime
	// machinery, so the level never carries an extra component the artist has to reason about, and
	// changing the extents in the editor cannot dirty the map through a component resize. The
	// editor draws the same box as a visualizer instead.
	ProximityVolume = NewObject<UBoxComponent>(OwnerActor, TEXT("ArchOpeningProximityVolume"));
	if (ProximityVolume == nullptr)
	{
		return;
	}

	ProximityVolume->SetupAttachment(this);
	ProximityVolume->SetBoxExtent(Proximity.BoxExtent.ComponentMax(FVector(1.0f)), /*bUpdateOverlaps*/ false);
	ProximityVolume->SetRelativeLocation(Proximity.BoxOffset);
	ProximityVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ProximityVolume->SetCollisionObjectType(ECC_WorldDynamic);
	ProximityVolume->SetCollisionResponseToAllChannels(ECR_Ignore);

	if (Proximity.OverlapObjectTypes.IsEmpty())
	{
		ProximityVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}
	else
	{
		for (const TEnumAsByte<EObjectTypeQuery>& ObjectType : Proximity.OverlapObjectTypes)
		{
			const ECollisionChannel Channel = UEngineTypes::ConvertToCollisionChannel(ObjectType);
			ProximityVolume->SetCollisionResponseToChannel(Channel, ECR_Overlap);
		}
	}

	ProximityVolume->SetGenerateOverlapEvents(true);
	ProximityVolume->SetHiddenInGame(true);
	ProximityVolume->RegisterComponent();

	// Seed occupancy from whatever is already inside BEFORE binding the delegates. Doing it in this
	// order is what stops an initial overlap from being counted twice, and it is also what makes an
	// opening that starts with a player already inside behave correctly: RefreshInitialOccupancy
	// runs the normal entry path, so such an opening opens exactly as if the player had walked in.
	ProximityVolume->UpdateOverlaps();
	RefreshInitialOccupancy();

	ProximityVolume->OnComponentBeginOverlap.AddDynamic(this, &UArchOpeningComponent::HandleProximityBeginOverlap);
	ProximityVolume->OnComponentEndOverlap.AddDynamic(this, &UArchOpeningComponent::HandleProximityEndOverlap);
}

void UArchOpeningComponent::DestroyProximityVolume()
{
	if (!::IsValid(ProximityVolume))
	{
		ProximityVolume = nullptr;
		return;
	}

	ProximityVolume->OnComponentBeginOverlap.RemoveAll(this);
	ProximityVolume->OnComponentEndOverlap.RemoveAll(this);
	ProximityVolume->DestroyComponent();
	ProximityVolume = nullptr;
}

bool UArchOpeningComponent::IsQualifyingOccupant(const AActor* Actor) const
{
	if (!::IsValid(Actor))
	{
		return false;
	}

	if (Proximity.AllowedOccupantClasses.IsEmpty())
	{
		if (!Actor->IsA<APawn>())
		{
			return false;
		}
	}
	else
	{
		bool bMatches = false;
		for (const TSubclassOf<AActor>& Class : Proximity.AllowedOccupantClasses)
		{
			if (Class != nullptr && Actor->IsA(Class))
			{
				bMatches = true;
				break;
			}
		}

		if (!bMatches)
		{
			return false;
		}
	}

	if (Proximity.bRequirePlayerControlledPawn)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		if (Pawn == nullptr || !Pawn->IsPlayerControlled())
		{
			return false;
		}
	}

	return true;
}

void UArchOpeningComponent::HandleProximityBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (IsQualifyingOccupant(OtherActor))
	{
		OnOccupantEntered(OtherActor);
	}
}

void UArchOpeningComponent::HandleProximityEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	// Deliberately not re-filtered through IsQualifyingOccupant: a pawn that was unpossessed while
	// inside would otherwise fail the filter on exit and leave a permanent phantom occupant.
	if (OtherActor != nullptr && ProximityOccupancy.Contains(OtherActor))
	{
		OnOccupantExited(OtherActor);
	}
}

void UArchOpeningComponent::HandleOccupantDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == nullptr)
	{
		return;
	}

	// Force the count to zero for this actor: a destroyed pawn cannot send the remaining EndOverlaps.
	if (int32* Count = ProximityOccupancy.Find(DestroyedActor))
	{
		*Count = 1;
		OnOccupantExited(DestroyedActor);
	}
}

void UArchOpeningComponent::OnOccupantEntered(AActor* Actor)
{
	const int32 PreviousTotal = GetOccupantCount();

	// Counted per overlapping component pair, so a pawn whose capsule and mesh both overlap the
	// trigger produces one enter and one exit as far as occupancy is concerned.
	int32& Count = ProximityOccupancy.FindOrAdd(Actor);
	if (Count == 0)
	{
		Actor->OnDestroyed.AddDynamic(this, &UArchOpeningComponent::HandleOccupantDestroyed);
	}
	++Count;

	if (PreviousTotal > 0)
	{
		return;	// Already occupied; nothing changes.
	}

	// Someone re-entered during the close delay: cancel the pending close.
	if (PendingCloseTimer >= 0.0f && PendingCloseSource == EArchOpeningCommandSource::Proximity)
	{
		CancelDeferredClose();
	}

	if (Interaction.bInteractionEnabled &&
		(Interaction.Mode == EArchOpeningInteractionMode::ProximityOnly ||
		 Interaction.Mode == EArchOpeningInteractionMode::ClickAndProximity))
	{
		Open(EArchOpeningCommandSource::Proximity);
	}

	UpdateTickEnabled();
}

void UArchOpeningComponent::OnOccupantExited(AActor* Actor)
{
	int32* Count = ProximityOccupancy.Find(Actor);
	if (Count == nullptr)
	{
		return;
	}

	--(*Count);
	if (*Count <= 0)
	{
		if (::IsValid(Actor))
		{
			Actor->OnDestroyed.RemoveAll(this);
		}
		ProximityOccupancy.Remove(Actor);
	}

	PruneStaleOccupants();

	if (GetOccupantCount() > 0)
	{
		return;
	}

	// The trigger is empty. A manual close made from inside is forgiven now, so the next entry may
	// open the opening again.
	bProximityReopenSuppressed = false;

	if (Proximity.bCloseOnExit && Interaction.bInteractionEnabled &&
		(Interaction.Mode == EArchOpeningInteractionMode::ProximityOnly ||
		 Interaction.Mode == EArchOpeningInteractionMode::ClickAndProximity))
	{
		RequestDeferredClose(Proximity.CloseDelay, EArchOpeningCommandSource::Proximity);
	}

	UpdateTickEnabled();
}

void UArchOpeningComponent::PruneStaleOccupants()
{
	for (auto It = ProximityOccupancy.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

int32 UArchOpeningComponent::GetOccupantCount() const
{
	int32 Total = 0;
	for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ProximityOccupancy)
	{
		if (Pair.Key.IsValid() && Pair.Value > 0)
		{
			++Total;
		}
	}
	return Total;
}

void UArchOpeningComponent::RefreshInitialOccupancy()
{
	if (!::IsValid(ProximityVolume))
	{
		return;
	}

	for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ProximityOccupancy)
	{
		if (AActor* Occupant = Pair.Key.Get())
		{
			Occupant->OnDestroyed.RemoveAll(this);
		}
	}
	ProximityOccupancy.Reset();

	TArray<UPrimitiveComponent*> Overlapping;
	ProximityVolume->GetOverlappingComponents(Overlapping);

	for (UPrimitiveComponent* Other : Overlapping)
	{
		AActor* OtherActor = Other ? Other->GetOwner() : nullptr;
		if (IsQualifyingOccupant(OtherActor))
		{
			OnOccupantEntered(OtherActor);
		}
	}
}

// -------------------------------------------------------------------------------------------
// Obstruction
// -------------------------------------------------------------------------------------------

bool UArchOpeningComponent::QueryObstruction(float TestOpenness, AActor*& OutObstructor) const
{
	OutObstructor = nullptr;

	const UWorld* World = GetWorld();
	if (World == nullptr || Obstruction.Policy == EArchOpeningObstructionPolicy::Ignore || !bLeafBoundsCached)
	{
		return false;
	}

	if (!CachedLeafLocalBounds.IsValid)
	{
		return false;
	}

	const FTransform Frame = GetCalibrationFrame();
	const FTransform Delta = FArchOpeningSolver::ComputeLeafDelta(
		MotionType, Hinged, Sliding, Calibration, FMath::Clamp(TestOpenness, 0.0f, 1.0f));

	const FTransform Combined = Delta * Frame;
	const FQuat WorldRotation = Combined.GetRotation();
	const float FrameScale = FMath::Max(FMath::Abs(Frame.GetScale3D().X), UE_KINDA_SMALL_NUMBER);

	// Slice the leaf volume along its longest local axis and test one oriented box per slice.
	//
	// This is deliberately a conservative discrete query, not continuous collision detection. A
	// rotating leaf sweeps an arc that no single box describes, and Unreal will not give continuous
	// detection for a rotation just because a move is flagged as swept, so the plugin does not
	// pretend otherwise: it tests the pose slightly ahead of the current one, in several pieces.
	// Limitations are listed in Docs/Limitations.md.
	const FVector LocalExtent = CachedLeafLocalBounds.GetExtent();
	const FVector LocalCentre = CachedLeafLocalBounds.GetCenter();

	int32 SliceAxis = 0;
	if (LocalExtent.Y > LocalExtent[SliceAxis]) { SliceAxis = 1; }
	if (LocalExtent.Z > LocalExtent[SliceAxis]) { SliceAxis = 2; }

	const int32 SliceCount = FMath::Clamp(Obstruction.SweepSliceCount, 1, 12);

	FCollisionQueryParams Params(FName(TEXT("ArchOpeningObstruction")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(GetOwner());

	ForEachPart([&Params](const FArchOpeningPartRef& Part, EArchOpeningPartRole, int32)
	{
		// Never treat the opening's own frame, leaf, glass or hardware as an obstruction.
		if (Part.IsValidPart())
		{
			if (const AActor* PartOwner = Part.Component->GetOwner())
			{
				Params.AddIgnoredActor(PartOwner);
			}
		}
		return true;
	});

	for (const TObjectPtr<USceneComponent>& Proxy : Interaction.InteractionProxies)
	{
		if (::IsValid(Proxy) && Proxy->GetOwner() != nullptr)
		{
			Params.AddIgnoredActor(Proxy->GetOwner());
		}
	}

	if (::IsValid(ProximityVolume))
	{
		// .Get() is required: a TObjectPtr converts equally well to both AddIgnoredComponent
		// overloads (raw pointer and TWeakObjectPtr), so the call is ambiguous without it.
		Params.AddIgnoredComponent(ProximityVolume.Get());
	}

	for (int32 SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex)
	{
		FVector SliceExtent = LocalExtent;
		SliceExtent[SliceAxis] = LocalExtent[SliceAxis] / static_cast<float>(SliceCount);

		FVector SliceCentre = LocalCentre;
		const float SliceSpan = 2.0f * LocalExtent[SliceAxis] / static_cast<float>(SliceCount);
		SliceCentre[SliceAxis] = LocalCentre[SliceAxis] - LocalExtent[SliceAxis] + SliceSpan * (SliceIndex + 0.5f);

		const FVector WorldCentre = Combined.TransformPosition(SliceCentre);

		// Shrink very slightly so a leaf resting flush against its own frame or the floor does not
		// register a permanent obstruction.
		const FVector WorldExtent = (SliceExtent * FrameScale) - FVector(0.5f);

		if (WorldExtent.GetMin() <= 0.0f)
		{
			continue;
		}

		TArray<FOverlapResult> Overlaps;
		const bool bAnyOverlap = World->OverlapMultiByChannel(
			Overlaps,
			WorldCentre,
			WorldRotation,
			Obstruction.ObstructionChannel,
			FCollisionShape::MakeBox(WorldExtent),
			Params);

		if (!bAnyOverlap)
		{
			continue;
		}

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* OverlapActor = Overlap.GetActor();
			if (!::IsValid(OverlapActor))
			{
				continue;
			}

			if (Obstruction.bOnlyPawnsObstruct && !OverlapActor->IsA<APawn>())
			{
				continue;
			}

			const UPrimitiveComponent* OverlapComponent = Overlap.GetComponent();
			if (OverlapComponent == nullptr || !OverlapComponent->IsCollisionEnabled())
			{
				continue;
			}

			OutObstructor = OverlapActor;
			return true;
		}
	}

	return false;
}

void UArchOpeningComponent::EnterObstructedState(AActor* Obstructor)
{
	CurrentObstructor = Obstructor;
	ObstructionClearTimer = 0.0f;

	// Remember what was interrupted so it can be resumed rather than reissued as a fresh command.
	bResumeClosingAfterObstruction = (State == EArchOpeningState::Closing);

	if (Obstruction.Policy == EArchOpeningObstructionPolicy::Reopen)
	{
		ObstructionReopenTarget = FMath::Clamp(Openness + FMath::Max(Obstruction.ReopenAmount, 0.0f), 0.0f, 1.0f);
	}
	else
	{
		ObstructionReopenTarget = -1.0f;
	}

	EnterState(EArchOpeningState::Obstructed);

	// Fired once per detection, on the transition into the state. It cannot repeat while the leaf
	// stays obstructed, which is what keeps obstruction from turning into an event or sound loop.
	OnObstructionDetected.Broadcast(this, Obstructor);
}

void UArchOpeningComponent::TickObstruction(float DeltaTime)
{
	if (Obstruction.Policy == EArchOpeningObstructionPolicy::Ignore)
	{
		return;
	}

	const bool bMoving = (State == EArchOpeningState::Closing) ||
		(State == EArchOpeningState::Opening && Obstruction.bCheckWhileOpening);

	if (bMoving)
	{
		ObstructionCheckAccumulator += DeltaTime;
		if (ObstructionCheckAccumulator < Obstruction.CheckInterval)
		{
			return;
		}
		ObstructionCheckAccumulator = 0.0f;

		// Test slightly ahead along the direction of travel so the leaf stops before contact.
		const float Direction = (State == EArchOpeningState::Closing) ? -1.0f : 1.0f;
		const float TestOpenness = Openness + Direction * FMath::Max(Obstruction.LookaheadOpenness, 0.0f);

		AActor* Obstructor = nullptr;
		if (QueryObstruction(TestOpenness, Obstructor))
		{
			EnterObstructedState(Obstructor);
		}

		return;
	}

	if (State != EArchOpeningState::Obstructed)
	{
		return;
	}

	// Back off, if the policy asks for it, before waiting.
	if (ObstructionReopenTarget >= 0.0f && Openness < ObstructionReopenTarget - KINDA_SMALL_NUMBER)
	{
		const float Duration = FMath::Max(ResolveTransitionDuration(/*bOpening*/ true), KINDA_SMALL_NUMBER);
		Openness = FMath::Min(Openness + DeltaTime / Duration, ObstructionReopenTarget);
		ObstructionClearTimer = 0.0f;
		return;
	}

	ObstructionCheckAccumulator += DeltaTime;
	if (ObstructionCheckAccumulator < FMath::Max(Obstruction.CheckInterval, 0.05f))
	{
		return;
	}
	ObstructionCheckAccumulator = 0.0f;

	AActor* Obstructor = nullptr;
	if (QueryObstruction(Openness, Obstructor))
	{
		CurrentObstructor = Obstructor;
		ObstructionClearTimer = 0.0f;
		return;
	}

	// Clear. Require a settled period before resuming so a pawn brushing past cannot make the leaf
	// stutter between moving and stopping.
	ObstructionClearTimer += FMath::Max(Obstruction.CheckInterval, 0.05f);
	if (ObstructionClearTimer < Obstruction.RetryDelay)
	{
		return;
	}

	CurrentObstructor = nullptr;
	ObstructionClearTimer = 0.0f;
	ObstructionReopenTarget = -1.0f;

	if (bResumeClosingAfterObstruction)
	{
		bResumeClosingAfterObstruction = false;
		BeginTransition(0.0f, /*bAllowHandleSequence*/ false, LastCommandSource);
	}
	else
	{
		BeginTransition(1.0f, /*bAllowHandleSequence*/ false, LastCommandSource);
	}
}

// -------------------------------------------------------------------------------------------
// Audio
// -------------------------------------------------------------------------------------------

USceneComponent* UArchOpeningComponent::GetAudioAnchor() const
{
	if (Audio.bAttachToLeaf)
	{
		for (const FArchOpeningPartRef& Part : LeafParts)
		{
			if (Part.IsValidPart())
			{
				return Part.Component;
			}
		}
	}

	return const_cast<UArchOpeningComponent*>(this);
}

bool UArchOpeningComponent::AreSoundsAllowed() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// Editor preview is silent unless the artist explicitly opts in, so scrubbing a door in the
	// viewport does not fill the editor with latch clicks.
	if (!World->IsGameWorld() || bPreviewActive)
	{
		return Audio.bPlaySoundsInEditorPreview;
	}

	return true;
}

void UArchOpeningComponent::PlayOneShot(USoundBase* Sound)
{
	if (Sound == nullptr || !AreSoundsAllowed())
	{
		return;
	}

	UGameplayStatics::SpawnSoundAttached(
		Sound,
		GetAudioAnchor(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		/*bStopWhenAttachedToDestroyed*/ true,
		Audio.VolumeMultiplier,
		Audio.PitchMultiplier,
		/*StartTime*/ 0.0f,
		Audio.Attenuation,
		/*ConcurrencySettings*/ nullptr,
		/*bAutoDestroy*/ true);
}

void UArchOpeningComponent::StartMovementLoop()
{
	if (Audio.MovementLoopSound == nullptr || !AreSoundsAllowed())
	{
		return;
	}

	if (::IsValid(MovementLoopComponent) && MovementLoopComponent->IsPlaying())
	{
		return;	// Already looping; a repeated command must not stack a second loop.
	}

	MovementLoopComponent = UGameplayStatics::SpawnSoundAttached(
		Audio.MovementLoopSound,
		GetAudioAnchor(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		/*bStopWhenAttachedToDestroyed*/ true,
		Audio.VolumeMultiplier,
		Audio.PitchMultiplier,
		/*StartTime*/ 0.0f,
		Audio.Attenuation,
		/*ConcurrencySettings*/ nullptr,
		/*bAutoDestroy*/ false);
}

void UArchOpeningComponent::StopMovementLoop()
{
	if (!::IsValid(MovementLoopComponent))
	{
		MovementLoopComponent = nullptr;
		return;
	}

	// The component was spawned with bAutoDestroy off so it could be faded. Handing ownership back
	// to the audio system BEFORE the fade starts is what makes it clean itself up when the fade
	// finishes, instead of lingering.
	MovementLoopComponent->bAutoDestroy = true;

	if (Audio.LoopFadeOutTime > 0.0f)
	{
		MovementLoopComponent->FadeOut(Audio.LoopFadeOutTime, 0.0f);
	}
	else
	{
		MovementLoopComponent->Stop();
	}

	MovementLoopComponent = nullptr;
}

#undef LOCTEXT_NAMESPACE
