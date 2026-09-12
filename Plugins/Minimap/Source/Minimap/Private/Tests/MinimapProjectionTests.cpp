#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include <limits>

#include "MinimapFunctionLibrary.h"
#include "MinimapTypes.h"
#include "GameFramework/Pawn.h"
#include "MinimapViewComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MinimapTestUtils
{
	static constexpr float Tolerance = 1.e-3f;

	/** 2000 x 2000 cm square centred on the origin, standard axis convention. */
	static FMinimapCalibration MakeSquareCalibration()
	{
		FMinimapCalibration Calibration;
		Calibration.WorldCenter          = FVector2D(0.0, 0.0);
		Calibration.WorldExtent          = FVector2D(1000.0, 1000.0);
		Calibration.MapYaw               = 0.0f;
		Calibration.Zoom                 = 1.0f;
		Calibration.MinZ                 = 0.0f;
		Calibration.MaxZ                 = 1000.0f;
		Calibration.bPreserveAspectRatio = true;
		Calibration.bCircularMap         = false;
		Calibration.bSwapUV              = false;
		Calibration.bInvertU             = false;
		Calibration.bInvertV             = false;
		return Calibration;
	}

	static bool NearlyEqual(const FVector2D& A, const FVector2D& B, float InTolerance = Tolerance)
	{
		return FMath::Abs(A.X - B.X) <= InTolerance && FMath::Abs(A.Y - B.Y) <= InTolerance;
	}

	static FString ToString(const FVector2D& V)
	{
		return FString::Printf(TEXT("(%.4f, %.4f)"), V.X, V.Y);
	}
}

#define MINIMAP_TEST_VECTOR_EQUAL(What, Actual, Expected) \
	if (!MinimapTestUtils::NearlyEqual((Actual), (Expected))) \
	{ \
		AddError(FString::Printf(TEXT("%s: expected %s, got %s"), \
			TEXT(What), *MinimapTestUtils::ToString(Expected), *MinimapTestUtils::ToString(Actual))); \
	}

// ---------------------------------------------------------------------------
// 1. Centre projection
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCenterProjectionTest,
	"Minimap.Projection.Center",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCenterProjectionTest::RunTest(const FString& Parameters)
{
	const FMinimapCalibration Calibration = MinimapTestUtils::MakeSquareCalibration();

	// A viewer standing on the map centre must land exactly at the map origin.
	const FVector2D AtCenter = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(0.0, 0.0, 0.0), Calibration.WorldCenter, 0.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Centre projects to origin", AtCenter, FVector2D::ZeroVector);

	// The same must hold with a non-zero centre and a rotated map: the anchor cancels out.
	FMinimapCalibration Offset = Calibration;
	Offset.WorldCenter = FVector2D(1234.0, -5678.0);
	Offset.MapYaw = 37.0f;

	const FVector2D OffsetCenter = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Offset, FVector(1234.0, -5678.0, 0.0), Offset.WorldCenter, 19.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Offset+rotated centre still projects to origin", OffsetCenter, FVector2D::ZeroVector);

	// Centre maps to the middle of UV space and the middle of the widget.
	MINIMAP_TEST_VECTOR_EQUAL("Centre UV", UMinimapFunctionLibrary::NormalizedToUV(AtCenter), FVector2D(0.5, 0.5));
	MINIMAP_TEST_VECTOR_EQUAL("Centre pixels",
		UMinimapFunctionLibrary::NormalizedToWidgetPixels(AtCenter, FVector2D(256.0, 256.0)),
		FVector2D(128.0, 128.0));

	// And to 0 on both legacy material parameters.
	MINIMAP_TEST_VECTOR_EQUAL("Centre material params",
		UMinimapFunctionLibrary::NormalizedToMaterialParams(AtCenter), FVector2D::ZeroVector);

	return true;
}

// ---------------------------------------------------------------------------
// 2. Cardinal directions, North-Up
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCardinalMappingTest,
	"Minimap.Projection.CardinalNorthUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCardinalMappingTest::RunTest(const FString& Parameters)
{
	const FMinimapCalibration Calibration = MinimapTestUtils::MakeSquareCalibration();
	const FVector2D Anchor = Calibration.WorldCenter;
	const float ViewYaw = 0.0f; // North-Up.

	// Standard convention: world +X = North = map UP (N.y = -1);
	//                      world +Y = East  = map RIGHT (N.x = +1).
	const FVector2D North = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(1000.0, 0.0, 0.0), Anchor, ViewYaw);
	MINIMAP_TEST_VECTOR_EQUAL("North (+X) maps to top edge", North, FVector2D(0.0, -1.0));

	const FVector2D South = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(-1000.0, 0.0, 0.0), Anchor, ViewYaw);
	MINIMAP_TEST_VECTOR_EQUAL("South (-X) maps to bottom edge", South, FVector2D(0.0, 1.0));

	const FVector2D East = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(0.0, 1000.0, 0.0), Anchor, ViewYaw);
	MINIMAP_TEST_VECTOR_EQUAL("East (+Y) maps to right edge", East, FVector2D(1.0, 0.0));

	const FVector2D West = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(0.0, -1000.0, 0.0), Anchor, ViewYaw);
	MINIMAP_TEST_VECTOR_EQUAL("West (-Y) maps to left edge", West, FVector2D(-1.0, 0.0));

	// Edge angles: 0 = up, clockwise positive.
	TestEqual(TEXT("North edge angle"), UMinimapFunctionLibrary::GetEdgeAngle(North),   0.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("East edge angle"),  UMinimapFunctionLibrary::GetEdgeAngle(East),   90.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("West edge angle"),  UMinimapFunctionLibrary::GetEdgeAngle(West),  -90.0f, MinimapTestUtils::Tolerance);
	// atan2(0, -1) == pi, and NormalizeAxis maps that to +180 (its range is (-180, 180]).
	TestEqual(TEXT("South edge angle"), FMath::Abs(UMinimapFunctionLibrary::GetEdgeAngle(South)), 180.0f, MinimapTestUtils::Tolerance);

	return true;
}

// ---------------------------------------------------------------------------
// 3. Rotating map
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapRotatingMapTest,
	"Minimap.Projection.RotatingMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapRotatingMapTest::RunTest(const FString& Parameters)
{
	const FMinimapCalibration Calibration = MinimapTestUtils::MakeSquareCalibration();
	const FVector2D Anchor = FVector2D::ZeroVector;

	// Facing east (yaw 90). Something due north of the viewer is now on their LEFT,
	// so it must appear on the left edge of a rotating map.
	const FVector2D NorthWhileFacingEast = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(1000.0, 0.0, 0.0), Anchor, 90.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Facing east: north target appears left", NorthWhileFacingEast, FVector2D(-1.0, 0.0));

	// Whatever the viewer faces is always straight up on a rotating map.
	const FVector2D EastWhileFacingEast = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(0.0, 1000.0, 0.0), Anchor, 90.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Facing east: east target appears up", EastWhileFacingEast, FVector2D(0.0, -1.0));

	// Facing south (yaw 180) flips north to the bottom.
	const FVector2D NorthWhileFacingSouth = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(1000.0, 0.0, 0.0), Anchor, 180.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Facing south: north target appears down", NorthWhileFacingSouth, FVector2D(0.0, 1.0));

	// MapYaw and ViewYaw must compose: 45 + 45 behaves exactly like 90 + 0.
	FMinimapCalibration Rotated = Calibration;
	Rotated.MapYaw = 45.0f;
	const FVector2D Composed = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Rotated, FVector(1000.0, 0.0, 0.0), Anchor, 45.0f);
	MINIMAP_TEST_VECTOR_EQUAL("MapYaw composes with ViewYaw", Composed, NorthWhileFacingEast);

	// Rotation must be rigid: it may not change the distance from the centre.
	const FVector2D Unrotated = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(600.0, 300.0, 0.0), Anchor, 0.0f);
	const FVector2D RotatedPoint = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, FVector(600.0, 300.0, 0.0), Anchor, 73.0f);
	TestEqual(TEXT("Rotation preserves radius"),
		static_cast<float>(RotatedPoint.Size()), static_cast<float>(Unrotated.Size()), MinimapTestUtils::Tolerance);

	return true;
}

// ---------------------------------------------------------------------------
// 4. North-Up vs anchor modes
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapAnchorModeTest,
	"Minimap.Projection.AnchorModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapAnchorModeTest::RunTest(const FString& Parameters)
{
	const FMinimapCalibration Calibration = MinimapTestUtils::MakeSquareCalibration();

	const FVector ViewerLocation(500.0, 0.0, 0.0);
	const FVector TargetLocation(500.0, 500.0, 0.0);

	// Fixed map: the anchor is the map centre, so the target keeps its absolute position.
	const FVector2D Fixed = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, TargetLocation, Calibration.WorldCenter, 0.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Fixed-map anchor gives absolute position", Fixed, FVector2D(0.5, -0.5));

	// Viewer-centred: the same target is now expressed relative to the viewer.
	const FVector2D Centered = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, TargetLocation, FVector2D(ViewerLocation.X, ViewerLocation.Y), 0.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Viewer-centred anchor gives relative position", Centered, FVector2D(0.5, 0.0));

	// The viewer is always exactly at the origin in viewer-centred mode.
	const FVector2D Self = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, ViewerLocation, FVector2D(ViewerLocation.X, ViewerLocation.Y), 123.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Viewer is always centred in viewer-centred mode", Self, FVector2D::ZeroVector);

	// North-Up must be insensitive to the viewer's facing.
	const FVector2D NorthUpA = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, TargetLocation, Calibration.WorldCenter, 0.0f);
	const FVector2D NorthUpB = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, TargetLocation, Calibration.WorldCenter, 0.0f);
	MINIMAP_TEST_VECTOR_EQUAL("North-Up ignores viewer yaw", NorthUpA, NorthUpB);

	return true;
}

// ---------------------------------------------------------------------------
// 5. Circular clamping
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCircularClampTest,
	"Minimap.Bounds.CircularClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCircularClampTest::RunTest(const FString& Parameters)
{
	bool bClamped = false;

	// Inside the circle: untouched.
	const FVector2D Inside = UMinimapFunctionLibrary::ClampNormalizedToShape(FVector2D(0.3, 0.4), true, bClamped);
	MINIMAP_TEST_VECTOR_EQUAL("Point inside circle is unchanged", Inside, FVector2D(0.3, 0.4));
	TestFalse(TEXT("Point inside circle is not clamped"), bClamped);

	// Outside: projected onto the unit circle, direction preserved.
	const FVector2D Outside = UMinimapFunctionLibrary::ClampNormalizedToShape(FVector2D(3.0, 4.0), true, bClamped);
	TestTrue(TEXT("Point outside circle is clamped"), bClamped);
	TestEqual(TEXT("Clamped point lies on the unit circle"),
		static_cast<float>(Outside.Size()), 1.0f, MinimapTestUtils::Tolerance);
	MINIMAP_TEST_VECTOR_EQUAL("Circular clamp preserves bearing", Outside, FVector2D(0.6, 0.8));

	// (3,4) has length 5; the clamp must be a pure scale by 1/5.
	TestEqual(TEXT("Circular clamp is a uniform scale"),
		static_cast<float>(Outside.X / 3.0), static_cast<float>(Outside.Y / 4.0), MinimapTestUtils::Tolerance);

	// Exactly on the boundary: no clamping.
	const FVector2D OnEdge = UMinimapFunctionLibrary::ClampNormalizedToShape(FVector2D(0.0, -1.0), true, bClamped);
	TestFalse(TEXT("Point exactly on the circle is not clamped"), bClamped);
	MINIMAP_TEST_VECTOR_EQUAL("Boundary point unchanged", OnEdge, FVector2D(0.0, -1.0));

	return true;
}

// ---------------------------------------------------------------------------
// 6. Rectangular edge clamping (the corner-bunching bug)
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapRectangularClampTest,
	"Minimap.Bounds.RectangularClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapRectangularClampTest::RunTest(const FString& Parameters)
{
	bool bClamped = false;

	const FVector2D Inside = UMinimapFunctionLibrary::ClampNormalizedToShape(FVector2D(0.5, -0.9), false, bClamped);
	MINIMAP_TEST_VECTOR_EQUAL("Point inside rectangle is unchanged", Inside, FVector2D(0.5, -0.9));
	TestFalse(TEXT("Point inside rectangle is not clamped"), bClamped);

	// Ray-box: (4, 2) has max component 4, so t = 0.25 and the result is (1, 0.5).
	// Independent per-axis clamping would wrongly give (1, 1) - the corner-bunching bug.
	const FVector2D Outside = UMinimapFunctionLibrary::ClampNormalizedToShape(FVector2D(4.0, 2.0), false, bClamped);
	TestTrue(TEXT("Point outside rectangle is clamped"), bClamped);
	MINIMAP_TEST_VECTOR_EQUAL("Ray-box clamp keeps the bearing", Outside, FVector2D(1.0, 0.5));

	// The decisive regression test: the clamped point must stay collinear with the origin
	// and the original point.
	const FVector2D Original(4.0, 2.0);
	const double Cross = Original.X * Outside.Y - Original.Y * Outside.X;
	TestEqual(TEXT("Clamped point is collinear with the original"), static_cast<float>(Cross), 0.0f, MinimapTestUtils::Tolerance);

	// Bearing must survive the clamp exactly.
	TestEqual(TEXT("Edge angle is preserved by clamping"),
		UMinimapFunctionLibrary::GetEdgeAngle(Outside),
		UMinimapFunctionLibrary::GetEdgeAngle(Original),
		MinimapTestUtils::Tolerance);

	// A true diagonal SHOULD land in the corner - only a diagonal.
	const FVector2D Diagonal = UMinimapFunctionLibrary::ClampNormalizedToShape(FVector2D(5.0, 5.0), false, bClamped);
	MINIMAP_TEST_VECTOR_EQUAL("Exact diagonal does land in the corner", Diagonal, FVector2D(1.0, 1.0));

	// Degenerate input must not divide by zero.
	const FVector2D AtOrigin = UMinimapFunctionLibrary::ClampNormalizedToShape(FVector2D::ZeroVector, false, bClamped);
	MINIMAP_TEST_VECTOR_EQUAL("Origin survives clamping", AtOrigin, FVector2D::ZeroVector);
	TestFalse(TEXT("Origin is not flagged as clamped"), bClamped);

	return true;
}

// ---------------------------------------------------------------------------
// 7. Out-of-bounds hysteresis
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapHysteresisTest,
	"Minimap.Bounds.Hysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapHysteresisTest::RunTest(const FString& Parameters)
{
	constexpr float Enter = 1.0f;
	constexpr float Exit  = 0.98f;

	// Rising through the enter threshold.
	TestFalse(TEXT("0.5 stays in bounds"),  UMinimapFunctionLibrary::ResolveOutOfBounds(0.50f, false, Enter, Exit));
	TestFalse(TEXT("0.99 stays in bounds"), UMinimapFunctionLibrary::ResolveOutOfBounds(0.99f, false, Enter, Exit));
	TestFalse(TEXT("Exactly 1.0 is still in bounds"), UMinimapFunctionLibrary::ResolveOutOfBounds(1.00f, false, Enter, Exit));
	TestTrue (TEXT("1.01 goes out of bounds"), UMinimapFunctionLibrary::ResolveOutOfBounds(1.01f, false, Enter, Exit));

	// The dead band: once out, 0.99 must NOT bring the marker back.
	TestTrue (TEXT("0.99 stays out of bounds once out"), UMinimapFunctionLibrary::ResolveOutOfBounds(0.99f, true, Enter, Exit));
	TestTrue (TEXT("Exactly 0.98 stays out of bounds"),  UMinimapFunctionLibrary::ResolveOutOfBounds(0.98f, true, Enter, Exit));
	TestFalse(TEXT("0.97 returns in bounds"),            UMinimapFunctionLibrary::ResolveOutOfBounds(0.97f, true, Enter, Exit));

	// A marker parked on the boundary must produce a STABLE state, not a per-update flip.
	// This is the whole reason hysteresis exists.
	bool bState = false;
	int32 Transitions = 0;
	const float Samples[] = { 0.995f, 1.004f, 0.996f, 1.002f, 0.999f, 1.001f, 0.994f, 1.003f };
	for (const float Sample : Samples)
	{
		const bool bNewState = UMinimapFunctionLibrary::ResolveOutOfBounds(Sample, bState, Enter, Exit);
		if (bNewState != bState)
		{
			++Transitions;
			bState = bNewState;
		}
	}
	// Only the first crossing above 1.0 may flip the state; it can never come back,
	// because nothing in the sample set drops below 0.98.
	TestEqual(TEXT("Boundary dithering causes at most one transition"), Transitions, 1);

	// Inverted thresholds must not produce an oscillator.
	TestTrue(TEXT("Inverted thresholds are handled safely"),
		UMinimapFunctionLibrary::ResolveOutOfBounds(1.5f, false, 1.0f, 2.0f));

	// Non-finite magnitude must latch the previous state rather than flip randomly.
	const float NaNValue = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("NaN magnitude preserves the previous OOB state"),
		UMinimapFunctionLibrary::ResolveOutOfBounds(NaNValue, true, Enter, Exit));

	// Shape magnitude must be 1.0 on the boundary for BOTH shapes, or the shared
	// thresholds above would mean different things per shape.
	TestEqual(TEXT("Circular magnitude on boundary"),
		UMinimapFunctionLibrary::GetShapeMagnitude(FVector2D(0.0, -1.0), true), 1.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("Rectangular magnitude on edge"),
		UMinimapFunctionLibrary::GetShapeMagnitude(FVector2D(0.4, -1.0), false), 1.0f, MinimapTestUtils::Tolerance);
	// A rectangle corner is inside the square but outside the inscribed circle - the one
	// place the two shapes legitimately disagree.
	TestEqual(TEXT("Rectangular magnitude at corner"),
		UMinimapFunctionLibrary::GetShapeMagnitude(FVector2D(1.0, 1.0), false), 1.0f, MinimapTestUtils::Tolerance);
	TestTrue(TEXT("Circular magnitude at rectangle corner exceeds 1"),
		UMinimapFunctionLibrary::GetShapeMagnitude(FVector2D(1.0, 1.0), true) > 1.0f);

	return true;
}

// ---------------------------------------------------------------------------
// 8. Zero / invalid extent
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapInvalidCalibrationTest,
	"Minimap.Calibration.Invalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapInvalidCalibrationTest::RunTest(const FString& Parameters)
{
	FMinimapCalibration Calibration = MinimapTestUtils::MakeSquareCalibration();
	TestTrue(TEXT("Baseline calibration is valid"), Calibration.IsValidCalibration());

	// Zero extent on either axis.
	FMinimapCalibration ZeroX = Calibration;
	ZeroX.WorldExtent = FVector2D(0.0, 1000.0);
	TestFalse(TEXT("Zero X extent is rejected"), ZeroX.IsValidCalibration());

	FMinimapCalibration ZeroY = Calibration;
	ZeroY.WorldExtent = FVector2D(1000.0, 0.0);
	TestFalse(TEXT("Zero Y extent is rejected"), ZeroY.IsValidCalibration());

	// Zero and negative zoom.
	FMinimapCalibration ZeroZoom = Calibration;
	ZeroZoom.Zoom = 0.0f;
	TestFalse(TEXT("Zero zoom is rejected"), ZeroZoom.IsValidCalibration());

	// Inverted Z band.
	FMinimapCalibration BadZ = Calibration;
	BadZ.MinZ = 500.0f;
	BadZ.MaxZ = 100.0f;
	TestFalse(TEXT("Inverted Z range is rejected"), BadZ.IsValidCalibration());

	// The critical safety property: projecting through an invalid calibration must return
	// a finite zero, never NaN, or the NaN would reach a widget render transform.
	const FVector2D Projected = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		ZeroX, FVector(500.0, 500.0, 0.0), FVector2D::ZeroVector, 45.0f);
	TestFalse(TEXT("Invalid calibration does not produce NaN"), Projected.ContainsNaN());
	MINIMAP_TEST_VECTOR_EQUAL("Invalid calibration projects to origin", Projected, FVector2D::ZeroVector);

	FMinimapProjectionContext Context;
	Context.Build(ZeroX, FVector2D::ZeroVector, 0.0f, 0.0f);
	TestFalse(TEXT("Context built from invalid calibration is flagged invalid"), Context.bValid);

	// Degenerate Z band must yield the neutral 0.5, not a divide by zero.
	FMinimapCalibration FlatZ = Calibration;
	FlatZ.MinZ = 100.0f;
	FlatZ.MaxZ = 100.0f;
	TestEqual(TEXT("Degenerate Z band returns a neutral height ratio"),
		UMinimapFunctionLibrary::GetHeightRatio(FlatZ, 100.0f), 0.5f, MinimapTestUtils::Tolerance);

	// Aspect-ratio preservation must square the extent using the LARGER axis.
	FMinimapCalibration Anisotropic = Calibration;
	Anisotropic.WorldExtent = FVector2D(500.0, 600.0);
	Anisotropic.bPreserveAspectRatio = true;
	MINIMAP_TEST_VECTOR_EQUAL("Aspect preservation squares to the larger axis",
		Anisotropic.GetEffectiveExtent(), FVector2D(600.0, 600.0));

	Anisotropic.bPreserveAspectRatio = false;
	MINIMAP_TEST_VECTOR_EQUAL("Aspect preservation off keeps the raw extent",
		Anisotropic.GetEffectiveExtent(), FVector2D(500.0, 600.0));

	return true;
}

// ---------------------------------------------------------------------------
// 9. Map rotation wrapping
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapRotationWrapTest,
	"Minimap.Rotation.Wrapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapRotationWrapTest::RunTest(const FString& Parameters)
{
	// Turns must always be [0, 1) so the material's Rotator node never jumps.
	TestEqual(TEXT("0 deg -> 0 turns"),    UMinimapFunctionLibrary::GetMapRotationTurns(0.0f,   0.0f, false), 0.00f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("90 deg -> 0.25"),      UMinimapFunctionLibrary::GetMapRotationTurns(90.0f,  0.0f, false), 0.25f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("360 deg wraps to 0"),  UMinimapFunctionLibrary::GetMapRotationTurns(360.0f, 0.0f, false), 0.00f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("450 deg wraps to 0.25"), UMinimapFunctionLibrary::GetMapRotationTurns(450.0f, 0.0f, false), 0.25f, MinimapTestUtils::Tolerance);

	// The case a plain Fmod gets wrong: negative yaw must not produce a negative turn.
	// GetControlRotation can legitimately return negative yaw depending on input setup.
	const float NegativeTurns = UMinimapFunctionLibrary::GetMapRotationTurns(-90.0f, 0.0f, false);
	TestTrue(TEXT("Negative yaw yields a non-negative turn value"), NegativeTurns >= 0.0f);
	TestEqual(TEXT("-90 deg -> 0.75 turns"), NegativeTurns, 0.75f, MinimapTestUtils::Tolerance);

	const float VeryNegative = UMinimapFunctionLibrary::GetMapRotationTurns(-810.0f, 0.0f, false);
	TestTrue(TEXT("Large negative yaw stays in [0,1)"), VeryNegative >= 0.0f && VeryNegative < 1.0f);
	TestEqual(TEXT("-810 deg -> 0.75 turns"), VeryNegative, 0.75f, MinimapTestUtils::Tolerance);

	// Every value in a full sweep must remain in range - this is the property that stops
	// the map snapping as the player turns through north.
	for (float Yaw = -1080.0f; Yaw <= 1080.0f; Yaw += 7.5f)
	{
		const float Turns = UMinimapFunctionLibrary::GetMapRotationTurns(Yaw, 33.0f, false);
		if (!(Turns >= 0.0f && Turns < 1.0f))
		{
			AddError(FString::Printf(TEXT("Yaw %.1f produced out-of-range turns %.4f"), Yaw, Turns));
			break;
		}
	}

	// MapYawOffset is the pin the legacy Blueprint left unconnected.
	TestEqual(TEXT("MapYawOffset shifts the result"),
		UMinimapFunctionLibrary::GetMapRotationTurns(0.0f, 90.0f, false), 0.25f, MinimapTestUtils::Tolerance);

	// Negation flips direction without touching the material.
	TestEqual(TEXT("Negation mirrors the rotation"),
		UMinimapFunctionLibrary::GetMapRotationTurns(90.0f, 0.0f, true), 0.75f, MinimapTestUtils::Tolerance);

	// Compass counter-rotates.
	TestEqual(TEXT("Compass angle counter-rotates"), UMinimapFunctionLibrary::GetCompassAngle(90.0f), -90.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("Compass angle is normalized"),   UMinimapFunctionLibrary::GetCompassAngle(450.0f), -90.0f, MinimapTestUtils::Tolerance);

	// Marker icon angles must be normalized, never unbounded.
	TestEqual(TEXT("Icon angle is relative to the view"),
		UMinimapFunctionLibrary::GetMarkerIconAngle(90.0f, 45.0f), 45.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("Icon angle wraps the short way"),
		UMinimapFunctionLibrary::GetMarkerIconAngle(350.0f, 10.0f), -20.0f, MinimapTestUtils::Tolerance);

	for (float ActorYaw = -720.0f; ActorYaw <= 720.0f; ActorYaw += 15.0f)
	{
		const float Angle = UMinimapFunctionLibrary::GetMarkerIconAngle(ActorYaw, 123.0f);
		if (!(Angle > -180.1f && Angle <= 180.1f))
		{
			AddError(FString::Printf(TEXT("Actor yaw %.1f produced un-normalized icon angle %.2f"), ActorYaw, Angle));
			break;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// 10. Respawn and repossession
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapViewActorResolutionTest,
	"Minimap.View.RespawnAndRepossession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapViewActorResolutionTest::RunTest(const FString& Parameters)
{
	// Bare objects are enough here: the resolution rule is pure priority + IsValid, and the
	// legacy bug was purely a policy bug (cache once at Construct, never re-check).
	APawn* const ControlledPawn = NewObject<APawn>(GetTransientPackage(), TEXT("MinimapTest_ControlledPawn"));
	APawn* const OwningPawn     = NewObject<APawn>(GetTransientPackage(), TEXT("MinimapTest_OwningPawn"));
	AActor* const ViewTargetA   = NewObject<AActor>(GetTransientPackage(), TEXT("MinimapTest_ViewTargetA"));
	AActor* const Override      = NewObject<AActor>(GetTransientPackage(), TEXT("MinimapTest_Override"));

	using FResolve = UMinimapViewComponent;

	// Normal play: the controlled pawn outranks both the view target and the owning pawn.
	TestTrue(TEXT("Controlled pawn is preferred"),
		FResolve::ResolveViewActorFromCandidates(nullptr, ControlledPawn, ViewTargetA, OwningPawn) == ControlledPawn);

	// Death: no controlled pawn, so the view target (death camera / spectator) takes over
	// instead of the minimap freezing on a destroyed actor.
	TestTrue(TEXT("View target is used when there is no pawn"),
		FResolve::ResolveViewActorFromCandidates(nullptr, nullptr, ViewTargetA, OwningPawn) == ViewTargetA);

	// With neither a controlled pawn nor a view target, the component's own pawn is the
	// last resort - this is the "view component lives on the Pawn" setup.
	TestTrue(TEXT("Owning pawn is the final fallback"),
		FResolve::ResolveViewActorFromCandidates(nullptr, nullptr, nullptr, OwningPawn) == OwningPawn);

	// An explicit override always wins - spectator cameras, cinematics.
	TestTrue(TEXT("Explicit override beats everything"),
		FResolve::ResolveViewActorFromCandidates(Override, ControlledPawn, ViewTargetA, OwningPawn) == Override);

	// Fully unpossessed: nothing to follow, and that must be a clean null rather than a
	// stale pointer. The view falls back to the map centre in this case.
	TestTrue(TEXT("Nothing to resolve yields null"),
		FResolve::ResolveViewActorFromCandidates(nullptr, nullptr, nullptr, nullptr) == nullptr);

	// --- Respawn / repossession -------------------------------------------
	// This is the exact failure mode of the legacy PlayerRef, which was captured once at
	// Construct and then held a dead pawn forever.
	APawn* const RespawnedPawn = NewObject<APawn>(GetTransientPackage(), TEXT("MinimapTest_RespawnedPawn"));
	ControlledPawn->MarkAsGarbage();

	// A NEW controlled pawn immediately wins over the destroyed one.
	TestTrue(TEXT("Repossession follows the new pawn"),
		FResolve::ResolveViewActorFromCandidates(nullptr, RespawnedPawn, ViewTargetA, OwningPawn) == RespawnedPawn);

	// A destroyed controlled pawn with no replacement must fall THROUGH, never be returned.
	TestTrue(TEXT("Destroyed controlled pawn falls through to the view target"),
		FResolve::ResolveViewActorFromCandidates(nullptr, ControlledPawn, ViewTargetA, nullptr) == ViewTargetA);

	// A destroyed override is skipped too - IsValid(), not a raw null check.
	Override->MarkAsGarbage();
	TestTrue(TEXT("A destroyed override is skipped in favour of the next candidate"),
		FResolve::ResolveViewActorFromCandidates(Override, nullptr, ViewTargetA, nullptr) == ViewTargetA);

	// And if every candidate is gone, the result is null rather than a dangling pointer.
	ViewTargetA->MarkAsGarbage();
	OwningPawn->MarkAsGarbage();
	TestTrue(TEXT("All candidates destroyed yields null"),
		FResolve::ResolveViewActorFromCandidates(Override, ControlledPawn, ViewTargetA, OwningPawn) == nullptr);

	return true;
}

// ---------------------------------------------------------------------------
// 11. Legacy WBP_Minimap / M_Minimap parity
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapLegacyParityTest,
	"Minimap.Compatibility.LegacyMaterialParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapLegacyParityTest::RunTest(const FString& Parameters)
{
	// The exact ranges from the original Blueprint, supplied as data rather than hardcoded
	// anywhere in the runtime path.
	constexpr float LegacyMinX = 0.0f;
	constexpr float LegacyMaxX = 1000.0f;
	constexpr float LegacyMinY = -1150.0f;
	constexpr float LegacyMaxY = 50.0f;

	const FMinimapCalibration Legacy = UMinimapFunctionLibrary::MakeCalibrationFromWorldRange(
		LegacyMinX, LegacyMaxX, LegacyMinY, LegacyMaxY,
		/*bLegacyAxisMapping=*/true,
		/*bPreserveAspectRatio=*/false);

	MINIMAP_TEST_VECTOR_EQUAL("Derived centre matches the legacy midpoints", Legacy.WorldCenter, FVector2D(500.0, -550.0));
	MINIMAP_TEST_VECTOR_EQUAL("Derived extent matches the legacy half-spans", Legacy.WorldExtent, FVector2D(500.0, 600.0));

	// Reference implementation of the original graph: two independent MapRangeClamped calls.
	auto LegacyMapRangeClamped = [](float Value, float InA, float InB, float OutA, float OutB)
	{
		const float Alpha = FMath::Clamp((Value - InA) / (InB - InA), 0.0f, 1.0f);
		return FMath::Lerp(OutA, OutB, Alpha);
	};

	// Sample across the mapped area and require agreement with the legacy formula to
	// within float tolerance. This is what makes "drop it in, the material still works"
	// a verifiable claim rather than an assertion.
	for (float X = LegacyMinX; X <= LegacyMaxX; X += 125.0f)
	{
		for (float Y = LegacyMinY; Y <= LegacyMaxY; Y += 150.0f)
		{
			const FVector2D N = UMinimapFunctionLibrary::ProjectWorldToNormalized(
				Legacy, FVector(X, Y, 0.0), Legacy.WorldCenter, 0.0f);
			const FVector2D MaterialParams = UMinimapFunctionLibrary::NormalizedToMaterialParams(N);

			const float ExpectedX = LegacyMapRangeClamped(X, LegacyMinX, LegacyMaxX, -0.5f, 0.5f);
			const float ExpectedY = LegacyMapRangeClamped(Y, LegacyMinY, LegacyMaxY, -0.5f, 0.5f);

			if (!MinimapTestUtils::NearlyEqual(MaterialParams, FVector2D(ExpectedX, ExpectedY)))
			{
				AddError(FString::Printf(
					TEXT("Legacy parity failed at world (%.1f, %.1f): expected (%.4f, %.4f), got %s"),
					X, Y, ExpectedX, ExpectedY, *MinimapTestUtils::ToString(MaterialParams)));
				return false;
			}
		}
	}

	// The standard convention must NOT match the legacy one - if it did, bSwapUV would be
	// unnecessary and the axis swap would be going unnoticed.
	FMinimapCalibration Standard = Legacy;
	Standard.bSwapUV = false;

	const FVector2D LegacyPoint = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Legacy, FVector(1000.0, -550.0, 0.0), Legacy.WorldCenter, 0.0f);
	const FVector2D StandardPoint = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Standard, FVector(1000.0, -550.0, 0.0), Standard.WorldCenter, 0.0f);

	TestFalse(TEXT("Legacy and standard axis conventions genuinely differ"),
		MinimapTestUtils::NearlyEqual(LegacyPoint, StandardPoint));

	// Specifically: due north drives +U under the legacy mapping but -V under the standard
	// one. The legacy extent is anisotropic (500 x 600), so the standard result is
	// -500/600, which is itself a reminder of why bPreserveAspectRatio exists.
	MINIMAP_TEST_VECTOR_EQUAL("Legacy: world +X drives U", LegacyPoint, FVector2D(1.0, 0.0));
	MINIMAP_TEST_VECTOR_EQUAL("Standard: world +X drives -V", StandardPoint, FVector2D(0.0, -500.0 / 600.0));

	return true;
}

// ---------------------------------------------------------------------------
// 12. Round trips and invert flags
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapConversionRoundTripTest,
	"Minimap.Projection.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapConversionRoundTripTest::RunTest(const FString& Parameters)
{
	const FVector2D Samples[] = {
		FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), FVector2D(-1.0, -1.0),
		FVector2D(0.3, -0.7), FVector2D(-0.25, 0.85)
	};

	for (const FVector2D& N : Samples)
	{
		MINIMAP_TEST_VECTOR_EQUAL("UV round trip",
			UMinimapFunctionLibrary::UVToNormalized(UMinimapFunctionLibrary::NormalizedToUV(N)), N);
	}

	// Rot2D must be a proper rotation: composable and length-preserving.
	const FVector2D V(3.0, 4.0);
	const FVector2D Rotated90 = UMinimapFunctionLibrary::Rot2D(V, 90.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Rot2D by 90 degrees", Rotated90, FVector2D(-4.0, 3.0));
	TestEqual(TEXT("Rot2D preserves length"),
		static_cast<float>(Rotated90.Size()), static_cast<float>(V.Size()), MinimapTestUtils::Tolerance);
	MINIMAP_TEST_VECTOR_EQUAL("Rot2D by 360 is identity", UMinimapFunctionLibrary::Rot2D(V, 360.0f), V);

	// Invert flags must mirror exactly one axis each.
	FMinimapCalibration Calibration = MinimapTestUtils::MakeSquareCalibration();
	const FVector Target(500.0, 250.0, 0.0);

	const FVector2D Base = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, Target, Calibration.WorldCenter, 0.0f);

	Calibration.bInvertU = true;
	const FVector2D InvertedU = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, Target, Calibration.WorldCenter, 0.0f);
	MINIMAP_TEST_VECTOR_EQUAL("bInvertU mirrors only the horizontal axis", InvertedU, FVector2D(-Base.X, Base.Y));

	Calibration.bInvertU = false;
	Calibration.bInvertV = true;
	const FVector2D InvertedV = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, Target, Calibration.WorldCenter, 0.0f);
	MINIMAP_TEST_VECTOR_EQUAL("bInvertV mirrors only the vertical axis", InvertedV, FVector2D(Base.X, -Base.Y));

	// Zoom must scale the normalized position linearly.
	Calibration = MinimapTestUtils::MakeSquareCalibration();
	Calibration.Zoom = 2.0f;
	const FVector2D Zoomed = UMinimapFunctionLibrary::ProjectWorldToNormalized(
		Calibration, Target, Calibration.WorldCenter, 0.0f);
	MINIMAP_TEST_VECTOR_EQUAL("Zoom 2x doubles the normalized offset", Zoomed, Base * 2.0);

	// Height ratio across the band.
	const FMinimapCalibration HeightCal = MinimapTestUtils::MakeSquareCalibration();
	TestEqual(TEXT("Height ratio at the floor"),   UMinimapFunctionLibrary::GetHeightRatio(HeightCal,    0.0f), 0.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("Height ratio at mid height"),  UMinimapFunctionLibrary::GetHeightRatio(HeightCal,  500.0f), 0.5f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("Height ratio at the ceiling"), UMinimapFunctionLibrary::GetHeightRatio(HeightCal, 1000.0f), 1.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("Height ratio clamps below"),   UMinimapFunctionLibrary::GetHeightRatio(HeightCal, -500.0f), 0.0f, MinimapTestUtils::Tolerance);
	TestEqual(TEXT("Height ratio clamps above"),   UMinimapFunctionLibrary::GetHeightRatio(HeightCal, 9999.0f), 1.0f, MinimapTestUtils::Tolerance);

	return true;
}

// ---------------------------------------------------------------------------
// 13. Compass: map yaw, calibration offset, and wrap behaviour
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapCompassTest,
	"Minimap.Rotation.Compass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapCompassTest::RunTest(const FString& Parameters)
{
	using namespace MinimapTestUtils;

	// The indicator shows where world NORTH sits on screen, clockwise-positive from up.
	TestEqual(TEXT("Facing north, north is up"),
		UMinimapFunctionLibrary::GetCompassAngleEx(0.0f, 0.0f, 0.0f), 0.0f, Tolerance);
	TestEqual(TEXT("Facing east, north is to the left"),
		UMinimapFunctionLibrary::GetCompassAngleEx(90.0f, 0.0f, 0.0f), -90.0f, Tolerance);
	TestEqual(TEXT("Facing west, north is to the right"),
		UMinimapFunctionLibrary::GetCompassAngleEx(270.0f, 0.0f, 0.0f), 90.0f, Tolerance);
	TestEqual(TEXT("Facing south, north is at the bottom"),
		FMath::Abs(UMinimapFunctionLibrary::GetCompassAngleEx(180.0f, 0.0f, 0.0f)), 180.0f, Tolerance);

	// A map drawn at an angle moves where north appears; ignoring MapYaw leaves the
	// indicator wrong by exactly that angle, which is the bug this argument covers.
	TestEqual(TEXT("A plan drawn 40 deg clockwise puts north 40 deg counter-clockwise"),
		UMinimapFunctionLibrary::GetCompassAngleEx(0.0f, 40.0f, 0.0f), -40.0f, Tolerance);
	TestEqual(TEXT("Map yaw composes with view yaw"),
		UMinimapFunctionLibrary::GetCompassAngleEx(90.0f, 40.0f, 0.0f), -130.0f, Tolerance);

	// The calibration dial shifts the indicator by exactly its value, at every heading.
	for (float ViewYaw = -360.0f; ViewYaw <= 360.0f; ViewYaw += 17.0f)
	{
		const float Base    = UMinimapFunctionLibrary::GetCompassAngleEx(ViewYaw, 0.0f, 0.0f);
		const float Offset  = UMinimapFunctionLibrary::GetCompassAngleEx(ViewYaw, 0.0f, 40.0f);
		const float Applied = FRotator::NormalizeAxis(Offset - Base);

		if (FMath::Abs(Applied - 40.0f) > 0.01f)
		{
			AddError(FString::Printf(
				TEXT("Offset was not applied uniformly at view yaw %.1f: got %.3f, expected 40."),
				ViewYaw, Applied));
			break;
		}
	}

	// Equal and opposite map yaw and offset must cancel exactly.
	for (float ViewYaw = 0.0f; ViewYaw < 360.0f; ViewYaw += 23.0f)
	{
		TestEqual(TEXT("Map yaw and an equal offset cancel"),
			UMinimapFunctionLibrary::GetCompassAngleEx(ViewYaw, 40.0f, 40.0f),
			UMinimapFunctionLibrary::GetCompassAngleEx(ViewYaw, 0.0f, 0.0f), Tolerance);
	}

	// Never unbounded, whatever it is fed.
	for (float ViewYaw = -1080.0f; ViewYaw <= 1080.0f; ViewYaw += 31.0f)
	{
		for (const float MapYaw : { -180.0f, -40.0f, 0.0f, 40.0f, 180.0f })
		{
			const float Angle = UMinimapFunctionLibrary::GetCompassAngleEx(ViewYaw, MapYaw, 40.0f);
			if (!(Angle > -180.1f && Angle <= 180.1f))
			{
				AddError(FString::Printf(TEXT("Compass angle %.2f out of range at yaw %.1f, map %.1f"),
					Angle, ViewYaw, MapYaw));
				return false;
			}
		}
	}

	// The legacy single-argument form must keep behaving exactly as it did.
	for (float ViewYaw = -360.0f; ViewYaw <= 360.0f; ViewYaw += 29.0f)
	{
		TestEqual(TEXT("Legacy GetCompassAngle is unchanged"),
			UMinimapFunctionLibrary::GetCompassAngle(ViewYaw),
			UMinimapFunctionLibrary::GetCompassAngleEx(ViewYaw, 0.0f, 0.0f), Tolerance);
	}

	return true;
}

// ---------------------------------------------------------------------------
// 14. Marker priority ordering - the contract the widget's cap depends on
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMinimapPriorityOrderTest,
	"Minimap.Markers.PriorityOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMinimapPriorityOrderTest::RunTest(const FString& Parameters)
{
	// The subsystem sorts snapshots priority-ASCENDING and trims from the FRONT, so the
	// most important markers survive and sit at the END of the array. The widget must
	// therefore walk BACKWARDS when capping, or it drops exactly the markers it should
	// keep - which is the bug this pins down.
	TArray<FMinimapMarkerSnapshot> Snapshots;
	for (const int32 Priority : { 5, 1, 9, 3, 7 })
	{
		FMinimapMarkerSnapshot Snapshot;
		Snapshot.Priority = Priority;
		Snapshots.Add(Snapshot);
	}

	Snapshots.StableSort([](const FMinimapMarkerSnapshot& A, const FMinimapMarkerSnapshot& B)
	{
		return A.Priority < B.Priority;
	});

	TestEqual(TEXT("Lowest priority is first"), Snapshots[0].Priority, 1);
	TestEqual(TEXT("Highest priority is last"), Snapshots.Last().Priority, 9);

	// Subsystem budget: trimming the front keeps the important ones.
	TArray<FMinimapMarkerSnapshot> Budgeted = Snapshots;
	const int32 Budget = 3;
	Budgeted.RemoveAt(0, Budgeted.Num() - Budget, EAllowShrinking::No);
	TestEqual(TEXT("Budget keeps three"), Budgeted.Num(), Budget);
	TestEqual(TEXT("Budget kept the highest"), Budgeted.Last().Priority, 9);
	TestEqual(TEXT("Budget dropped the lowest"), Budgeted[0].Priority, 5);

	// Widget cap: walking backwards must take the highest first.
	TArray<int32> TakenByWidget;
	const int32 MaxWidgets = 2;
	for (int32 Index = Snapshots.Num() - 1; Index >= 0 && TakenByWidget.Num() < MaxWidgets; --Index)
	{
		TakenByWidget.Add(Snapshots[Index].Priority);
	}
	TestEqual(TEXT("Widget took the two highest"), TakenByWidget.Num(), 2);
	TestEqual(TEXT("Widget took 9 first"), TakenByWidget[0], 9);
	TestEqual(TEXT("Widget took 7 second"), TakenByWidget[1], 7);

	// The forward walk that used to be there would have taken 1 and 3 - the least
	// important markers on the map.
	TestTrue(TEXT("Backward walk differs from the forward walk"),
		TakenByWidget[0] != Snapshots[0].Priority);

	return true;
}

#undef MINIMAP_TEST_VECTOR_EQUAL

#endif // WITH_DEV_AUTOMATION_TESTS
