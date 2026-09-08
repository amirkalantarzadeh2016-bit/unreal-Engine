// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningComponent.h"

#include "ArchOpeningEasing.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningPreset.h"
#include "ArchOpeningSolver.h"
#include "ArchOpeningSubsystem.h"

#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "ArchOpenings"

UArchOpeningComponent::UArchOpeningComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	// The opening component itself is only a frame of reference; it is never rendered and never
	// collides. Everything visible belongs to the assigned meshes.
	bWantsInitializeComponent = false;
}

// -------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::OnRegister()
{
	Super::OnRegister();

	RefreshCurveValidity();
	RebuildLeafBoundsCache();
}

void UArchOpeningComponent::OnUnregister()
{
	StopMovementLoop();
	UnregisterFromSubsystem();
	DestroyProximityVolume();

	Super::OnUnregister();
}

void UArchOpeningComponent::PostLoad()
{
	Super::PostLoad();

	RefreshCurveValidity();
}

void UArchOpeningComponent::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);

#if WITH_EDITOR
	// Only plain editor duplication is interesting. World duplication for PIE preserves object
	// names wholesale, so the name comparison below would misfire there.
	if (DuplicateMode != EDuplicateMode::Normal)
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();
	int32 ClearedCount = 0;

	auto ClearStaleRefs = [this, OwnerActor, &ClearedCount](TArray<FArchOpeningPartRef>& Parts)
	{
		for (FArchOpeningPartRef& Part : Parts)
		{
			if (!::IsValid(Part.Component))
			{
				continue;
			}

			const AActor* PartOwner = Part.Component->GetOwner();
			if (PartOwner == nullptr || PartOwner == OwnerActor)
			{
				// Same actor: duplication already remapped it to the copy.
				continue;
			}

			// A reference the engine remapped to a duplicated part points at a NEW actor, which
			// always carries a new unique name. A reference that still matches the recorded name is
			// still pointing at the original opening's mesh, which this copy must not drive.
			if (PartOwner->GetFName() == Part.OwnerActorNameAtAssign)
			{
				Part.Component = nullptr;
				Part.bRestCaptured = false;
				++ClearedCount;
			}
		}

		Parts.RemoveAll([](const FArchOpeningPartRef& Part) { return Part.Component == nullptr; });
	};

	ClearStaleRefs(StationaryParts);
	ClearStaleRefs(LeafParts);
	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		ClearStaleRefs(Group.Parts);
	}

	Interaction.InteractionProxies.RemoveAll([OwnerActor](const TObjectPtr<USceneComponent>& Proxy)
	{
		return !::IsValid(Proxy) || (Proxy->GetOwner() != nullptr && Proxy->GetOwner() != OwnerActor);
	});

	if (ClearedCount > 0)
	{
		Calibration.bCalibrated = false;
		bLeafBoundsCached = false;

		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s' was duplicated on its own: %d part reference(s) still pointing at the original opening's meshes were cleared. Duplicate the opening together with its meshes to get a fully configured copy."),
			*GetPathName(), ClearedCount);
	}
#endif // WITH_EDITOR
}

void UArchOpeningComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshCurveValidity();
	RebuildLeafBoundsCache();

	// One validation pass at BeginPlay, logged once per issue key. Nothing logs from tick.
	const FArchOpeningValidationReport Report = Validate();
	for (const FArchOpeningIssue& Issue : Report.Issues)
	{
		if (Issue.Severity != EArchOpeningIssueSeverity::Info)
		{
			LogOnce(Issue.Key, Issue.Severity, Issue.Message);
		}
	}

	RegisterWithSubsystem();

	// Authored starting pose. Applied without animation, delays, sounds or transition events.
	Openness = FMath::Clamp(Timing.InitialOpenness, 0.0f, 1.0f);
	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		const bool bActuated =
			(Group.ReturnBehavior == EArchOpeningHandleReturn::RemainActuatedWhileOpen) && (Openness > 0.0f);
		Group.CurrentAlpha = bActuated ? 1.0f : 0.0f;
		Group.TargetAlpha = Group.CurrentAlpha;
	}

	State = (Openness <= 0.0f) ? EArchOpeningState::Closed : EArchOpeningState::Open;
	ApplyPose();

	if (Interaction.Mode == EArchOpeningInteractionMode::ProximityOnly ||
		Interaction.Mode == EArchOpeningInteractionMode::ClickAndProximity)
	{
		CreateProximityVolume();
	}

	// An opening that starts open with auto-close enabled must still close on its own.
	if (State == EArchOpeningState::Open && Timing.bAutoClose)
	{
		RequestDeferredClose(Timing.AutoCloseDelay, EArchOpeningCommandSource::AutoClose);
	}

	UpdateTickEnabled();
}

void UArchOpeningComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Audio first, and hard-stopped rather than faded: a looping component must never outlive the
	// opening, so EndPlay does not leave a fade running.
	if (UAudioComponent* Loop = MovementLoopComponent)
	{
		Loop->Stop();
		Loop->DestroyComponent();
	}
	MovementLoopComponent = nullptr;

	// Unbind every occupant destruction delegate we installed.
	for (const TPair<TWeakObjectPtr<AActor>, int32>& Pair : ProximityOccupancy)
	{
		if (AActor* Occupant = Pair.Key.Get())
		{
			Occupant->OnDestroyed.RemoveAll(this);
		}
	}
	ProximityOccupancy.Reset();

	DestroyProximityVolume();
	UnregisterFromSubsystem();
	CancelDeferredClose();

	SetComponentTickEnabled(false);

	Super::EndPlay(EndPlayReason);
}

// -------------------------------------------------------------------------------------------
// Calibration
// -------------------------------------------------------------------------------------------

FTransform UArchOpeningComponent::GetCalibrationFrame() const
{
	FArchOpeningSolver::FScaleAnalysis Analysis;
	return FArchOpeningSolver::MakeCalibrationFrame(GetComponentTransform(), Analysis);
}

bool UArchOpeningComponent::IsCalibrationFrameStale() const
{
	if (!Calibration.bCalibrated)
	{
		return false;
	}

	const FTransform Current = GetCalibrationFrame();
	return !Current.Equals(Calibration.CapturedFrame, 0.01f);
}

void UArchOpeningComponent::CapturePartRest(FArchOpeningPartRef& Part, const FTransform& Frame, bool bPromoteMobility)
{
	if (!::IsValid(Part.Component))
	{
		Part.bRestCaptured = false;
		return;
	}

	Part.RestRelative = FArchOpeningSolver::CaptureRestRelative(Part.Component->GetComponentTransform(), Frame);
	Part.bRestCaptured = true;

	if (const AActor* PartOwner = Part.Component->GetOwner())
	{
		Part.OwnerActorNameAtAssign = PartOwner->GetFName();
	}

	if (bPromoteMobility && Part.Component->Mobility != EComponentMobility::Movable)
	{
		if (!Part.bMobilityPromoted)
		{
			Part.OriginalMobility = Part.Component->Mobility;
			Part.bMobilityPromoted = true;
		}

		Part.Component->Modify();
		Part.Component->SetMobility(EComponentMobility::Movable);

		UE_LOG(LogArchOpenings, Log,
			TEXT("'%s': promoted '%s' to Movable so it can be animated. Movable geometry is no longer treated as baked static geometry by the lighting build; re-light the scene after setup."),
			*GetPathName(), *Part.Component->GetPathName());
	}
}

bool UArchOpeningComponent::SetCurrentPoseAsClosed()
{
	// A preview pose must never be silently promoted to the authored closed pose.
	if (bPreviewActive || (bPrePreviewCaptured && !FMath::IsNearlyZero(Openness)))
	{
		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s': refusing to capture a closed pose while an editor preview is showing openness %.3f. Stop the preview and restore the pre-preview pose first."),
			*GetPathName(), Openness);
		return false;
	}

	if (!FMath::IsNearlyZero(Openness))
	{
		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s': refusing to capture a closed pose at openness %.3f. Reset to the closed pose first, or set openness to 0."),
			*GetPathName(), Openness);
		return false;
	}

	Modify();

	FArchOpeningSolver::FScaleAnalysis Analysis;
	const FTransform Frame = FArchOpeningSolver::MakeCalibrationFrame(GetComponentTransform(), Analysis);

	Calibration.CapturedUniformScale = Analysis.bUniform && !Analysis.bNegative ? Analysis.UniformScale : 1.0f;
	Calibration.CapturedFrame = Frame;

	for (FArchOpeningPartRef& Part : StationaryParts)
	{
		// Stationary parts are recorded so the tool can reason about them and warn about conflicts,
		// but their mobility is never touched and they are never written to.
		CapturePartRest(Part, Frame, /*bPromoteMobility*/ false);
	}

	for (FArchOpeningPartRef& Part : LeafParts)
	{
		CapturePartRest(Part, Frame, bPromoteDrivenPartsToMovable);
	}

	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		for (FArchOpeningPartRef& Part : Group.Parts)
		{
			CapturePartRest(Part, Frame, bPromoteDrivenPartsToMovable);
		}
		Group.CurrentAlpha = 0.0f;
		Group.TargetAlpha = 0.0f;
	}

	Calibration.bCalibrated = LeafParts.ContainsByPredicate(
		[](const FArchOpeningPartRef& Part) { return Part.bRestCaptured; });

	Openness = 0.0f;
	TransitionProgress = 0.0f;
	State = EArchOpeningState::Closed;

	RebuildLeafBoundsCache();
	LoggedIssueKeys.Reset();

	return Calibration.bCalibrated;
}

void UArchOpeningComponent::ResetToClosedPose()
{
	Openness = 0.0f;
	TransitionProgress = 0.0f;
	TransitionStartOpenness = 0.0f;
	TransitionEndOpenness = 0.0f;
	State = EArchOpeningState::Closed;
	StateTimer = 0.0f;
	ObstructionReopenTarget = -1.0f;
	bResumeClosingAfterObstruction = false;
	CurrentObstructor = nullptr;
	CancelDeferredClose();

	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		Group.CurrentAlpha = 0.0f;
		Group.TargetAlpha = 0.0f;
	}

	ApplyPose();
	StopMovementLoop();
	UpdateTickEnabled();
}

// -------------------------------------------------------------------------------------------
// Pose application
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::ApplyPose()
{
	if (!Calibration.bCalibrated)
	{
		return;
	}

	const FTransform Frame = GetCalibrationFrame();

	const FTransform LeafDelta = FArchOpeningSolver::ComputeLeafDelta(
		MotionType, Hinged, Sliding, Calibration, Openness);

	ApplyDeltaToParts(LeafParts, LeafDelta, Frame);

	for (const FArchOpeningHandleGroup& Group : HandleGroups)
	{
		// Handle delta first, then the leaf delta: the handle inherits leaf motion while still
		// rotating about its own pivot.
		const FTransform HandleDelta = FArchOpeningSolver::ComputeHandleDelta(Group, Group.CurrentAlpha);
		ApplyDeltaToParts(Group.Parts, HandleDelta * LeafDelta, Frame);
	}
}

void UArchOpeningComponent::ApplyDeltaToParts(const TArray<FArchOpeningPartRef>& Parts, const FTransform& Delta, const FTransform& Frame)
{
	if (Parts.IsEmpty())
	{
		return;
	}

	// If one driven part happens to be attached under another, writing the parent first and the
	// child second leaves both at the pose we intend. Depth is recomputed per apply because
	// attachment can change outside our control; the arrays are small enough for that to be free.
	TArray<TPair<int32, int32>, TInlineAllocator<16>> Ordered;
	Ordered.Reserve(Parts.Num());

	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		const FArchOpeningPartRef& Part = Parts[Index];
		if (!::IsValid(Part.Component) || !Part.bRestCaptured)
		{
			continue;
		}

		int32 Depth = 0;
		for (const USceneComponent* Walk = Part.Component->GetAttachParent(); Walk != nullptr; Walk = Walk->GetAttachParent())
		{
			++Depth;
			if (Depth > 64)
			{
				break;	// Defensive: never spin on a malformed attachment chain.
			}
		}

		Ordered.Emplace(Depth, Index);
	}

	Ordered.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Key < B.Key; });

	for (const TPair<int32, int32>& Entry : Ordered)
	{
		const FArchOpeningPartRef& Part = Parts[Entry.Value];

		const FTransform Target = FArchOpeningSolver::ComposeWorld(Part.RestRelative, Delta, Frame);

		// Never sweep here: the leaf is deliberately kinematic, and a root sweep would not describe
		// the motion of the other meshes in the assembly anyway. Obstruction is handled by the
		// explicit multi-box query in TickObstruction().
		Part.Component->SetWorldTransform(Target, /*bSweep*/ false, /*OutHit*/ nullptr, ETeleportType::TeleportPhysics);
	}
}

void UArchOpeningComponent::ForEachPart(TFunctionRef<bool(const FArchOpeningPartRef&, EArchOpeningPartRole, int32)> Predicate) const
{
	for (const FArchOpeningPartRef& Part : StationaryParts)
	{
		if (!Predicate(Part, EArchOpeningPartRole::Stationary, INDEX_NONE))
		{
			return;
		}
	}

	for (const FArchOpeningPartRef& Part : LeafParts)
	{
		if (!Predicate(Part, EArchOpeningPartRole::Leaf, INDEX_NONE))
		{
			return;
		}
	}

	for (int32 GroupIndex = 0; GroupIndex < HandleGroups.Num(); ++GroupIndex)
	{
		for (const FArchOpeningPartRef& Part : HandleGroups[GroupIndex].Parts)
		{
			if (!Predicate(Part, EArchOpeningPartRole::Handle, GroupIndex))
			{
				return;
			}
		}
	}
}

FBox UArchOpeningComponent::GetLeafLocalBounds() const
{
	return CachedLeafLocalBounds;
}

void UArchOpeningComponent::RebuildLeafBoundsCache()
{
	FBox Bounds(ForceInit);
	bool bAny = false;

	for (const FArchOpeningPartRef& Part : LeafParts)
	{
		if (!::IsValid(Part.Component) || !Part.bRestCaptured)
		{
			continue;
		}

		const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part.Component);
		if (Primitive == nullptr)
		{
			continue;
		}

		// CalcBounds with the rest transform yields the part's bounds directly in calibration space.
		const FBoxSphereBounds PartBounds = Primitive->CalcBounds(Part.RestRelative);
		Bounds += PartBounds.GetBox();
		bAny = true;
	}

	CachedLeafLocalBounds = Bounds;
	bLeafBoundsCached = bAny;
}

void UArchOpeningComponent::RefreshCurveValidity()
{
	FText Reason;
	bOpeningCurveValid = (Timing.OpeningEasing != EArchOpeningEasing::Custom)
		|| FArchOpeningEasing::ValidateCurve(Timing.OpeningCurve, Reason);

	bClosingCurveValid = (Timing.ClosingEasing != EArchOpeningEasing::Custom)
		|| FArchOpeningEasing::ValidateCurve(Timing.ClosingCurve, Reason);
}

// -------------------------------------------------------------------------------------------
// Presets
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::ApplyPreset(const UArchOpeningPreset* Preset, bool bApplyHandleBehaviour)
{
	if (Preset == nullptr)
	{
		return;
	}

	Modify();

	if (Preset->bApplyMotionType)
	{
		MotionType = Preset->MotionType;
	}

	if (Preset->bApplyOpenAngleAndTravel)
	{
		Hinged.OpenAngle = Preset->OpenAngle;
		Sliding.TravelDistance = Preset->TravelDistance;
	}

	if (Preset->bApplyTiming)
	{
		Timing = Preset->Timing;
	}

	if (Preset->bApplyInteraction)
	{
		// Proxies are level-specific references and are deliberately preserved.
		TArray<TObjectPtr<USceneComponent>> KeptProxies = Interaction.InteractionProxies;
		Interaction = Preset->Interaction;
		Interaction.InteractionProxies = MoveTemp(KeptProxies);
	}

	if (Preset->bApplyProximity)
	{
		Proximity = Preset->Proximity;
	}

	if (Preset->bApplyObstruction)
	{
		Obstruction = Preset->Obstruction;
	}

	if (Preset->bApplyAudio)
	{
		Audio = Preset->Audio;
	}

	if (bApplyHandleBehaviour && Preset->bApplyHandleBehaviour)
	{
		// Behaviour only. Parts, pivots and axes stay exactly as the artist calibrated them.
		for (FArchOpeningHandleGroup& Group : HandleGroups)
		{
			Group.RotationAngle = Preset->HandleRotationAngle;
			Group.ActuationDuration = Preset->HandleActuationDuration;
			Group.ReturnDuration = Preset->HandleReturnDuration;
			Group.DelayBeforeLeafMovement = Preset->HandleDelayBeforeLeafMovement;
			Group.ReturnBehavior = Preset->HandleReturnBehavior;
		}
	}

	RefreshCurveValidity();
	LoggedIssueKeys.Reset();
}

// -------------------------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::LogOnce(FName Key, EArchOpeningIssueSeverity Severity, const FText& Message)
{
	if (LoggedIssueKeys.Contains(Key))
	{
		return;
	}
	LoggedIssueKeys.Add(Key);

	switch (Severity)
	{
	case EArchOpeningIssueSeverity::Error:
		UE_LOG(LogArchOpenings, Error, TEXT("'%s': %s"), *GetPathName(), *Message.ToString());
		break;
	case EArchOpeningIssueSeverity::Warning:
		UE_LOG(LogArchOpenings, Warning, TEXT("'%s': %s"), *GetPathName(), *Message.ToString());
		break;
	default:
		UE_LOG(LogArchOpenings, Log, TEXT("'%s': %s"), *GetPathName(), *Message.ToString());
		break;
	}
}

#undef LOCTEXT_NAMESPACE
