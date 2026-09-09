// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningComponent.h"

#include "ArchOpeningEasing.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningSolver.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR

#define LOCTEXT_NAMESPACE "ArchOpenings"

namespace ArchOpeningEditorLocal
{
	/** Removes a component from a part array. Returns true if it was present. */
	bool RemoveFrom(TArray<FArchOpeningPartRef>& Parts, const USceneComponent* Component)
	{
		const int32 Removed = Parts.RemoveAll(
			[Component](const FArchOpeningPartRef& Part) { return Part.Component == Component; });
		return Removed > 0;
	}
}

// -------------------------------------------------------------------------------------------
// Assignment
// -------------------------------------------------------------------------------------------

bool UArchOpeningComponent::AssignPart(USceneComponent* Component, EArchOpeningPartRole Role, int32 HandleGroupIndex, FText& OutError)
{
	if (!::IsValid(Component))
	{
		OutError = LOCTEXT("AssignNull", "The selected object is not a valid scene component.");
		return false;
	}

	if (Component == static_cast<USceneComponent*>(this))
	{
		OutError = LOCTEXT("AssignSelf", "The opening component cannot drive itself.");
		return false;
	}

	// Driving an ancestor of the opening would move the very frame the rest poses are measured
	// against, which is a feedback loop rather than a hierarchy.
	if (IsAttachedTo(Component))
	{
		OutError = LOCTEXT("AssignAncestor", "That component is an ancestor of the opening component. Driving it would move the opening's own calibration frame.");
		return false;
	}

	if (Component->GetWorld() != GetWorld())
	{
		OutError = LOCTEXT("AssignWorld", "That component belongs to a different world than the opening.");
		return false;
	}

	const AActor* PartOwner = Component->GetOwner();
	if (PartOwner == nullptr)
	{
		OutError = LOCTEXT("AssignNoOwner", "That component has no owning actor.");
		return false;
	}

	if (GetOwner() != nullptr && PartOwner->GetLevel() != GetOwner()->GetLevel())
	{
		OutError = LOCTEXT("AssignLevel", "That component belongs to a different level than the opening. Cross-level references would not survive streaming; move the opening into the same level.");
		return false;
	}

	if (Role == EArchOpeningPartRole::Handle && !HandleGroups.IsValidIndex(HandleGroupIndex))
	{
		OutError = LOCTEXT("AssignNoGroup", "No handle group is selected. Add a handle group first.");
		return false;
	}

	Modify();

	// A component belongs to exactly one role, so assigning always clears the previous one. This is
	// what makes it impossible to have a mesh receive the leaf motion twice by also being a handle.
	UnassignPart(Component);

	FArchOpeningPartRef NewPart;
	NewPart.Component = Component;
	NewPart.OwnerActorNameAtAssign = PartOwner->GetFName();

	// Capture the rest immediately so a part assigned after calibration is usable without a full
	// recalibration pass, and so preview cannot leave it at an unknown rest.
	FArchOpeningSolver::FScaleAnalysis Analysis;
	const FTransform Frame = Calibration.bCalibrated
		? Calibration.CapturedFrame
		: FArchOpeningSolver::MakeCalibrationFrame(GetComponentTransform(), Analysis);

	switch (Role)
	{
	case EArchOpeningPartRole::Stationary:
		CapturePartRest(NewPart, Frame, /*bPromoteMobility*/ false);
		StationaryParts.Add(NewPart);
		break;

	case EArchOpeningPartRole::Leaf:
		CapturePartRest(NewPart, Frame, bPromoteDrivenPartsToMovable);
		LeafParts.Add(NewPart);
		break;

	case EArchOpeningPartRole::Handle:
		CapturePartRest(NewPart, Frame, bPromoteDrivenPartsToMovable);
		HandleGroups[HandleGroupIndex].Parts.Add(NewPart);
		break;
	}

	if (!Calibration.bCalibrated && Role == EArchOpeningPartRole::Leaf)
	{
		Calibration.CapturedFrame = Frame;
		Calibration.CapturedUniformScale = Analysis.bUniform && !Analysis.bNegative ? Analysis.UniformScale : 1.0f;
		Calibration.bCalibrated = true;
	}

	RebuildLeafBoundsCache();
	LoggedIssueKeys.Reset();

	return true;
}

bool UArchOpeningComponent::UnassignPart(USceneComponent* Component)
{
	using namespace ArchOpeningEditorLocal;

	if (Component == nullptr)
	{
		return false;
	}

	Modify();

	bool bChanged = RemoveFrom(StationaryParts, Component);
	bChanged |= RemoveFrom(LeafParts, Component);

	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		bChanged |= RemoveFrom(Group.Parts, Component);
	}

	if (bChanged)
	{
		RebuildLeafBoundsCache();
		LoggedIssueKeys.Reset();
	}

	return bChanged;
}

void UArchOpeningComponent::ClearRole(EArchOpeningPartRole Role, int32 HandleGroupIndex)
{
	Modify();

	switch (Role)
	{
	case EArchOpeningPartRole::Stationary:
		StationaryParts.Reset();
		break;

	case EArchOpeningPartRole::Leaf:
		LeafParts.Reset();
		Calibration.bCalibrated = false;
		break;

	case EArchOpeningPartRole::Handle:
		if (HandleGroups.IsValidIndex(HandleGroupIndex))
		{
			HandleGroups[HandleGroupIndex].Parts.Reset();
		}
		else
		{
			for (FArchOpeningHandleGroup& Group : HandleGroups)
			{
				Group.Parts.Reset();
			}
		}
		break;
	}

	RebuildLeafBoundsCache();
	LoggedIssueKeys.Reset();
}

// -------------------------------------------------------------------------------------------
// Placement helpers
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::SnapHingeToLeafEdge()
{
	RebuildLeafBoundsCache();

	if (!bLeafBoundsCached || !CachedLeafLocalBounds.IsValid)
	{
		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s': cannot snap the hinge, the leaf has no calibrated bounds yet."), *GetPathName());
		return;
	}

	Modify();

	const FVector Centre = CachedLeafLocalBounds.GetCenter();
	const FVector Extent = CachedLeafLocalBounds.GetExtent();

	const FVector Up = Calibration.UpDirection.GetSafeNormal();
	const FVector Left = FArchOpeningSolver::ComputeLeftDirection(Calibration.OutsideDirection, Calibration.UpDirection);

	if (Up.IsNearlyZero() || Left.IsNearlyZero())
	{
		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s': cannot snap the hinge, the outside and up directions are degenerate."), *GetPathName());
		return;
	}

	// Project the bounds onto the axis the hinge sits on and take the appropriate face.
	auto ExtentAlong = [&Extent](const FVector& Axis)
	{
		return FMath::Abs(Axis.X) * Extent.X + FMath::Abs(Axis.Y) * Extent.Y + FMath::Abs(Axis.Z) * Extent.Z;
	};

	switch (Hinged.AxisPreset)
	{
	case EArchOpeningHingeAxisPreset::VerticalSide:
	{
		const FVector SideDirection = (Hinged.HingeSide == EArchOpeningHingeSide::Left) ? Left : -Left;
		Hinged.HingeLocation = Centre + SideDirection * ExtentAlong(Left);
		break;
	}

	case EArchOpeningHingeAxisPreset::HorizontalTop:
		Hinged.HingeLocation = Centre + Up * ExtentAlong(Up);
		break;

	case EArchOpeningHingeAxisPreset::HorizontalBottom:
		Hinged.HingeLocation = Centre - Up * ExtentAlong(Up);
		break;

	default:
		// Custom axis: put the hinge at the leaf centre and let the artist move it.
		Hinged.HingeLocation = Centre;
		break;
	}
}

void UArchOpeningComponent::SnapSlideToLeafBounds()
{
	RebuildLeafBoundsCache();

	if (!bLeafBoundsCached || !CachedLeafLocalBounds.IsValid)
	{
		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s': cannot derive a slide path, the leaf has no calibrated bounds yet."), *GetPathName());
		return;
	}

	const FVector Left = FArchOpeningSolver::ComputeLeftDirection(Calibration.OutsideDirection, Calibration.UpDirection);
	if (Left.IsNearlyZero())
	{
		return;
	}

	Modify();

	const FVector Extent = CachedLeafLocalBounds.GetExtent();
	const float WidthAlongLeft =
		2.0f * (FMath::Abs(Left.X) * Extent.X + FMath::Abs(Left.Y) * Extent.Y + FMath::Abs(Left.Z) * Extent.Z);

	Sliding.SlideDirection = Left;
	Sliding.TravelDistance = WidthAlongLeft;
}

void UArchOpeningComponent::SnapHandlePivotToGroupBounds(int32 GroupIndex)
{
	if (!HandleGroups.IsValidIndex(GroupIndex))
	{
		return;
	}

	FArchOpeningHandleGroup& Group = HandleGroups[GroupIndex];

	// Not named "Bounds": that would shadow USceneComponent::Bounds, which the engine's build
	// settings treat as an error rather than a warning.
	FBox GroupBounds(ForceInit);
	bool bAny = false;

	for (const FArchOpeningPartRef& Part : Group.Parts)
	{
		const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part.Component.Get());
		if (Primitive == nullptr || !Part.bRestCaptured)
		{
			continue;
		}

		GroupBounds += Primitive->CalcBounds(Part.RestRelative).GetBox();
		bAny = true;
	}

	if (!bAny)
	{
		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s': handle group '%s' has no calibrated meshes to derive a pivot from."),
			*GetPathName(), *Group.GroupName.ToString());
		return;
	}

	Modify();
	Group.PivotLocation = GroupBounds.GetCenter();

	// A lever rotates about the axis normal to the door face, which is the outside direction.
	const FVector Outside = Calibration.OutsideDirection.GetSafeNormal();
	if (!Outside.IsNearlyZero())
	{
		Group.RotationAxis = Outside;
	}
}

// -------------------------------------------------------------------------------------------
// Moving the opening after calibration
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::TransportPartsWithFrameChange()
{
	if (!Calibration.bCalibrated)
	{
		return;
	}

	FArchOpeningSolver::FScaleAnalysis Analysis;
	const FTransform NewFrame = FArchOpeningSolver::MakeCalibrationFrame(GetComponentTransform(), Analysis);

	if (NewFrame.Equals(Calibration.CapturedFrame, 0.001f))
	{
		return;
	}

	Modify();

	// Rigidly carry every assigned part, stationary included, so the configured assembly keeps its
	// world relationship to the opening. Stationary parts are written here and only here: this is a
	// deliberate, transacted editor operation, not a per-frame drive, so their mobility is untouched.
	const FTransform LeafDelta = FArchOpeningSolver::ComputeLeafDelta(
		MotionType, Hinged, Sliding, Calibration, Openness);

	auto Transport = [&NewFrame](const TArray<FArchOpeningPartRef>& Parts, const FTransform& Delta)
	{
		for (const FArchOpeningPartRef& Part : Parts)
		{
			if (!::IsValid(Part.Component) || !Part.bRestCaptured)
			{
				continue;
			}

			Part.Component->Modify();
			if (AActor* PartOwner = Part.Component->GetOwner())
			{
				PartOwner->Modify();
			}

			Part.Component->SetWorldTransform(
				FArchOpeningSolver::ComposeWorld(Part.RestRelative, Delta, NewFrame));
		}
	};

	Transport(StationaryParts, FTransform::Identity);
	Transport(LeafParts, LeafDelta);

	for (const FArchOpeningHandleGroup& Group : HandleGroups)
	{
		const FTransform HandleDelta = FArchOpeningSolver::ComputeHandleDelta(Group, Group.CurrentAlpha);
		Transport(Group.Parts, HandleDelta * LeafDelta);
	}

	Calibration.CapturedFrame = NewFrame;
	Calibration.CapturedUniformScale = Analysis.bUniform && !Analysis.bNegative ? Analysis.UniformScale : 1.0f;
}

void UArchOpeningComponent::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);

	if (bFinished && bMovePartsWithOpening && Calibration.bCalibrated)
	{
		TransportPartsWithFrameChange();
	}
}

// -------------------------------------------------------------------------------------------
// Preview
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::PreviewOpen()
{
	if (!bPrePreviewCaptured)
	{
		PrePreviewOpenness = Openness;
		bPrePreviewCaptured = true;
	}

	bPreviewActive = true;
	BeginTransition(1.0f, /*bAllowHandleSequence*/ true, EArchOpeningCommandSource::Preview);
}

void UArchOpeningComponent::PreviewClose()
{
	if (!bPrePreviewCaptured)
	{
		PrePreviewOpenness = Openness;
		bPrePreviewCaptured = true;
	}

	bPreviewActive = true;
	BeginTransition(0.0f, /*bAllowHandleSequence*/ true, EArchOpeningCommandSource::Preview);
}

void UArchOpeningComponent::PreviewToggle()
{
	const bool bHeadingOpen =
		State == EArchOpeningState::Open ||
		State == EArchOpeningState::Opening ||
		(State == EArchOpeningState::HandlePreparation && bTransitionOpening);

	if (bHeadingOpen)
	{
		PreviewClose();
	}
	else
	{
		PreviewOpen();
	}
}

void UArchOpeningComponent::StopPreview()
{
	bPreviewActive = false;
	StateTimer = 0.0f;
	CancelDeferredClose();
	StopMovementLoop();

	State = (Openness <= KINDA_SMALL_NUMBER) ? EArchOpeningState::Closed : EArchOpeningState::Open;

	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		Group.TargetAlpha = Group.CurrentAlpha;
	}
}

void UArchOpeningComponent::SetPreviewOpenness(float NewOpenness)
{
	if (!bPrePreviewCaptured)
	{
		PrePreviewOpenness = Openness;
		bPrePreviewCaptured = true;
	}

	const float Target = FMath::Clamp(NewOpenness, 0.0f, 1.0f);

	if (bPreviewActive && (State == EArchOpeningState::Opening || State == EArchOpeningState::Closing))
	{
		// Scrubbing during a running preview: keep the same transition but move its progress to the
		// point on the easing curve that produces the scrubbed pose, so releasing the scrub carries
		// on smoothly instead of jumping back.
		const bool bOpening = (State == EArchOpeningState::Opening);
		const float Span = TransitionEndOpenness - TransitionStartOpenness;

		if (!FMath::IsNearlyZero(Span))
		{
			const float NormalisedPose = FMath::Clamp((Target - TransitionStartOpenness) / Span, 0.0f, 1.0f);
			TransitionProgress = FArchOpeningEasing::InverseEvaluate(
				ResolveEasing(bOpening), ResolveCurve(bOpening), ResolveCurveValidity(bOpening), NormalisedPose);
		}

		Openness = Target;
		ApplyPose();
		return;
	}

	// Plain scrub with no transition running: jump to the pose without touching the calibration.
	bPreviewActive = false;
	Openness = Target;
	TransitionProgress = Target;
	TransitionStartOpenness = Target;
	TransitionEndOpenness = Target;
	State = (Openness <= KINDA_SMALL_NUMBER) ? EArchOpeningState::Closed : EArchOpeningState::Open;

	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		const bool bActuated =
			(Group.ReturnBehavior == EArchOpeningHandleReturn::RemainActuatedWhileOpen) && (Openness > 0.0f);
		Group.CurrentAlpha = bActuated ? 1.0f : 0.0f;
		Group.TargetAlpha = Group.CurrentAlpha;
	}

	ApplyPose();
}

void UArchOpeningComponent::RestorePrePreviewPose()
{
	bPreviewActive = false;
	StopMovementLoop();
	CancelDeferredClose();

	const float Restored = bPrePreviewCaptured ? PrePreviewOpenness : 0.0f;
	bPrePreviewCaptured = false;
	PrePreviewOpenness = 0.0f;

	Openness = FMath::Clamp(Restored, 0.0f, 1.0f);
	TransitionProgress = Openness;
	TransitionStartOpenness = Openness;
	TransitionEndOpenness = Openness;
	StateTimer = 0.0f;

	for (FArchOpeningHandleGroup& Group : HandleGroups)
	{
		const bool bActuated =
			(Group.ReturnBehavior == EArchOpeningHandleReturn::RemainActuatedWhileOpen) && (Openness > 0.0f);
		Group.CurrentAlpha = bActuated ? 1.0f : 0.0f;
		Group.TargetAlpha = Group.CurrentAlpha;
	}

	State = (Openness <= KINDA_SMALL_NUMBER) ? EArchOpeningState::Closed : EArchOpeningState::Open;

	// Restoring drives the parts back onto poses recomputed from the stored rest, so the meshes end
	// up exactly where they were before the preview, not merely close to it.
	ApplyPose();
}

void UArchOpeningComponent::TickPreview(float DeltaSeconds)
{
	if (!bPreviewActive || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// Deliberately not the gameplay tick: the editor module drives this from the core ticker, so
	// preview works with no PIE session and no world tick running.
	TickDelays(DeltaSeconds);
	TickTransition(DeltaSeconds);
	TickHandles(DeltaSeconds);

	ApplyPose();

	if (State != EArchOpeningState::Opening && State != EArchOpeningState::Closing &&
		State != EArchOpeningState::HandlePreparation && State != EArchOpeningState::OpeningDelay &&
		State != EArchOpeningState::ClosingDelay && !AnyHandleStillAnimating())
	{
		bPreviewActive = false;
	}
}

// -------------------------------------------------------------------------------------------
// Property edits
// -------------------------------------------------------------------------------------------

void UArchOpeningComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	RefreshCurveValidity();
	RebuildLeafBoundsCache();
	LoggedIssueKeys.Reset();

	// Reflect motion setting edits in the viewport immediately, but only when the leaf is already
	// away from closed - editing settings must never move a closed opening.
	if (Openness > KINDA_SMALL_NUMBER)
	{
		ApplyPose();
	}
}

void UArchOpeningComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	RefreshCurveValidity();
	RebuildLeafBoundsCache();
	LoggedIssueKeys.Reset();

	if (Openness > KINDA_SMALL_NUMBER)
	{
		ApplyPose();
	}
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR
