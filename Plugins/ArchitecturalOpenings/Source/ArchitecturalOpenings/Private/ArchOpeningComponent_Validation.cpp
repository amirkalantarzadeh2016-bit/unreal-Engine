// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningComponent.h"

#include "ArchOpeningEasing.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningSolver.h"

#include "Components/PrimitiveComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "ArchOpenings"

FArchOpeningValidationReport UArchOpeningComponent::Validate() const
{
	FArchOpeningValidationReport Report;

	// -- Assigned parts -----------------------------------------------------------------------

	const int32 ValidLeafCount = LeafParts.FilterByPredicate(
		[](const FArchOpeningPartRef& Part) { return Part.IsValidPart(); }).Num();

	if (LeafParts.IsEmpty())
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("NoLeafParts"),
			LOCTEXT("NoLeafParts", "No movable leaf parts are assigned. The opening cannot animate anything."));
	}
	else if (ValidLeafCount == 0)
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("LeafPartsDeleted"),
			LOCTEXT("LeafPartsDeleted", "Every assigned leaf part has been deleted from the level."));
	}
	else if (ValidLeafCount < LeafParts.Num())
	{
		Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("SomeLeafPartsDeleted"),
			LOCTEXT("SomeLeafPartsDeleted", "Some assigned leaf parts have been deleted from the level and are being skipped."));
	}

	// Duplicate / conflicting assignment across roles.
	TSet<const USceneComponent*> Seen;
	bool bDuplicateFound = false;
	bool bMissingRestFound = false;
	bool bBadMobilityFound = false;
	bool bCyclicFound = false;
	bool bWrongWorldFound = false;

	ForEachPart([&](const FArchOpeningPartRef& Part, EArchOpeningPartRole Role, int32 /*GroupIndex*/)
	{
		if (!Part.IsValidPart())
		{
			return true;
		}

		if (Seen.Contains(Part.Component))
		{
			bDuplicateFound = true;
		}
		Seen.Add(Part.Component);

		if (!Part.bRestCaptured && Role != EArchOpeningPartRole::Stationary)
		{
			bMissingRestFound = true;
		}

		if (Role != EArchOpeningPartRole::Stationary && Part.Component->Mobility != EComponentMobility::Movable)
		{
			bBadMobilityFound = true;
		}

		if (Part.Component.Get() == this || IsAttachedTo(Part.Component.Get()))
		{
			bCyclicFound = true;
		}

		if (Part.Component->GetWorld() != GetWorld())
		{
			bWrongWorldFound = true;
		}

		return true;
	});

	if (bDuplicateFound)
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("DuplicateAssignment"),
			LOCTEXT("DuplicateAssignment", "The same mesh is assigned to more than one role. A mesh listed both as a leaf part and as a handle part would receive the leaf motion twice."));
	}

	if (bMissingRestFound)
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("MissingRest"),
			LOCTEXT("MissingRest", "Some driven parts have no captured closed pose. Run 'Set Current Pose As Closed'."));
	}

	if (bBadMobilityFound)
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("BadMobility"),
			LOCTEXT("BadMobility", "Some driven parts are not Movable and cannot be animated at runtime. Enable 'Promote Driven Parts To Movable' and recalibrate, or set their mobility by hand."));
	}

	if (bCyclicFound)
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("CyclicAssignment"),
			LOCTEXT("CyclicAssignment", "A part is the opening component itself or one of its ancestors. Driving it would move the calibration frame it is measured against."));
	}

	if (bWrongWorldFound)
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("WrongWorld"),
			LOCTEXT("WrongWorld", "A part belongs to a different world than the opening. Cross-world assignment is not supported."));
	}

	// Openings competing for the same mesh, which is what a lone duplicate of a configured opening
	// produces if the automatic clean-up was bypassed.
	if (const UWorld* World = GetWorld())
	{
		bool bContested = false;

		for (TObjectIterator<UArchOpeningComponent> It; It; ++It)
		{
			UArchOpeningComponent* Other = *It;
			if (Other == this || !::IsValid(Other) || Other->GetWorld() != World)
			{
				continue;
			}

			Other->ForEachPart([&](const FArchOpeningPartRef& OtherPart, EArchOpeningPartRole OtherRole, int32)
			{
				if (OtherRole != EArchOpeningPartRole::Stationary && OtherPart.IsValidPart() && Seen.Contains(OtherPart.Component))
				{
					bContested = true;
					return false;
				}
				return true;
			});

			if (bContested)
			{
				break;
			}
		}

		if (bContested)
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("ContestedParts"),
				LOCTEXT("ContestedParts", "One or more assigned meshes are also driven by another opening in this level. Two openings writing the same transform will fight every frame."));
		}
	}

	// -- Calibration --------------------------------------------------------------------------

	if (!Calibration.bCalibrated)
	{
		Report.Add(EArchOpeningIssueSeverity::Error, TEXT("NotCalibrated"),
			LOCTEXT("NotCalibrated", "The opening has no captured closed pose. Run 'Set Current Pose As Closed'."));
	}
	else if (IsCalibrationFrameStale())
	{
		Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("StaleFrame"),
			LOCTEXT("StaleFrame", "The opening has been moved since its closed pose was captured, so the calibration no longer matches the meshes. Use 'Re-anchor Parts To Opening', or recapture the closed pose."));
	}

	{
		FArchOpeningSolver::FScaleAnalysis Analysis = FArchOpeningSolver::AnalyzeScale(GetComponentScale());

		if (!Analysis.bUniform)
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("NonUniformScale"),
				LOCTEXT("NonUniformScale", "The opening component has non-uniform scale. Composing a rotation with a non-uniform parent scale would shear the leaf, so the scale is ignored: motion is authored in unscaled centimetres. Scale the source meshes instead, or set the opening's scale back to uniform."));
		}
		else if (Analysis.bNegative)
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("NegativeScale"),
				LOCTEXT("NegativeScale", "The opening component has mirrored (negative) scale, which makes the resolved swing direction ambiguous. The mirror is ignored for motion; check the swing direction and use 'Invert Direction' if it opens the wrong way."));
		}

		if (Analysis.RawScale.IsNearlyZero())
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("ZeroScale"),
				LOCTEXT("ZeroScale", "The opening component has a zero scale component. Motion cannot be resolved."));
		}
	}

	{
		const FVector Outside = Calibration.OutsideDirection.GetSafeNormal();
		const FVector Up = Calibration.UpDirection.GetSafeNormal();

		if (Outside.IsNearlyZero() || Up.IsNearlyZero())
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("BadReferenceFrame"),
				LOCTEXT("BadReferenceFrame", "The outside direction and the up direction must both be non-zero."));
		}
		else if (FMath::Abs(FVector::DotProduct(Outside, Up)) > 0.999f)
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("ParallelReferenceFrame"),
				LOCTEXT("ParallelReferenceFrame", "The outside direction and the up direction are parallel, so left and right cannot be derived. Choose an up direction perpendicular to the outside face."));
		}
	}

	// -- Motion -------------------------------------------------------------------------------

	if (MotionType == EArchOpeningMotionType::Hinged)
	{
		if (FArchOpeningSolver::ComputeHingeAxis(Hinged, Calibration).IsNearlyZero())
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("InvalidHingeAxis"),
				LOCTEXT("InvalidHingeAxis", "The hinge axis resolves to zero. Check the custom axis, or the outside and up directions."));
		}

		if (FMath::IsNearlyZero(Hinged.OpenAngle))
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("ZeroOpenAngle"),
				LOCTEXT("ZeroOpenAngle", "The opening angle is zero, so the leaf will not visibly move."));
		}
	}
	else
	{
		if (FArchOpeningSolver::ComputeSlideDirection(Sliding, Calibration).IsNearlyZero())
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("InvalidSlideDirection"),
				LOCTEXT("InvalidSlideDirection", "The slide direction resolves to zero. Enter a non-zero local direction."));
		}

		if (FMath::IsNearlyZero(Sliding.TravelDistance))
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("ZeroTravel"),
				LOCTEXT("ZeroTravel", "The travel distance is zero, so the panel will not visibly move."));
		}
	}

	// -- Timing -------------------------------------------------------------------------------

	if (Timing.TimingMode == EArchOpeningTimingMode::Duration)
	{
		if (Timing.OpenDuration <= 0.0f || Timing.CloseDuration <= 0.0f)
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("ZeroDuration"),
				LOCTEXT("ZeroDuration", "A duration of zero makes the transition instantaneous. It is handled safely but the leaf will teleport."));
		}
	}
	else
	{
		if (Timing.OpenSpeed <= 0.0f || Timing.CloseSpeed <= 0.0f)
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("ZeroSpeed"),
				LOCTEXT("ZeroSpeed", "A speed of zero makes the transition instantaneous. It is handled safely but the leaf will teleport."));
		}
	}

	{
		FText Reason;
		if (Timing.OpeningEasing == EArchOpeningEasing::Custom && !FArchOpeningEasing::ValidateCurve(Timing.OpeningCurve, Reason))
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("BadOpeningCurve"),
				FText::Format(LOCTEXT("BadOpeningCurveFmt", "Opening curve rejected, falling back to Smooth Step: {0}"), Reason));
		}

		if (Timing.ClosingEasing == EArchOpeningEasing::Custom && !FArchOpeningEasing::ValidateCurve(Timing.ClosingCurve, Reason))
		{
			Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("BadClosingCurve"),
				FText::Format(LOCTEXT("BadClosingCurveFmt", "Closing curve rejected, falling back to Smooth Step: {0}"), Reason));
		}
	}

	// -- Handles ------------------------------------------------------------------------------

	for (int32 GroupIndex = 0; GroupIndex < HandleGroups.Num(); ++GroupIndex)
	{
		const FArchOpeningHandleGroup& Group = HandleGroups[GroupIndex];

		const bool bHasParts = Group.Parts.ContainsByPredicate(
			[](const FArchOpeningPartRef& Part) { return Part.IsValidPart(); });

		if (!bHasParts)
		{
			Report.Add(EArchOpeningIssueSeverity::Warning,
				FName(*FString::Printf(TEXT("HandleGroupEmpty_%d"), GroupIndex)),
				FText::Format(LOCTEXT("HandleGroupEmptyFmt", "Handle group '{0}' has no valid meshes assigned."),
					FText::FromName(Group.GroupName)));
			continue;
		}

		if (Group.RotationAxis.GetSafeNormal().IsNearlyZero())
		{
			Report.Add(EArchOpeningIssueSeverity::Error,
				FName(*FString::Printf(TEXT("HandleGroupAxis_%d"), GroupIndex)),
				FText::Format(LOCTEXT("HandleGroupAxisFmt", "Handle group '{0}' has a zero rotation axis, so it has no valid pivot to rotate about."),
					FText::FromName(Group.GroupName)));
		}

		if (FMath::IsNearlyZero(Group.RotationAngle))
		{
			Report.Add(EArchOpeningIssueSeverity::Info,
				FName(*FString::Printf(TEXT("HandleGroupAngle_%d"), GroupIndex)),
				FText::Format(LOCTEXT("HandleGroupAngleFmt", "Handle group '{0}' has a zero rotation angle and will not visibly actuate."),
					FText::FromName(Group.GroupName)));
		}
	}

	// -- Interaction --------------------------------------------------------------------------

	if (Interaction.Mode == EArchOpeningInteractionMode::ClickOnly ||
		Interaction.Mode == EArchOpeningInteractionMode::ClickAndProximity)
	{
		if (!Interaction.bLeafMeshesClickable && !Interaction.bHandleMeshesClickable &&
			!Interaction.bStationaryMeshesClickable && Interaction.InteractionProxies.IsEmpty())
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("NothingClickable"),
				LOCTEXT("NothingClickable", "Click interaction is enabled but no part role is clickable and no interaction proxy is assigned. Nothing can be clicked."));
		}

		bool bBlockingFound = false;
		bool bAnyClickableChecked = false;

		auto CheckClickable = [&](const FArchOpeningPartRef& Part, EArchOpeningPartRole Role, int32)
		{
			const bool bRoleClickable =
				(Role == EArchOpeningPartRole::Leaf && Interaction.bLeafMeshesClickable) ||
				(Role == EArchOpeningPartRole::Handle && Interaction.bHandleMeshesClickable) ||
				(Role == EArchOpeningPartRole::Stationary && Interaction.bStationaryMeshesClickable);

			if (!bRoleClickable || !Part.IsValidPart())
			{
				return true;
			}

			if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part.Component))
			{
				bAnyClickableChecked = true;
				if (Primitive->IsCollisionEnabled() &&
					Primitive->GetCollisionResponseToChannel(Interaction.InteractionTraceChannel) == ECR_Block)
				{
					bBlockingFound = true;
					return false;
				}
			}

			return true;
		};

		ForEachPart(CheckClickable);

		for (const TObjectPtr<USceneComponent>& Proxy : Interaction.InteractionProxies)
		{
			if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Proxy.Get()))
			{
				bAnyClickableChecked = true;
				if (Primitive->IsCollisionEnabled() &&
					Primitive->GetCollisionResponseToChannel(Interaction.InteractionTraceChannel) == ECR_Block)
				{
					bBlockingFound = true;
				}
			}
		}

		if (bAnyClickableChecked && !bBlockingFound)
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("NoClickCollision"),
				LOCTEXT("NoClickCollision", "No clickable mesh blocks the configured interaction trace channel, so the trace will pass straight through. Enable collision on the meshes, change the channel, or add an interaction proxy."));
		}
	}

	if (Interaction.Mode == EArchOpeningInteractionMode::ProximityOnly ||
		Interaction.Mode == EArchOpeningInteractionMode::ClickAndProximity)
	{
		if (Proximity.BoxExtent.GetMin() <= 0.0f)
		{
			Report.Add(EArchOpeningIssueSeverity::Error, TEXT("BadProximityExtent"),
				LOCTEXT("BadProximityExtent", "The proximity trigger has a zero or negative extent on at least one axis."));
		}

		for (const TSubclassOf<AActor>& Class : Proximity.AllowedOccupantClasses)
		{
			if (Class == nullptr)
			{
				Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("NullOccupantClass"),
					LOCTEXT("NullOccupantClass", "The allowed occupant class list contains an empty entry, which is ignored."));
				break;
			}
		}
	}

	// -- Obstruction --------------------------------------------------------------------------

	if (Obstruction.Policy != EArchOpeningObstructionPolicy::Ignore && !bLeafBoundsCached)
	{
		Report.Add(EArchOpeningIssueSeverity::Warning, TEXT("NoLeafBounds"),
			LOCTEXT("NoLeafBounds", "Obstruction checks are enabled but the leaf has no primitive bounds to sweep. Assign mesh components as leaf parts and recalibrate."));
	}

	// -- Audio (never fatal) ------------------------------------------------------------------

	{
		const bool bAnySound = Audio.HandleActuationSound || Audio.OpenStartSound || Audio.CloseStartSound
			|| Audio.MovementLoopSound || Audio.LatchSound || Audio.EndStopSound;

		if (!bAnySound)
		{
			Report.Add(EArchOpeningIssueSeverity::Info, TEXT("NoSounds"),
				LOCTEXT("NoSounds", "No sounds are assigned. The opening works silently; assign any of the sound slots when you have audio assets."));
		}
	}

	return Report;
}

#undef LOCTEXT_NAMESPACE
