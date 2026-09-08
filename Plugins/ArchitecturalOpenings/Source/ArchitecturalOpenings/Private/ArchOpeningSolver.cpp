// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningSolver.h"

FArchOpeningSolver::FScaleAnalysis FArchOpeningSolver::AnalyzeScale(const FVector& WorldScale)
{
	FScaleAnalysis Analysis;
	Analysis.RawScale = WorldScale;

	const double SignedProduct = WorldScale.X * WorldScale.Y * WorldScale.Z;
	Analysis.bNegative = SignedProduct < 0.0;

	const double AbsX = FMath::Abs(WorldScale.X);
	const double AbsY = FMath::Abs(WorldScale.Y);
	const double AbsZ = FMath::Abs(WorldScale.Z);
	const double Largest = FMath::Max3(AbsX, AbsY, AbsZ);

	if (Largest <= UE_DOUBLE_SMALL_NUMBER)
	{
		// Degenerate scale. Treat as non-uniform so the caller warns instead of dividing by zero.
		Analysis.bUniform = false;
		Analysis.UniformScale = 1.0f;
		return Analysis;
	}

	const double Spread = FMath::Max3(
		FMath::Abs(AbsX - AbsY),
		FMath::Abs(AbsY - AbsZ),
		FMath::Abs(AbsX - AbsZ));

	Analysis.bUniform = (Spread / Largest) <= UniformScaleTolerance;
	Analysis.UniformScale = Analysis.bUniform ? static_cast<float>(AbsX) : 1.0f;

	return Analysis;
}

FTransform FArchOpeningSolver::MakeCalibrationFrame(const FTransform& ComponentWorld, FScaleAnalysis& OutAnalysis)
{
	OutAnalysis = AnalyzeScale(ComponentWorld.GetScale3D());

	// Only a uniform, positive scale is folded into the frame. Non-uniform scale composed with the
	// leaf rotation would shear the driven meshes, and a mirrored frame makes the resolved hinge
	// direction ambiguous, so both are dropped to 1.0 and reported by the caller.
	const float FrameScale = (OutAnalysis.bUniform && !OutAnalysis.bNegative)
		? FMath::Max(OutAnalysis.UniformScale, UE_KINDA_SMALL_NUMBER)
		: 1.0f;

	return FTransform(ComponentWorld.GetRotation(), ComponentWorld.GetLocation(), FVector(FrameScale));
}

FVector FArchOpeningSolver::ComputeLeftDirection(const FVector& OutsideDirection, const FVector& UpDirection)
{
	const FVector Outside = OutsideDirection.GetSafeNormal();
	const FVector Up = UpDirection.GetSafeNormal();

	if (Outside.IsNearlyZero() || Up.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	// Standing outside and looking in, forward is -Outside. In Unreal's left-handed frame the
	// viewer's right is Up x Forward, so the viewer's LEFT is Up x Outside.
	return FVector::CrossProduct(Up, Outside).GetSafeNormal();
}

FVector FArchOpeningSolver::ComputeHingeAxis(const FArchOpeningHingedSettings& Hinged, const FArchOpeningCalibration& Calibration)
{
	const float InvertSign = Hinged.bInvertDirection ? -1.0f : 1.0f;

	if (Hinged.AxisPreset == EArchOpeningHingeAxisPreset::Custom)
	{
		const FVector Custom = Hinged.CustomAxis.GetSafeNormal();
		return Custom.IsNearlyZero() ? FVector::ZeroVector : (Custom * InvertSign);
	}

	const FVector Up = Calibration.UpDirection.GetSafeNormal();
	const FVector Left = ComputeLeftDirection(Calibration.OutsideDirection, Calibration.UpDirection);

	if (Up.IsNearlyZero() || Left.IsNearlyZero())
	{
		// Outside and Up are parallel (or one is zero): the frame is degenerate.
		return FVector::ZeroVector;
	}

	const float SwingSign = (Hinged.SwingDirection == EArchOpeningSwingDirection::Outward) ? 1.0f : -1.0f;

	switch (Hinged.AxisPreset)
	{
	case EArchOpeningHingeAxisPreset::VerticalSide:
	{
		// Derived in the docs: left hinge + outward swing rotates positively about +Up.
		const float SideSign = (Hinged.HingeSide == EArchOpeningHingeSide::Left) ? 1.0f : -1.0f;
		return Up * (SideSign * SwingSign * InvertSign);
	}

	case EArchOpeningHingeAxisPreset::HorizontalTop:
		// Awning: the free edge is the bottom rail, so outward is a negative rotation about Left.
		return Left * (-SwingSign * InvertSign);

	case EArchOpeningHingeAxisPreset::HorizontalBottom:
		// Hopper: mirror of the awning case.
		return Left * (SwingSign * InvertSign);

	default:
		return FVector::ZeroVector;
	}
}

FVector FArchOpeningSolver::ComputeDefaultSlideDirection(const FArchOpeningCalibration& Calibration)
{
	return ComputeLeftDirection(Calibration.OutsideDirection, Calibration.UpDirection);
}

FVector FArchOpeningSolver::ComputeSlideDirection(const FArchOpeningSlidingSettings& Sliding, const FArchOpeningCalibration& Calibration)
{
	FVector Direction = Sliding.SlideDirection;

	if (Direction.Size() < MinDirectionLength)
	{
		Direction = ComputeDefaultSlideDirection(Calibration);
	}

	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	return Sliding.bInvertDirection ? -Direction : Direction;
}

FTransform FArchOpeningSolver::MakeRotationDelta(const FVector& PivotLocation, const FVector& Axis, float AngleDegrees)
{
	const FVector NormalizedAxis = Axis.GetSafeNormal();
	if (NormalizedAxis.IsNearlyZero() || FMath::IsNearlyZero(AngleDegrees))
	{
		return FTransform::Identity;
	}

	// Axis-angle throughout: no Euler angles are ever formed, so there is nothing to wrap.
	const FQuat Rotation(NormalizedAxis, FMath::DegreesToRadians(AngleDegrees));

	// "Into pivot space, rotate, back out" using Unreal's apply-left-first composition order.
	const FTransform ToPivot(FQuat::Identity, -PivotLocation);
	const FTransform FromPivot(FQuat::Identity, PivotLocation);

	return ToPivot * FTransform(Rotation) * FromPivot;
}

FTransform FArchOpeningSolver::MakeTranslationDelta(const FVector& Direction, float Distance)
{
	const FVector NormalizedDirection = Direction.GetSafeNormal();
	if (NormalizedDirection.IsNearlyZero() || FMath::IsNearlyZero(Distance))
	{
		return FTransform::Identity;
	}

	return FTransform(FQuat::Identity, NormalizedDirection * Distance);
}

FTransform FArchOpeningSolver::ComputeLeafDelta(
	EArchOpeningMotionType MotionType,
	const FArchOpeningHingedSettings& Hinged,
	const FArchOpeningSlidingSettings& Sliding,
	const FArchOpeningCalibration& Calibration,
	float Openness)
{
	const float Alpha = FMath::Clamp(Openness, 0.0f, 1.0f);

	if (MotionType == EArchOpeningMotionType::Hinged)
	{
		const FVector Axis = ComputeHingeAxis(Hinged, Calibration);
		return MakeRotationDelta(Hinged.HingeLocation, Axis, Hinged.OpenAngle * Alpha);
	}

	const FVector Direction = ComputeSlideDirection(Sliding, Calibration);
	return MakeTranslationDelta(Direction, Sliding.TravelDistance * Alpha);
}

FTransform FArchOpeningSolver::ComputeHandleDelta(const FArchOpeningHandleGroup& Group, float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return MakeRotationDelta(Group.PivotLocation, Group.RotationAxis, Group.RotationAngle * Clamped);
}

FTransform FArchOpeningSolver::ComposeWorld(const FTransform& RestRelative, const FTransform& Delta, const FTransform& CalibrationFrame)
{
	return RestRelative * Delta * CalibrationFrame;
}

FTransform FArchOpeningSolver::CaptureRestRelative(const FTransform& PartWorld, const FTransform& CalibrationFrame)
{
	return PartWorld.GetRelativeTransform(CalibrationFrame);
}

float FArchOpeningSolver::ComputeNominalTravel(
	EArchOpeningMotionType MotionType,
	const FArchOpeningHingedSettings& Hinged,
	const FArchOpeningSlidingSettings& Sliding)
{
	return (MotionType == EArchOpeningMotionType::Hinged)
		? FMath::Abs(Hinged.OpenAngle)
		: FMath::Abs(Sliding.TravelDistance);
}
