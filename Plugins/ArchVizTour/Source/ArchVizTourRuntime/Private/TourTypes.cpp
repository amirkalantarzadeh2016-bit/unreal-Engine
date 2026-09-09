// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourTypes.h"

namespace ArchVizTour::Private
{
	/** Tolerance used by the struct equality operators, in centimetres / degrees / generic units. */
	static constexpr float StructCompareTolerance = 1.e-3f;

	static bool NearlyEqual(float A, float B)
	{
		return FMath::IsNearlyEqual(A, B, StructCompareTolerance);
	}

	static bool NearlyEqual(const FVector& A, const FVector& B)
	{
		return A.Equals(B, StructCompareTolerance);
	}

	static bool NearlyEqual(const FRotator& A, const FRotator& B)
	{
		return A.Equals(B, StructCompareTolerance);
	}

	/**
	 * FText has no value equality that survives a JSON round trip (the round-tripped text is a
	 * fresh culture-invariant literal, never the same FTextId), so labels are compared on their
	 * display string. That is exactly the property the serialization tests care about.
	 */
	static bool TextEqual(const FText& A, const FText& B)
	{
		return A.ToString().Equals(B.ToString(), ESearchCase::CaseSensitive);
	}
}

bool FTourPoint::operator==(const FTourPoint& Other) const
{
	using namespace ArchVizTour::Private;

	return NearlyEqual(Location, Other.Location)
		&& NearlyEqual(ArriveTangent, Other.ArriveTangent)
		&& NearlyEqual(LeaveTangent, Other.LeaveTangent)
		&& NearlyEqual(Rotation, Other.Rotation)
		&& bUseExplicitRotation == Other.bUseExplicitRotation
		&& NearlyEqual(FocalLength, Other.FocalLength)
		&& NearlyEqual(Aperture, Other.Aperture)
		&& NearlyEqual(ManualFocusDistance, Other.ManualFocusDistance)
		&& bUseLookAtTarget == Other.bUseLookAtTarget
		&& LookAtTargetName == Other.LookAtTargetName
		&& NearlyEqual(Speed, Other.Speed)
		&& NearlyEqual(DwellTime, Other.DwellTime)
		&& NearlyEqual(EaseIn, Other.EaseIn)
		&& NearlyEqual(EaseOut, Other.EaseOut)
		&& PointType == Other.PointType
		&& TextEqual(Label, Other.Label);
}

int32 FTourPathData::GetSegmentCount() const
{
	if (Points.Num() < 2)
	{
		return 0;
	}

	return bClosedLoop ? Points.Num() : Points.Num() - 1;
}

bool FTourPathData::operator==(const FTourPathData& Other) const
{
	using namespace ArchVizTour::Private;

	if (Points.Num() != Other.Points.Num()
		|| bClosedLoop != Other.bClosedLoop
		|| !NearlyEqual(DefaultSpeed, Other.DefaultSpeed)
		|| !SplineWorldTransform.Equals(Other.SplineWorldTransform, StructCompareTolerance))
	{
		return false;
	}

	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Points[Index] != Other.Points[Index])
		{
			return false;
		}
	}

	return true;
}

bool FTourStep::operator==(const FTourStep& Other) const
{
	using namespace ArchVizTour::Private;

	return StepType == Other.StepType
		&& TextEqual(Label, Other.Label)
		&& NearlyEqual(Duration, Other.Duration)
		&& NearlyEqual(BlendTime, Other.BlendTime)
		&& BlendFunction == Other.BlendFunction
		&& NearlyEqual(BlendExp, Other.BlendExp)
		&& bLockOutgoing == Other.bLockOutgoing
		&& SplinePathRef == Other.SplinePathRef
		&& NearlyEqual(StartInputKey, Other.StartInputKey)
		&& NearlyEqual(EndInputKey, Other.EndInputKey)
		&& StaticCameraRef == Other.StaticCameraRef
		&& bPauseAtEnd == Other.bPauseAtEnd
		&& CustomEventTag == Other.CustomEventTag;
}

FTourCameraState FTourCameraState::Blend(const FTourCameraState& A, const FTourCameraState& B, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);

	FTourCameraState Result;
	Result.Location      = FMath::Lerp(A.Location, B.Location, T);
	Result.Rotation      = FQuat::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), T).Rotator();
	Result.FocalLength   = FMath::Lerp(A.FocalLength, B.FocalLength, T);
	Result.Aperture      = FMath::Lerp(A.Aperture, B.Aperture, T);
	// A zero focus distance means "unset"; lerping towards it would drag a valid distance to
	// zero mid-blend, so the endpoint that actually has a value wins.
	if (A.FocusDistance > 0.0f && B.FocusDistance > 0.0f)
	{
		Result.FocusDistance = FMath::Lerp(A.FocusDistance, B.FocusDistance, T);
	}
	else
	{
		Result.FocusDistance = (T < 0.5f) ? A.FocusDistance : B.FocusDistance;
	}
	Result.bValid = A.bValid && B.bValid;
	return Result;
}
