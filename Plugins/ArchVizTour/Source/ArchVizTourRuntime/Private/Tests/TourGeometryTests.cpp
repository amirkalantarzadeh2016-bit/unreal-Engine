// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "TourGeometryLibrary.h"
#include "TourTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ArchVizTourTests
{
	/** Radius used by the arc tests, in centimetres. Chosen to be ArchViz-scale. */
	static constexpr float TestRadius = 500.0f;

	/**
	 * Accuracy bar for the circular Bezier approximation: the sampled curve must stay within
	 * 0.1% of the radius of the analytic circle. The worst case the plugin allows - a single
	 * 90 degree segment - measures about 0.027%, so this leaves real headroom without being
	 * so loose that a regression to auto-tangents (several percent) would slip through.
	 */
	static constexpr float ArcDeviationTolerance = 0.001f;

	/** Samples taken per segment when measuring arc deviation. */
	static constexpr int32 ArcSampleCount = 200;

	/** Measure the worst radial deviation of a generated arc from a perfect circle, in centimetres. */
	static float MeasureArcDeviation(const FTourPathData& PathData, const FVector& Center, float Radius)
	{
		const int32 SegmentCount = PathData.GetSegmentCount();
		if (SegmentCount <= 0)
		{
			return TNumericLimits<float>::Max();
		}

		float WorstDeviation = 0.0f;
		for (int32 Sample = 0; Sample <= SegmentCount * ArcSampleCount; ++Sample)
		{
			const float Key = static_cast<float>(SegmentCount) * static_cast<float>(Sample)
				/ static_cast<float>(SegmentCount * ArcSampleCount);

			const FVector Position = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, Key);
			const FVector Radial = Position - Center;

			// The arcs under test lie in the XY plane, so the radius is the planar distance.
			const float SampledRadius = static_cast<float>(FVector2D(Radial.X, Radial.Y).Size());
			WorstDeviation = FMath::Max(WorstDeviation, FMath::Abs(SampledRadius - Radius));
		}

		return WorstDeviation;
	}

	/** A path whose points are deliberately unevenly spaced along a straight line. */
	static FTourPathData MakeUnevenStraightPath()
	{
		FTourPathData PathData;

		const float XPositions[] = { 0.0f, 100.0f, 1200.0f, 1400.0f };
		for (const float X : XPositions)
		{
			FTourPoint Point;
			Point.Location  = FVector(X, 0.0, 0.0);
			// Linear points make the curve exactly the straight line through them, which gives
			// the constant-speed test an analytic answer to compare against.
			Point.PointType = ESplinePointType::Linear;
			PathData.Points.Add(Point);
		}

		return PathData;
	}
}

// ---------------------------------------------------------------------------
// Arc tangent accuracy
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourArcTangentAccuracyTest,
	"ArchVizTour.Geometry.ArcTangentsMatchAnalyticCircle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourArcTangentAccuracyTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourTests;

	struct FArcCase
	{
		const TCHAR* Description;
		float SweepDegrees;
		int32 PointCount;
	};

	// The last case is the worst the generator allows: one 90 degree Bezier segment.
	const FArcCase Cases[] =
	{
		{ TEXT("quarter arc, 5 points"),      90.0f,  5 },
		{ TEXT("quarter arc, 3 points"),      90.0f,  3 },
		{ TEXT("half arc, 4 points"),        180.0f,  4 },
		{ TEXT("full circle, 9 points"),     360.0f,  9 },
		{ TEXT("clockwise quarter arc"),     -90.0f,  5 },
		{ TEXT("quarter arc, 2 points"),      90.0f,  2 },
	};

	const FVector Center(1000.0, -250.0, 75.0);

	for (const FArcCase& Case : Cases)
	{
		const FTourPathData PathData = UTourGeometryLibrary::GenerateArc(
			Center, TestRadius, /*StartAngleDeg*/ 30.0f, Case.SweepDegrees, Case.PointCount,
			FVector::UpVector, /*HeightDelta*/ 0.0f, FTransform::Identity);

		if (PathData.Points.Num() < 2)
		{
			AddError(FString::Printf(TEXT("%s: the generator produced %d points."), Case.Description, PathData.Points.Num()));
			continue;
		}

		const float Deviation = MeasureArcDeviation(PathData, Center, TestRadius);
		const float RelativeDeviation = Deviation / TestRadius;

		if (RelativeDeviation > ArcDeviationTolerance)
		{
			AddError(FString::Printf(
				TEXT("%s: worst radial deviation %.6f cm is %.5f%% of the radius, above the %.5f%% tolerance."),
				Case.Description, Deviation, RelativeDeviation * 100.0f, ArcDeviationTolerance * 100.0f));
		}
	}

	// The Bezier handle length is the one number the whole approximation rests on; assert it
	// directly so a refactor cannot quietly drop the factor of 4/3 or the quarter angle.
	const float QuarterTurnHandle = UTourGeometryLibrary::ComputeArcBezierHandleLength(1.0f, UE_PI * 0.5f);
	TestEqual(TEXT("Bezier handle length for a quarter circle of unit radius"), QuarterTurnHandle, 0.5522847f, 1.e-4f);

	return true;
}

// ---------------------------------------------------------------------------
// Arc-length parameterisation
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourArcLengthConstantSpeedTest,
	"ArchVizTour.Geometry.ArcLengthParameterisationIsConstantSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourArcLengthConstantSpeedTest::RunTest(const FString& Parameters)
{
	using namespace ArchVizTourTests;

	// Straight line with deliberately uneven point spacing (0, 100, 1200, 1400 cm). Sampling it
	// by spline key would move at wildly different speeds in each segment; sampling it by arc
	// length must not.
	const FTourPathData PathData = MakeUnevenStraightPath();

	FTourArcLengthTable Table;
	Table.Build(PathData);

	if (!Table.IsValid())
	{
		AddError(TEXT("The arc-length table reported itself invalid for a four-point straight line."));
		return false;
	}

	TestEqual(TEXT("Measured length of the 1400 cm straight line"), Table.GetTotalLength(), 1400.0f, 0.5f);

	constexpr int32 StepCount = 200;
	const float NominalStep = Table.GetTotalLength() / static_cast<float>(StepCount);

	FVector PreviousPosition = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, Table.GetKeyAtDistance(0.0f));
	float WorstRelativeError = 0.0f;

	for (int32 Index = 1; Index <= StepCount; ++Index)
	{
		const float Distance = NominalStep * static_cast<float>(Index);
		const FVector Position = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, Table.GetKeyAtDistance(Distance));

		const float ActualStep = static_cast<float>(FVector::Dist(PreviousPosition, Position));
		PreviousPosition = Position;

		WorstRelativeError = FMath::Max(WorstRelativeError, FMath::Abs(ActualStep - NominalStep) / NominalStep);
	}

	// On a straight line the parameterisation is exact, so anything above rounding noise means
	// the distance table is not doing its job.
	if (WorstRelativeError > 0.01f)
	{
		AddError(FString::Printf(
			TEXT("Uniform distance steps produced spatial steps varying by %.4f%%; the path is not arc-length parameterised."),
			WorstRelativeError * 100.0f));
	}

	// Same property on a curve, where the table is a numerical approximation rather than exact.
	const FTourPathData Arc = UTourGeometryLibrary::GenerateArc(
		FVector::ZeroVector, TestRadius, 0.0f, 270.0f, 7, FVector::UpVector, 0.0f, FTransform::Identity);

	FTourArcLengthTable ArcTable;
	ArcTable.Build(Arc);

	const float ExpectedArcLength = TestRadius * FMath::DegreesToRadians(270.0f);
	TestEqual(TEXT("Measured length of a 270 degree arc"), ArcTable.GetTotalLength(), ExpectedArcLength, ExpectedArcLength * 0.001f);

	const float ArcNominalStep = ArcTable.GetTotalLength() / static_cast<float>(StepCount);
	FVector PreviousArcPosition = UTourGeometryLibrary::EvaluatePositionAtKey(Arc, ArcTable.GetKeyAtDistance(0.0f));
	float WorstArcError = 0.0f;

	for (int32 Index = 1; Index <= StepCount; ++Index)
	{
		const FVector Position = UTourGeometryLibrary::EvaluatePositionAtKey(
			Arc, ArcTable.GetKeyAtDistance(ArcNominalStep * static_cast<float>(Index)));

		const float ActualStep = static_cast<float>(FVector::Dist(PreviousArcPosition, Position));
		PreviousArcPosition = Position;

		WorstArcError = FMath::Max(WorstArcError, FMath::Abs(ActualStep - ArcNominalStep) / ArcNominalStep);
	}

	if (WorstArcError > 0.02f)
	{
		AddError(FString::Printf(
			TEXT("Uniform distance steps along an arc varied by %.4f%%, above the 2%% sampling tolerance."),
			WorstArcError * 100.0f));
	}

	return true;
}

// ---------------------------------------------------------------------------
// Degenerate input
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourGeometryDegenerateInputTest,
	"ArchVizTour.Geometry.DegenerateInputIsHandled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourGeometryDegenerateInputTest::RunTest(const FString& Parameters)
{
	// Every one of these is an authoring mistake that must degrade rather than crash. Each logs
	// a warning through LogArchVizTour; warnings do not fail an automation test, and asserting
	// on log text would couple the test to the wording of the messages.
	const FTourPathData ZeroRadius = UTourGeometryLibrary::GenerateArc(
		FVector::ZeroVector, 0.0f, 0.0f, 90.0f, 5, FVector::UpVector, 0.0f, FTransform::Identity);
	TestEqual(TEXT("A zero-radius arc produces no points"), ZeroRadius.Points.Num(), 0);

	const FTourPathData ZeroLengthDolly = UTourGeometryLibrary::GenerateDollyLine(
		FVector::ZeroVector, FVector::ZeroVector, 4, FTransform::Identity);
	TestEqual(TEXT("A zero-length dolly produces no points"), ZeroLengthDolly.Points.Num(), 0);

	const FTourPathData NullOrbit = UTourGeometryLibrary::GenerateOrbitAround(nullptr, 500.0f, 100.0f, 180.0f, 5);
	TestEqual(TEXT("An orbit around nothing produces no points"), NullOrbit.Points.Num(), 0);

	// A single-point path has no segments, so every query has to answer rather than divide by zero.
	FTourPathData SinglePoint;
	FTourPoint Only;
	Only.Location = FVector(10.0, 20.0, 30.0);
	SinglePoint.Points.Add(Only);

	TestEqual(TEXT("A single-point path has zero length"), UTourGeometryLibrary::ComputeArcLength(SinglePoint), 0.0f);
	TestEqual(TEXT("A single-point path evaluates to that point"),
		UTourGeometryLibrary::EvaluatePositionAtKey(SinglePoint, 3.7f), Only.Location);
	TestFalse(TEXT("A single-point path is not traversable"), SinglePoint.IsTraversable());

	const FTourPathData Resampled = UTourGeometryLibrary::ResampleUniform(SinglePoint, 50.0f);
	TestEqual(TEXT("Resampling a single-point path returns it unchanged"), Resampled.Points.Num(), 1);

	// An empty path must survive every entry point.
	const FTourPathData Empty;
	TestEqual(TEXT("An empty path has zero length"), UTourGeometryLibrary::ComputeArcLength(Empty), 0.0f);
	TestEqual(TEXT("An empty path evaluates to the origin"),
		UTourGeometryLibrary::EvaluatePositionAtKey(Empty, 1.0f), FVector::ZeroVector);

	return true;
}

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArchVizTourEasingTest,
	"ArchVizTour.Geometry.EasingIsMonotonicAndBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArchVizTourEasingTest::RunTest(const FString& Parameters)
{
	// With no easing the curve must be the identity, or an unearsed step would retime itself.
	for (int32 Index = 0; Index <= 10; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / 10.0f;
		TestEqual(TEXT("Unweighted easing is the identity"), UTourGeometryLibrary::EvaluateEase(Alpha, 0.0f, 0.0f), Alpha, 1.e-3f);
	}

	const float EaseWeights[][2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.4f, 0.7f } };

	for (const float* Weights : EaseWeights)
	{
		TestEqual(TEXT("Easing starts at 0"), UTourGeometryLibrary::EvaluateEase(0.0f, Weights[0], Weights[1]), 0.0f, 1.e-3f);
		TestEqual(TEXT("Easing ends at 1"), UTourGeometryLibrary::EvaluateEase(1.0f, Weights[0], Weights[1]), 1.0f, 1.e-3f);

		// Monotonicity is what guarantees the camera never reverses inside a step.
		float Previous = 0.0f;
		for (int32 Index = 0; Index <= 100; ++Index)
		{
			const float Value = UTourGeometryLibrary::EvaluateEase(static_cast<float>(Index) / 100.0f, Weights[0], Weights[1]);

			if (Value < Previous - 1.e-4f)
			{
				AddError(FString::Printf(
					TEXT("Easing with weights (%.2f, %.2f) went backwards at alpha %.2f: %.6f after %.6f."),
					Weights[0], Weights[1], static_cast<float>(Index) / 100.0f, Value, Previous));
				break;
			}

			TestTrue(TEXT("Easing stays within 0..1"), Value >= -1.e-4f && Value <= 1.0f + 1.e-4f);
			Previous = Value;
		}
	}

	// Out-of-range input is clamped rather than extrapolated.
	TestEqual(TEXT("Negative alpha clamps to 0"), UTourGeometryLibrary::EvaluateEase(-2.0f, 0.5f, 0.5f), 0.0f, 1.e-3f);
	TestEqual(TEXT("Alpha above 1 clamps to 1"), UTourGeometryLibrary::EvaluateEase(4.0f, 0.5f, 0.5f), 1.0f, 1.e-3f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
