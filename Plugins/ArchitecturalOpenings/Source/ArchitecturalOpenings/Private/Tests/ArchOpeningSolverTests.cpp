// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ArchOpeningEasing.h"
#include "ArchOpeningSolver.h"
#include "ArchOpeningTypes.h"

namespace ArchOpeningTestHelpers
{
	FArchOpeningCalibration MakeCalibration(const FVector& Outside = FVector(1, 0, 0), const FVector& Up = FVector(0, 0, 1))
	{
		FArchOpeningCalibration Calibration;
		Calibration.OutsideDirection = Outside;
		Calibration.UpDirection = Up;
		Calibration.bCalibrated = true;
		return Calibration;
	}

	FArchOpeningHingedSettings MakeHinged(EArchOpeningHingeSide Side, EArchOpeningSwingDirection Swing, const FVector& HingeLocation)
	{
		FArchOpeningHingedSettings Hinged;
		Hinged.AxisPreset = EArchOpeningHingeAxisPreset::VerticalSide;
		Hinged.HingeSide = Side;
		Hinged.SwingDirection = Swing;
		Hinged.HingeLocation = HingeLocation;
		Hinged.OpenAngle = 90.0f;
		return Hinged;
	}
}

// ------------------------------------------------------------------------------------------------
// Handing and swing direction
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningHandingTest,
	"ArchitecturalOpenings.Solver.HingeHandingAndSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningHandingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningTestHelpers;

	// Outside is +X, up is +Z. Standing outside looking in (-X), the viewer's left is +Y.
	const FArchOpeningCalibration Calibration = MakeCalibration();

	const FVector Left = FArchOpeningSolver::ComputeLeftDirection(Calibration.OutsideDirection, Calibration.UpDirection);
	TestTrue(TEXT("Viewer's left resolves to +Y"), Left.Equals(FVector(0, 1, 0), 1e-4));

	// A left-hinged leaf: hinge at +Y, free edge toward -Y.
	const FVector HingeLocation(0, 100, 0);
	const FVector FreeEdgeRest(0, 0, 0);	// 100 cm from the hinge, toward -Y.

	FArchOpeningSlidingSettings UnusedSliding;

	{
		// Outward swing must carry the free edge toward +X (the outside).
		const FArchOpeningHingedSettings Hinged =
			MakeHinged(EArchOpeningHingeSide::Left, EArchOpeningSwingDirection::Outward, HingeLocation);

		const FTransform Delta = FArchOpeningSolver::ComputeLeafDelta(
			EArchOpeningMotionType::Hinged, Hinged, UnusedSliding, Calibration, 1.0f);

		const FVector Moved = Delta.TransformPosition(FreeEdgeRest);
		TestTrue(TEXT("Left-hinged outward moves the free edge to +X"), Moved.X > 50.0);
		TestTrue(TEXT("Left-hinged outward keeps the hinge fixed"),
			Delta.TransformPosition(HingeLocation).Equals(HingeLocation, 1e-3));
	}

	{
		// Inward swing must carry it the other way.
		const FArchOpeningHingedSettings Hinged =
			MakeHinged(EArchOpeningHingeSide::Left, EArchOpeningSwingDirection::Inward, HingeLocation);

		const FTransform Delta = FArchOpeningSolver::ComputeLeafDelta(
			EArchOpeningMotionType::Hinged, Hinged, UnusedSliding, Calibration, 1.0f);

		TestTrue(TEXT("Left-hinged inward moves the free edge to -X"),
			Delta.TransformPosition(FreeEdgeRest).X < -50.0);
	}

	{
		// Right-hinged: hinge at -Y, free edge toward +Y, outward must still go to +X.
		const FVector RightHinge(0, -100, 0);
		const FArchOpeningHingedSettings Hinged =
			MakeHinged(EArchOpeningHingeSide::Right, EArchOpeningSwingDirection::Outward, RightHinge);

		const FTransform Delta = FArchOpeningSolver::ComputeLeafDelta(
			EArchOpeningMotionType::Hinged, Hinged, UnusedSliding, Calibration, 1.0f);

		TestTrue(TEXT("Right-hinged outward moves the free edge to +X"),
			Delta.TransformPosition(FVector::ZeroVector).X > 50.0);
	}

	{
		// Top-hinged awning: hinge at the head, free edge at the sill, outward pushes the sill out.
		FArchOpeningHingedSettings Hinged;
		Hinged.AxisPreset = EArchOpeningHingeAxisPreset::HorizontalTop;
		Hinged.SwingDirection = EArchOpeningSwingDirection::Outward;
		Hinged.HingeLocation = FVector(0, 0, 100);
		Hinged.OpenAngle = 30.0f;

		const FTransform Delta = FArchOpeningSolver::ComputeLeafDelta(
			EArchOpeningMotionType::Hinged, Hinged, UnusedSliding, Calibration, 1.0f);

		const FVector BottomEdge = Delta.TransformPosition(FVector::ZeroVector);
		TestTrue(TEXT("Top-hinged outward pushes the bottom edge to +X"), BottomEdge.X > 10.0);
		TestTrue(TEXT("Top-hinged outward lifts the bottom edge"), BottomEdge.Z > 1.0);
	}

	{
		// Bottom-hinged hopper: mirror image of the awning.
		FArchOpeningHingedSettings Hinged;
		Hinged.AxisPreset = EArchOpeningHingeAxisPreset::HorizontalBottom;
		Hinged.SwingDirection = EArchOpeningSwingDirection::Outward;
		Hinged.HingeLocation = FVector::ZeroVector;
		Hinged.OpenAngle = 30.0f;

		const FTransform Delta = FArchOpeningSolver::ComputeLeafDelta(
			EArchOpeningMotionType::Hinged, Hinged, UnusedSliding, Calibration, 1.0f);

		const FVector TopEdge = Delta.TransformPosition(FVector(0, 0, 100));
		TestTrue(TEXT("Bottom-hinged outward pushes the top edge to +X"), TopEdge.X > 10.0);
	}

	{
		// Invert must flip the resolved direction without touching the presets.
		FArchOpeningHingedSettings Hinged =
			MakeHinged(EArchOpeningHingeSide::Left, EArchOpeningSwingDirection::Outward, HingeLocation);
		Hinged.bInvertDirection = true;

		const FTransform Delta = FArchOpeningSolver::ComputeLeafDelta(
			EArchOpeningMotionType::Hinged, Hinged, UnusedSliding, Calibration, 1.0f);

		TestTrue(TEXT("Invert flips the swing"), Delta.TransformPosition(FreeEdgeRest).X < -50.0);
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
// Rotated openings and drift
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningRotatedFrameTest,
	"ArchitecturalOpenings.Solver.RotatedFrameAndDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningRotatedFrameTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningTestHelpers;

	const FArchOpeningCalibration Calibration = MakeCalibration();
	const FArchOpeningHingedSettings Hinged =
		MakeHinged(EArchOpeningHingeSide::Left, EArchOpeningSwingDirection::Outward, FVector(0, 100, 0));
	const FArchOpeningSlidingSettings Sliding;

	// An opening placed at an arbitrary rotation and translation somewhere in the level.
	const FTransform Frame(
		FRotator(0.0f, 37.5f, 0.0f).Quaternion(),
		FVector(1234.0f, -567.0f, 89.0f),
		FVector::OneVector);

	// A leaf part sitting somewhere in that frame.
	const FTransform PartWorldRest = FTransform(FRotator(0, 37.5f, 0).Quaternion(), FVector(1234.0f, -517.0f, 189.0f));
	const FTransform Rest = FArchOpeningSolver::CaptureRestRelative(PartWorldRest, Frame);

	// Closed pose must reproduce the captured world transform exactly.
	{
		const FTransform Closed = FArchOpeningSolver::ComposeWorld(
			Rest, FArchOpeningSolver::ComputeLeafDelta(EArchOpeningMotionType::Hinged, Hinged, Sliding, Calibration, 0.0f), Frame);

		TestTrue(TEXT("Closed pose reproduces the captured world transform"), Closed.Equals(PartWorldRest, 1e-3));
	}

	// The leaf must rotate about the world-space hinge, which is the local hinge mapped by the frame.
	{
		const FTransform Open = FArchOpeningSolver::ComposeWorld(
			Rest, FArchOpeningSolver::ComputeLeafDelta(EArchOpeningMotionType::Hinged, Hinged, Sliding, Calibration, 1.0f), Frame);

		const FQuat DeltaRotation = Open.GetRotation() * PartWorldRest.GetRotation().Inverse();
		FVector Axis;
		float AngleRadians = 0.0f;
		DeltaRotation.ToAxisAndAngle(Axis, AngleRadians);

		TestTrue(TEXT("Fully open rotates by the configured angle"),
			FMath::IsNearlyEqual(FMath::RadiansToDegrees(AngleRadians), 90.0f, 0.01f));

		// The rotation axis must be the frame-rotated local up, not world up by accident: with a yaw
		// only rotation those coincide, so check the axis is unit and vertical.
		TestTrue(TEXT("Rotation axis is the frame's up"), FMath::IsNearlyEqual(FMath::Abs(Axis.Z), 1.0, 1e-3));
	}

	// 200 open/close cycles must return to a bit-comparable closed pose: poses are recomputed from
	// the stored rest, never accumulated, so there is nothing for error to build up in.
	{
		FTransform Latest = FTransform::Identity;
		for (int32 Cycle = 0; Cycle < 200; ++Cycle)
		{
			for (int32 Step = 0; Step <= 10; ++Step)
			{
				const float Alpha = static_cast<float>(Step) / 10.0f;
				Latest = FArchOpeningSolver::ComposeWorld(
					Rest, FArchOpeningSolver::ComputeLeafDelta(EArchOpeningMotionType::Hinged, Hinged, Sliding, Calibration, Alpha), Frame);
			}

			Latest = FArchOpeningSolver::ComposeWorld(
				Rest, FArchOpeningSolver::ComputeLeafDelta(EArchOpeningMotionType::Hinged, Hinged, Sliding, Calibration, 0.0f), Frame);
		}

		TestTrue(TEXT("200 cycles leave no positional or rotational drift"), Latest.Equals(PartWorldRest, 1e-4));
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
// Sliding
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningSlidingTest,
	"ArchitecturalOpenings.Solver.SlidingTravel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningSlidingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningTestHelpers;

	const FArchOpeningCalibration Calibration = MakeCalibration();
	const FArchOpeningHingedSettings UnusedHinged;

	FArchOpeningSlidingSettings Sliding;
	Sliding.SlideDirection = FVector(0, 1, 0);
	Sliding.TravelDistance = 137.5f;

	const FTransform Frame(FRotator(0, 90.0f, 0).Quaternion(), FVector(500, 0, 0));
	const FTransform PartWorldRest(FQuat::Identity, FVector(500, 0, 100));
	const FTransform Rest = FArchOpeningSolver::CaptureRestRelative(PartWorldRest, Frame);

	const FTransform Open = FArchOpeningSolver::ComposeWorld(
		Rest, FArchOpeningSolver::ComputeLeafDelta(EArchOpeningMotionType::Sliding, UnusedHinged, Sliding, Calibration, 1.0f), Frame);

	const float Travelled = FVector::Dist(Open.GetLocation(), PartWorldRest.GetLocation());
	TestTrue(TEXT("Slide travels exactly the configured distance"),
		FMath::IsNearlyEqual(Travelled, 137.5f, 0.01f));

	TestTrue(TEXT("Slide does not rotate the panel"),
		Open.GetRotation().Equals(PartWorldRest.GetRotation(), 1e-4));

	// Half open must be exactly half the travel: the geometric mapping is linear in openness, so any
	// non-linearity the artist sees comes from easing alone.
	const FTransform Half = FArchOpeningSolver::ComposeWorld(
		Rest, FArchOpeningSolver::ComputeLeafDelta(EArchOpeningMotionType::Sliding, UnusedHinged, Sliding, Calibration, 0.5f), Frame);

	TestTrue(TEXT("Half open is half the travel"),
		FMath::IsNearlyEqual(FVector::Dist(Half.GetLocation(), PartWorldRest.GetLocation()), 137.5f * 0.5f, 0.01f));

	// Inversion must reverse the direction and keep the magnitude.
	Sliding.bInvertDirection = true;
	const FTransform Inverted = FArchOpeningSolver::ComposeWorld(
		Rest, FArchOpeningSolver::ComputeLeafDelta(EArchOpeningMotionType::Sliding, UnusedHinged, Sliding, Calibration, 1.0f), Frame);

	TestTrue(TEXT("Inverted slide travels the same distance the other way"),
		FMath::IsNearlyEqual(FVector::Dist(Inverted.GetLocation(), PartWorldRest.GetLocation()), 137.5f, 0.01f));
	TestTrue(TEXT("Inverted slide is opposite to the original"),
		FVector::DotProduct(Open.GetLocation() - PartWorldRest.GetLocation(),
			Inverted.GetLocation() - PartWorldRest.GetLocation()) < 0.0);

	return true;
}

// ------------------------------------------------------------------------------------------------
// Handles
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningHandleCompositionTest,
	"ArchitecturalOpenings.Solver.HandleInheritsLeafMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningHandleCompositionTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ArchOpeningTestHelpers;

	const FArchOpeningCalibration Calibration = MakeCalibration();
	const FArchOpeningHingedSettings Hinged =
		MakeHinged(EArchOpeningHingeSide::Left, EArchOpeningSwingDirection::Outward, FVector(0, 100, 0));
	const FArchOpeningSlidingSettings Sliding;
	const FTransform Frame = FTransform::Identity;

	FArchOpeningHandleGroup Group;
	Group.PivotLocation = FVector(0, 10, 100);
	Group.RotationAxis = FVector(1, 0, 0);
	Group.RotationAngle = -40.0f;

	const FTransform HandleWorldRest(FQuat::Identity, FVector(-5, 10, 100));
	const FTransform HandleRest = FArchOpeningSolver::CaptureRestRelative(HandleWorldRest, Frame);

	const FTransform LeafOpen = FArchOpeningSolver::ComputeLeafDelta(
		EArchOpeningMotionType::Hinged, Hinged, Sliding, Calibration, 1.0f);

	// Handle at rest, leaf open: the handle must land exactly where the leaf motion alone puts it.
	{
		const FTransform Composed = FArchOpeningSolver::ComposeWorld(
			HandleRest, FArchOpeningSolver::ComputeHandleDelta(Group, 0.0f) * LeafOpen, Frame);

		const FTransform LeafOnly = FArchOpeningSolver::ComposeWorld(HandleRest, LeafOpen, Frame);

		TestTrue(TEXT("An unactuated handle exactly follows the leaf"), Composed.Equals(LeafOnly, 1e-4));
	}

	// Handle actuated, leaf closed: the handle rotates about its own pivot and the pivot stays put.
	{
		const FTransform HandleDelta = FArchOpeningSolver::ComputeHandleDelta(Group, 1.0f);

		TestTrue(TEXT("The handle pivot is a fixed point of the handle rotation"),
			HandleDelta.TransformPosition(Group.PivotLocation).Equals(Group.PivotLocation, 1e-3));

		const FTransform Composed = FArchOpeningSolver::ComposeWorld(HandleRest, HandleDelta, Frame);
		TestTrue(TEXT("An actuated handle moves away from its rest pose"),
			!Composed.GetRotation().Equals(HandleWorldRest.GetRotation(), 1e-3));
	}

	// Handle actuated AND leaf open: the composition must equal "rotate locally, then swing", i.e.
	// the leaf motion is applied once, not twice.
	{
		const FTransform HandleDelta = FArchOpeningSolver::ComputeHandleDelta(Group, 1.0f);
		const FTransform Composed = FArchOpeningSolver::ComposeWorld(HandleRest, HandleDelta * LeafOpen, Frame);

		const FTransform Expected = HandleRest * HandleDelta * LeafOpen * Frame;
		TestTrue(TEXT("Handle composition applies the leaf delta exactly once"), Composed.Equals(Expected, 1e-4));

		// The distance from the handle to the hinge line must be unchanged by the leaf swing.
		const FVector HingeAxis = FArchOpeningSolver::ComputeHingeAxis(Hinged, Calibration);
		const FVector ToRest = HandleWorldRest.GetLocation() - Hinged.HingeLocation;
		const FVector ToOpen = Composed.GetLocation() - Hinged.HingeLocation;

		const double RestRadius = (ToRest - HingeAxis * FVector::DotProduct(ToRest, HingeAxis)).Size();
		const double OpenRadius = (ToOpen - HingeAxis * FVector::DotProduct(ToOpen, HingeAxis)).Size();

		// The handle's own rotation moves it a little, so this is a sanity bound rather than equality.
		TestTrue(TEXT("The handle stays on roughly its hinge radius"),
			FMath::Abs(RestRadius - OpenRadius) < 20.0);
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
// Scale analysis
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningScaleTest,
	"ArchitecturalOpenings.Solver.ScaleHandling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningScaleTest::RunTest(const FString& /*Parameters*/)
{
	{
		const FArchOpeningSolver::FScaleAnalysis Analysis = FArchOpeningSolver::AnalyzeScale(FVector(2, 2, 2));
		TestTrue(TEXT("Uniform scale is detected"), Analysis.bUniform);
		TestFalse(TEXT("Uniform positive scale is not flagged negative"), Analysis.bNegative);
		TestTrue(TEXT("Uniform scale value is captured"), FMath::IsNearlyEqual(Analysis.UniformScale, 2.0f));
	}

	{
		const FArchOpeningSolver::FScaleAnalysis Analysis = FArchOpeningSolver::AnalyzeScale(FVector(1, 2, 1));
		TestFalse(TEXT("Non-uniform scale is detected"), Analysis.bUniform);
	}

	{
		const FArchOpeningSolver::FScaleAnalysis Analysis = FArchOpeningSolver::AnalyzeScale(FVector(-1, 1, 1));
		TestTrue(TEXT("Mirrored scale is detected"), Analysis.bNegative);
	}

	// A uniform positive scale must survive into the frame; anything else must be dropped to 1 so a
	// rotation composed with it cannot shear the driven meshes.
	{
		FArchOpeningSolver::FScaleAnalysis Analysis;
		const FTransform Frame = FArchOpeningSolver::MakeCalibrationFrame(
			FTransform(FQuat::Identity, FVector::ZeroVector, FVector(3, 3, 3)), Analysis);
		TestTrue(TEXT("Uniform scale is carried into the frame"), Frame.GetScale3D().Equals(FVector(3, 3, 3), 1e-4));
	}

	{
		FArchOpeningSolver::FScaleAnalysis Analysis;
		const FTransform Frame = FArchOpeningSolver::MakeCalibrationFrame(
			FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1, 2, 3)), Analysis);
		TestTrue(TEXT("Non-uniform scale is dropped from the frame"), Frame.GetScale3D().Equals(FVector::OneVector, 1e-4));
		TestFalse(TEXT("Non-uniform scale is reported"), Analysis.bUniform);
	}

	{
		FArchOpeningSolver::FScaleAnalysis Analysis;
		const FTransform Frame = FArchOpeningSolver::MakeCalibrationFrame(
			FTransform(FQuat::Identity, FVector::ZeroVector, FVector(-2, -2, -2)), Analysis);
		TestTrue(TEXT("Mirrored scale is dropped from the frame"), Frame.GetScale3D().Equals(FVector::OneVector, 1e-4));
		TestTrue(TEXT("Mirrored scale is reported"), Analysis.bNegative);
	}

	// A part's own scale, including a mirrored one, must survive a capture/recompose round trip
	// untouched: the plugin never rewrites part scale.
	{
		const FTransform Frame(FRotator(0, 45, 0).Quaternion(), FVector(10, 20, 30), FVector(2, 2, 2));
		const FTransform PartWorld(FRotator(10, 20, 30).Quaternion(), FVector(1, 2, 3), FVector(-1.5f, 1.5f, 1.5f));

		const FTransform Rest = FArchOpeningSolver::CaptureRestRelative(PartWorld, Frame);
		const FTransform Recomposed = FArchOpeningSolver::ComposeWorld(Rest, FTransform::Identity, Frame);

		TestTrue(TEXT("A mirrored part survives capture and recompose"), Recomposed.Equals(PartWorld, 1e-3));
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
// Easing
// ------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArchOpeningEasingTest,
	"ArchitecturalOpenings.Easing.ContractAndInversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchOpeningEasingTest::RunTest(const FString& /*Parameters*/)
{
	const EArchOpeningEasing Types[] =
	{
		EArchOpeningEasing::Linear,
		EArchOpeningEasing::SmoothStep,
		EArchOpeningEasing::EaseIn,
		EArchOpeningEasing::EaseOut,
		EArchOpeningEasing::EaseInOut
	};

	for (EArchOpeningEasing Type : Types)
	{
		TestTrue(TEXT("Easing maps 0 to 0"),
			FMath::IsNearlyEqual(FArchOpeningEasing::EvaluateBuiltIn(Type, 0.0f), 0.0f, 1e-4f));
		TestTrue(TEXT("Easing maps 1 to 1"),
			FMath::IsNearlyEqual(FArchOpeningEasing::EvaluateBuiltIn(Type, 1.0f), 1.0f, 1e-4f));

		float Previous = 0.0f;
		for (int32 Index = 0; Index <= 64; ++Index)
		{
			const float T = static_cast<float>(Index) / 64.0f;
			const float Value = FArchOpeningEasing::EvaluateBuiltIn(Type, T);

			TestTrue(TEXT("Easing stays inside 0..1"), Value >= -1e-4f && Value <= 1.0f + 1e-4f);
			TestTrue(TEXT("Easing is non-decreasing"), Value >= Previous - 1e-4f);
			Previous = Value;
		}

		// Out-of-range input must be clamped, not extrapolated: this is what stops a bad progress
		// value from driving the leaf past its configured range.
		TestTrue(TEXT("Easing clamps below zero"),
			FMath::IsNearlyEqual(FArchOpeningEasing::EvaluateBuiltIn(Type, -5.0f), 0.0f, 1e-4f));
		TestTrue(TEXT("Easing clamps above one"),
			FMath::IsNearlyEqual(FArchOpeningEasing::EvaluateBuiltIn(Type, 5.0f), 1.0f, 1e-4f));

		// Inversion must round-trip.
		for (int32 Index = 0; Index <= 20; ++Index)
		{
			const float T = static_cast<float>(Index) / 20.0f;
			const float Value = FArchOpeningEasing::Evaluate(Type, nullptr, false, T);
			const float Recovered = FArchOpeningEasing::InverseEvaluate(Type, nullptr, false, Value);

			TestTrue(TEXT("Inverse easing round-trips through the value"),
				FMath::IsNearlyEqual(FArchOpeningEasing::Evaluate(Type, nullptr, false, Recovered), Value, 1e-3f));
		}
	}

	// Custom with no valid curve must fall back to a well behaved built-in rather than misbehaving.
	TestTrue(TEXT("Custom without a valid curve falls back"),
		FMath::IsNearlyEqual(FArchOpeningEasing::Evaluate(EArchOpeningEasing::Custom, nullptr, false, 0.5f),
			FArchOpeningEasing::EvaluateBuiltIn(EArchOpeningEasing::SmoothStep, 0.5f), 1e-4f));

	FText Reason;
	TestFalse(TEXT("A null curve is rejected"), FArchOpeningEasing::ValidateCurve(nullptr, Reason));
	TestFalse(TEXT("Rejection gives a reason"), Reason.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
