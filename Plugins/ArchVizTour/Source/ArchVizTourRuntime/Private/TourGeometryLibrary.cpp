// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourGeometryLibrary.h"

#include "Algo/BinarySearch.h"
#include "Algo/Reverse.h"
#include "ArchVizTourLog.h"
#include "GameFramework/Actor.h"

namespace ArchVizTour::Geometry
{
	/** Floor on samples per segment for the arc-length table. */
	static constexpr int32 MinSubSteps = 2;

	/** Ceiling on samples per segment, so a pathological curve cannot blow the table up. */
	static constexpr int32 MaxSubStepsPerSegment = 512;

	/** Target spacing between arc-length table samples, in centimetres. */
	static constexpr float TargetSampleSpacingCm = 2.0f;

	/** Chords used for the cheap per-segment length estimate that drives the sample count. */
	static constexpr int32 CoarseEstimateSteps = 8;

	/** Iterations used to invert the easing Bezier. Newton converges to float precision well inside this. */
	static constexpr int32 EaseNewtonIterations = 8;

	/** Cubic Hermite basis evaluated for a single segment parameter T in [0, 1]. */
	static FVector Hermite(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;

		const float H00 =  2.0f * T3 - 3.0f * T2 + 1.0f;
		const float H10 =         T3 - 2.0f * T2 + T;
		const float H01 = -2.0f * T3 + 3.0f * T2;
		const float H11 =         T3 -        T2;

		return (P0 * H00) + (T0 * H10) + (P1 * H01) + (T1 * H11);
	}

	/** First derivative of Hermite() with respect to T. */
	static FVector HermiteDerivative(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, float T)
	{
		const float T2 = T * T;

		const float D00 =  6.0f * T2 - 6.0f * T;
		const float D10 =  3.0f * T2 - 4.0f * T + 1.0f;
		const float D01 = -6.0f * T2 + 6.0f * T;
		const float D11 =  3.0f * T2 - 2.0f * T;

		return (P0 * D00) + (T0 * D10) + (P1 * D01) + (T1 * D11);
	}

	/**
	 * Split a fractional spline key into a segment index and a 0..1 parameter.
	 * Closed loops wrap; open paths clamp, so sampling past either end is well defined.
	 * Returns false when the path has no segments at all.
	 */
	static bool ResolveKey(const FTourPathData& PathData, float Key, int32& OutSegment, float& OutT)
	{
		const int32 SegmentCount = PathData.GetSegmentCount();
		if (SegmentCount <= 0)
		{
			OutSegment = 0;
			OutT = 0.0f;
			return false;
		}

		if (PathData.bClosedLoop)
		{
			Key = FMath::Fmod(Key, static_cast<float>(SegmentCount));
			if (Key < 0.0f)
			{
				Key += static_cast<float>(SegmentCount);
			}
		}
		else
		{
			Key = FMath::Clamp(Key, 0.0f, static_cast<float>(SegmentCount));
		}

		OutSegment = FMath::Clamp(FMath::FloorToInt(Key), 0, SegmentCount - 1);
		OutT = FMath::Clamp(Key - static_cast<float>(OutSegment), 0.0f, 1.0f);
		return true;
	}

	/** Index of the point that closes the segment starting at SegmentIndex. */
	static int32 NextPointIndex(const FTourPathData& PathData, int32 SegmentIndex)
	{
		const int32 PointCount = PathData.Points.Num();
		check(PointCount > 0);
		return (SegmentIndex + 1) % PointCount;
	}

	/**
	 * A Linear point behaves as if its tangents ran straight to its neighbours; Constant holds
	 * the previous value. Baking that here keeps every consumer - evaluation, arc length,
	 * resampling - agreeing on the same curve.
	 */
	static void GetSegmentControlPoints(
		const FTourPathData& PathData,
		int32 SegmentIndex,
		FVector& OutP0,
		FVector& OutT0,
		FVector& OutP1,
		FVector& OutT1)
	{
		const int32 IndexA = SegmentIndex;
		const int32 IndexB = NextPointIndex(PathData, SegmentIndex);

		const FTourPoint& A = PathData.Points[IndexA];
		const FTourPoint& B = PathData.Points[IndexB];

		OutP0 = A.Location;
		OutP1 = B.Location;
		OutT0 = A.LeaveTangent;
		OutT1 = B.ArriveTangent;

		const FVector Chord = B.Location - A.Location;

		if (A.PointType == ESplinePointType::Linear || B.PointType == ESplinePointType::Linear)
		{
			OutT0 = Chord;
			OutT1 = Chord;
		}

		if (A.PointType == ESplinePointType::Constant)
		{
			OutP1 = A.Location;
			OutT0 = FVector::ZeroVector;
			OutT1 = FVector::ZeroVector;
		}
	}

	/** Sample a cubic Bezier defined by four control points. Used only by the easing solver. */
	static float BezierScalar(float P0, float P1, float P2, float P3, float T)
	{
		const float U = 1.0f - T;
		return (U * U * U * P0)
			+ (3.0f * U * U * T * P1)
			+ (3.0f * U * T * T * P2)
			+ (T * T * T * P3);
	}

	static float BezierScalarDerivative(float P0, float P1, float P2, float P3, float T)
	{
		const float U = 1.0f - T;
		return (3.0f * U * U * (P1 - P0))
			+ (6.0f * U * T * (P2 - P1))
			+ (3.0f * T * T * (P3 - P2));
	}

	/** Reflect V across a plane through the origin with unit normal N. */
	static FVector MirrorVector(const FVector& V, const FVector& UnitNormal)
	{
		return V - 2.0 * FVector::DotProduct(V, UnitNormal) * UnitNormal;
	}

	/** Linearly interpolate every camera attribute of a point; positional data is left to the caller. */
	static void LerpCameraAttributes(const FTourPoint& A, const FTourPoint& B, float Alpha, FTourPoint& Out)
	{
		Out.FocalLength         = FMath::Lerp(A.FocalLength, B.FocalLength, Alpha);
		Out.Aperture            = FMath::Lerp(A.Aperture, B.Aperture, Alpha);
		Out.ManualFocusDistance = FMath::Lerp(A.ManualFocusDistance, B.ManualFocusDistance, Alpha);
		Out.Speed               = FMath::Lerp(A.Speed, B.Speed, Alpha);
		Out.EaseIn              = FMath::Lerp(A.EaseIn, B.EaseIn, Alpha);
		Out.EaseOut             = FMath::Lerp(A.EaseOut, B.EaseOut, Alpha);

		// Discrete attributes snap to the nearer source point rather than being invented.
		const FTourPoint& Nearest = (Alpha < 0.5f) ? A : B;
		Out.bUseExplicitRotation = Nearest.bUseExplicitRotation;
		Out.Rotation             = FQuat::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), Alpha).Rotator();
		Out.bUseLookAtTarget     = Nearest.bUseLookAtTarget;
		Out.LookAtTargetName     = Nearest.LookAtTargetName;
		Out.PointType            = ESplinePointType::CurveCustomTangent;
	}
}

// ---------------------------------------------------------------------------
// FTourArcLengthTable
// ---------------------------------------------------------------------------

void FTourArcLengthTable::Build(const FTourPathData& PathData, int32 SubSteps)
{
	using namespace ArchVizTour::Geometry;

	Keys.Reset();
	Distances.Reset();
	TotalLength = 0.0f;

	const int32 SegmentCount = PathData.GetSegmentCount();
	if (SegmentCount <= 0)
	{
		return;
	}

	SubSteps = FMath::Clamp(SubSteps, MinSubSteps, MaxSubStepsPerSegment);

	Keys.Add(0.0f);
	Distances.Add(0.0f);

	FVector PreviousPosition = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, 0.0f);

	for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
	{
		// A fixed sample count per segment is the usual mistake here: it makes the table's
		// accuracy depend on how long each segment happens to be, so a path with one 20 cm hop
		// and one 20 m sweep is parameterised well in the first and badly in the second. Size
		// each segment's sample count from a cheap chord-length estimate instead, so sample
		// spacing - and therefore the linear interpolation error between table entries - stays
		// roughly constant along the whole curve.
		float CoarseLength = 0.0f;
		{
			FVector CoarsePrevious = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, static_cast<float>(Segment));
			for (int32 Step = 1; Step <= CoarseEstimateSteps; ++Step)
			{
				const float Key = static_cast<float>(Segment) + static_cast<float>(Step) / static_cast<float>(CoarseEstimateSteps);
				const FVector Position = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, Key);
				CoarseLength += static_cast<float>(FVector::Dist(CoarsePrevious, Position));
				CoarsePrevious = Position;
			}
		}

		const int32 DesiredSteps = FMath::CeilToInt(CoarseLength / TargetSampleSpacingCm);
		const int32 SegmentSteps = FMath::Clamp(DesiredSteps, SubSteps, MaxSubStepsPerSegment);

		Keys.Reserve(Keys.Num() + SegmentSteps);
		Distances.Reserve(Distances.Num() + SegmentSteps);

		for (int32 Step = 1; Step <= SegmentSteps; ++Step)
		{
			const float LocalT = static_cast<float>(Step) / static_cast<float>(SegmentSteps);
			const float Key = static_cast<float>(Segment) + LocalT;

			const FVector Position = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, Key);
			TotalLength += static_cast<float>(FVector::Dist(PreviousPosition, Position));
			PreviousPosition = Position;

			Keys.Add(Key);
			Distances.Add(TotalLength);
		}
	}
}

float FTourArcLengthTable::GetKeyAtDistance(float Distance) const
{
	if (!IsValid())
	{
		return 0.0f;
	}

	Distance = FMath::Clamp(Distance, 0.0f, TotalLength);

	// Distances is strictly non-decreasing, so a binary search finds the bracketing samples.
	const int32 UpperIndex = FMath::Clamp(Algo::UpperBound(Distances, Distance), 1, Distances.Num() - 1);
	const int32 LowerIndex = UpperIndex - 1;

	const float SegmentLength = Distances[UpperIndex] - Distances[LowerIndex];
	const float LocalAlpha = (SegmentLength > UE_KINDA_SMALL_NUMBER)
		? (Distance - Distances[LowerIndex]) / SegmentLength
		: 0.0f;

	return FMath::Lerp(Keys[LowerIndex], Keys[UpperIndex], LocalAlpha);
}

float FTourArcLengthTable::GetDistanceAtKey(float Key) const
{
	if (!IsValid())
	{
		return 0.0f;
	}

	Key = FMath::Clamp(Key, Keys[0], Keys.Last());

	const int32 UpperIndex = FMath::Clamp(Algo::UpperBound(Keys, Key), 1, Keys.Num() - 1);
	const int32 LowerIndex = UpperIndex - 1;

	const float KeySpan = Keys[UpperIndex] - Keys[LowerIndex];
	const float LocalAlpha = (KeySpan > UE_KINDA_SMALL_NUMBER)
		? (Key - Keys[LowerIndex]) / KeySpan
		: 0.0f;

	return FMath::Lerp(Distances[LowerIndex], Distances[UpperIndex], LocalAlpha);
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

FVector UTourGeometryLibrary::EvaluatePositionAtKey(const FTourPathData& PathData, float Key)
{
	using namespace ArchVizTour::Geometry;

	if (PathData.Points.Num() == 0)
	{
		return FVector::ZeroVector;
	}

	int32 Segment = 0;
	float LocalT = 0.0f;
	if (!ResolveKey(PathData, Key, Segment, LocalT))
	{
		// Single-point path: the only defined position is that point.
		return PathData.Points[0].Location;
	}

	FVector P0, T0, P1, T1;
	GetSegmentControlPoints(PathData, Segment, P0, T0, P1, T1);
	return Hermite(P0, T0, P1, T1, LocalT);
}

FVector UTourGeometryLibrary::EvaluateTangentAtKey(const FTourPathData& PathData, float Key)
{
	using namespace ArchVizTour::Geometry;

	int32 Segment = 0;
	float LocalT = 0.0f;
	if (!ResolveKey(PathData, Key, Segment, LocalT))
	{
		return FVector::ZeroVector;
	}

	FVector P0, T0, P1, T1;
	GetSegmentControlPoints(PathData, Segment, P0, T0, P1, T1);
	return HermiteDerivative(P0, T0, P1, T1, LocalT);
}

FVector UTourGeometryLibrary::EvaluatePositionAtDistance(const FTourPathData& PathData, float Distance)
{
	FTourArcLengthTable Table;
	Table.Build(PathData);

	if (!Table.IsValid())
	{
		return PathData.Points.Num() > 0 ? PathData.Points[0].Location : FVector::ZeroVector;
	}

	return EvaluatePositionAtKey(PathData, Table.GetKeyAtDistance(Distance));
}

float UTourGeometryLibrary::ComputeArcLength(const FTourPathData& PathData, int32 SubSteps)
{
	FTourArcLengthTable Table;
	Table.Build(PathData, SubSteps);
	return Table.GetTotalLength();
}

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

float UTourGeometryLibrary::ComputeArcBezierHandleLength(float Radius, float SegmentAngleRad)
{
	return Radius * (4.0f / 3.0f) * FMath::Tan(SegmentAngleRad * 0.25f);
}

FTourPathData UTourGeometryLibrary::GenerateArc(
	FVector Center,
	float Radius,
	float StartAngleDeg,
	float SweepAngleDeg,
	int32 PointCount,
	FVector UpAxis,
	float HeightDelta,
	const FTransform& Transform)
{
	FTourPathData Result;

	if (Radius <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("GenerateArc: radius %.3f is not positive; returning an empty path."), Radius);
		return Result;
	}

	PointCount = FMath::Max(PointCount, 2);

	FVector Axis = UpAxis;
	if (!Axis.Normalize())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("GenerateArc: degenerate up axis; falling back to world +Z."));
		Axis = FVector::UpVector;
	}

	// Any unit vector perpendicular to Axis serves as the 0-degree reference. FindBestAxisVectors
	// picks a numerically stable one instead of risking a near-parallel cross product; the second
	// reference is then derived so that (RefX, RefY, Axis) is right-handed, which is what makes a
	// positive SweepAngleDeg counter-clockwise about Axis.
	FVector RefX, Unused;
	Axis.FindBestAxisVectors(RefX, Unused);
	RefX = RefX.GetSafeNormal();
	const FVector RefY = FVector::CrossProduct(Axis, RefX).GetSafeNormal();

	const float SweepRad = FMath::DegreesToRadians(SweepAngleDeg);
	const float StartRad = FMath::DegreesToRadians(StartAngleDeg);

	const int32 SegmentCount = PointCount - 1;
	const float SegmentAngle = SweepRad / static_cast<float>(SegmentCount);

	// A segment spanning half a turn or more makes tan(theta/4) diverge and the cubic
	// approximation meaningless; refuse rather than emit a path that looks plausible but isn't.
	if (FMath::Abs(SegmentAngle) >= UE_PI)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("GenerateArc: %d points over %.1f degrees gives a %.1f degree segment; the circular Bezier approximation needs segments below 180 degrees. Returning an empty path."),
			PointCount, SweepAngleDeg, FMath::RadiansToDegrees(FMath::Abs(SegmentAngle)));
		return Result;
	}

	// Bezier handle length for the circular approximation. USplineComponent stores *Hermite*
	// tangents, and for a unit-parameter segment the Hermite tangent is exactly three times the
	// Bezier control-point offset (B1 = P0 + T0/3). Storing the handle length directly would
	// flatten the arc by a third, which is the classic version of this bug. tan() is odd, so a
	// negative (clockwise) sweep produces a correctly reversed tangent with no extra sign work.
	const float TangentLength = 3.0f * ComputeArcBezierHandleLength(Radius, SegmentAngle);

	Result.Points.Reserve(PointCount);

	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(SegmentCount);
		const float Angle = StartRad + SweepRad * Alpha;

		const FVector Radial  = (RefX * FMath::Cos(Angle)) + (RefY * FMath::Sin(Angle));
		const FVector Forward = (RefX * -FMath::Sin(Angle)) + (RefY * FMath::Cos(Angle));

		const FVector LocalPosition = Center + Radial * Radius + Axis * (HeightDelta * Alpha);

		// The helix rise is linear in the sweep parameter, so it contributes a constant
		// component to the tangent; ignoring it makes the ends of a helix visibly kink.
		const FVector AxialTangent = Axis * (HeightDelta / static_cast<float>(SegmentCount));
		const FVector LocalTangent = Forward * TangentLength + AxialTangent;

		FTourPoint Point;
		Point.Location      = Transform.TransformPosition(LocalPosition);
		Point.ArriveTangent = Transform.TransformVector(LocalTangent);
		Point.LeaveTangent  = Point.ArriveTangent;
		Point.PointType     = ESplinePointType::CurveCustomTangent;
		Result.Points.Add(Point);
	}

	// A full revolution generated as an open arc leaves a duplicate point at the seam; closing
	// the loop instead and dropping the duplicate gives a continuous, seamless orbit.
	if (FMath::IsNearlyEqual(FMath::Abs(SweepAngleDeg), 360.0f, 0.01f)
		&& FMath::IsNearlyZero(HeightDelta)
		&& Result.Points.Num() > 2)
	{
		Result.Points.Pop();
		Result.bClosedLoop = true;
	}

	Result.ArcGenerationParams.GeneratorType  = ETourGeneratorType::Arc;
	Result.ArcGenerationParams.Center         = Center;
	Result.ArcGenerationParams.Radius         = Radius;
	Result.ArcGenerationParams.StartAngleDeg  = StartAngleDeg;
	Result.ArcGenerationParams.SweepAngleDeg  = SweepAngleDeg;
	Result.ArcGenerationParams.PointCount     = PointCount;
	Result.ArcGenerationParams.UpAxis         = UpAxis;
	Result.ArcGenerationParams.HeightDelta    = HeightDelta;
	Result.SplineWorldTransform               = Transform;

	return Result;
}

FTourPathData UTourGeometryLibrary::GenerateHelix(
	FVector Center,
	float Radius,
	float StartAngleDeg,
	float Turns,
	float HeightDelta,
	int32 PointCount,
	FVector UpAxis,
	const FTransform& Transform)
{
	if (Turns <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("GenerateHelix: turn count %.3f is not positive; returning an empty path."), Turns);
		return FTourPathData();
	}

	// A helix is an arc of Turns revolutions. Enough points to keep each segment well under a
	// half-turn, otherwise the circular Bezier approximation degrades sharply.
	const int32 MinPointsForAccuracy = FMath::CeilToInt(Turns * 4.0f) + 1;
	PointCount = FMath::Max3(PointCount, MinPointsForAccuracy, 2);

	FTourPathData Result = GenerateArc(
		Center, Radius, StartAngleDeg, Turns * 360.0f, PointCount, UpAxis, HeightDelta, Transform);

	Result.ArcGenerationParams.GeneratorType = ETourGeneratorType::Helix;
	Result.ArcGenerationParams.HelixTurns    = Turns;
	return Result;
}

FTourPathData UTourGeometryLibrary::GenerateOrbitAround(
	const AActor* Target,
	float Radius,
	float Height,
	float SweepDeg,
	int32 PointCount,
	float StartAngleDeg)
{
	if (Target == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("GenerateOrbitAround: null target actor; returning an empty path."));
		return FTourPathData();
	}

	const FVector TargetOrigin = Target->GetActorLocation();
	const FVector OrbitCenter = TargetOrigin + FVector(0.0, 0.0, Height);

	FTourPathData Result = GenerateArc(
		OrbitCenter, Radius, StartAngleDeg, SweepDeg, PointCount, FVector::UpVector, 0.0f, FTransform::Identity);

	// Aim every point back at the actor's origin rather than at the orbit plane's centre, so a
	// raised orbit looks down at the building instead of staring at empty air beside it.
	for (FTourPoint& Point : Result.Points)
	{
		Point.bUseExplicitRotation = true;
		Point.Rotation = (TargetOrigin - Point.Location).Rotation();
	}

	Result.ArcGenerationParams.GeneratorType   = ETourGeneratorType::Orbit;
	Result.ArcGenerationParams.OrbitTargetName = Target->GetFName();
	Result.ArcGenerationParams.bFaceCenter     = true;
	return Result;
}

FTourPathData UTourGeometryLibrary::GenerateDollyLine(FVector A, FVector B, int32 PointCount, const FTransform& Transform)
{
	FTourPathData Result;

	PointCount = FMath::Max(PointCount, 2);

	const FVector Delta = B - A;
	if (Delta.IsNearlyZero())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("GenerateDollyLine: start and end coincide; returning an empty path."));
		return Result;
	}

	const int32 SegmentCount = PointCount - 1;
	// Tangent equal to one segment's chord makes the Hermite curve exactly the straight line.
	const FVector SegmentChord = Delta / static_cast<float>(SegmentCount);

	Result.Points.Reserve(PointCount);
	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(SegmentCount);

		FTourPoint Point;
		Point.Location      = Transform.TransformPosition(A + Delta * Alpha);
		Point.ArriveTangent = Transform.TransformVector(SegmentChord);
		Point.LeaveTangent  = Point.ArriveTangent;
		Point.PointType     = ESplinePointType::CurveCustomTangent;
		Result.Points.Add(Point);
	}

	Result.ArcGenerationParams.GeneratorType = ETourGeneratorType::DollyLine;
	Result.ArcGenerationParams.DollyStart    = A;
	Result.ArcGenerationParams.DollyEnd      = B;
	Result.ArcGenerationParams.PointCount    = PointCount;
	Result.SplineWorldTransform              = Transform;
	return Result;
}

FTourPathData UTourGeometryLibrary::GenerateFromParams(const FTourArcGenerationParams& Params, const FTransform& Transform)
{
	switch (Params.GeneratorType)
	{
	case ETourGeneratorType::Arc:
		return GenerateArc(
			Params.Center, Params.Radius, Params.StartAngleDeg, Params.SweepAngleDeg,
			Params.PointCount, Params.UpAxis, Params.HeightDelta, Transform);

	case ETourGeneratorType::Helix:
		return GenerateHelix(
			Params.Center, Params.Radius, Params.StartAngleDeg, Params.HelixTurns,
			Params.HeightDelta, Params.PointCount, Params.UpAxis, Transform);

	case ETourGeneratorType::Orbit:
	{
		// The orbit target is a level actor, which this pure-math path cannot resolve. Fall back
		// to an arc about the stored centre; ATourPath::GenerateArcRail re-aims it when it can
		// actually find the actor.
		FTourPathData Result = GenerateArc(
			Params.Center, Params.Radius, Params.StartAngleDeg, Params.SweepAngleDeg,
			Params.PointCount, Params.UpAxis, Params.HeightDelta, Transform);

		if (Params.bFaceCenter)
		{
			const FVector WorldCenter = Transform.TransformPosition(Params.Center);
			for (FTourPoint& Point : Result.Points)
			{
				Point.bUseExplicitRotation = true;
				Point.Rotation = (WorldCenter - Point.Location).Rotation();
			}
		}

		Result.ArcGenerationParams = Params;
		return Result;
	}

	case ETourGeneratorType::DollyLine:
		return GenerateDollyLine(Params.DollyStart, Params.DollyEnd, Params.PointCount, Transform);

	case ETourGeneratorType::None:
	default:
		UE_LOG(LogArchVizTour, Verbose, TEXT("GenerateFromParams: path is hand authored; nothing to regenerate."));
		return FTourPathData();
	}
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------

FTourPathData UTourGeometryLibrary::ResampleUniform(const FTourPathData& PathData, float SpacingCm)
{
	using namespace ArchVizTour::Geometry;

	if (!PathData.IsTraversable() || SpacingCm <= UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ResampleUniform: path is not traversable or spacing %.3f cm is invalid; returning the input unchanged."), SpacingCm);
		return PathData;
	}

	FTourArcLengthTable Table;
	Table.Build(PathData);
	if (!Table.IsValid())
	{
		return PathData;
	}

	const float TotalLength = Table.GetTotalLength();
	const int32 SegmentCount = FMath::Max(1, FMath::RoundToInt(TotalLength / SpacingCm));
	const int32 NewPointCount = SegmentCount + 1;

	FTourPathData Result;
	Result.bClosedLoop          = PathData.bClosedLoop;
	Result.DefaultSpeed         = PathData.DefaultSpeed;
	Result.ArcGenerationParams  = PathData.ArcGenerationParams;
	Result.SplineWorldTransform = PathData.SplineWorldTransform;
	Result.Points.Reserve(NewPointCount);

	// Uniform spacing means the actual step is TotalLength / SegmentCount, not SpacingCm; using
	// SpacingCm directly would leave a short ragged final segment.
	const float ActualSpacing = TotalLength / static_cast<float>(SegmentCount);

	for (int32 Index = 0; Index < NewPointCount; ++Index)
	{
		const float Distance = FMath::Min(ActualSpacing * static_cast<float>(Index), TotalLength);
		const float Key = Table.GetKeyAtDistance(Distance);

		FTourPoint Point;
		Point.Location = EvaluatePositionAtKey(PathData, Key);

		// Re-express the tangent in the *new* parameterisation. Because the resample is uniform in
		// arc length, ds/dKey is exactly ActualSpacing for every new segment, so the Hermite
		// tangent is the unit tangent scaled by that spacing. Reusing the source derivative
		// directly would leave the curve parameterised for the old, uneven point spacing.
		const FVector UnitTangent = EvaluateTangentAtKey(PathData, Key).GetSafeNormal();
		Point.ArriveTangent = UnitTangent * ActualSpacing;
		Point.LeaveTangent  = Point.ArriveTangent;

		int32 SourceSegment = 0;
		float SourceT = 0.0f;
		if (ResolveKey(PathData, Key, SourceSegment, SourceT))
		{
			const FTourPoint& A = PathData.Points[SourceSegment];
			const FTourPoint& B = PathData.Points[NextPointIndex(PathData, SourceSegment)];
			LerpCameraAttributes(A, B, SourceT, Point);
		}

		Result.Points.Add(Point);
	}

	if (Result.bClosedLoop && Result.Points.Num() > 1)
	{
		// The wrap-around point duplicates the first one on a closed loop.
		Result.Points.Pop();
	}

	return Result;
}

void UTourGeometryLibrary::SmoothTangents(FTourPathData& PathData, float Strength)
{
	const int32 PointCount = PathData.Points.Num();
	if (PointCount < 2)
	{
		return;
	}

	Strength = FMath::Clamp(Strength, 0.0f, 1.0f);
	if (FMath::IsNearlyZero(Strength))
	{
		return;
	}

	TArray<FVector> NewTangents;
	NewTangents.SetNumUninitialized(PointCount);

	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		const bool bHasPrev = PathData.bClosedLoop || Index > 0;
		const bool bHasNext = PathData.bClosedLoop || Index < PointCount - 1;

		const int32 PrevIndex = (Index - 1 + PointCount) % PointCount;
		const int32 NextIndex = (Index + 1) % PointCount;

		const FVector& Current = PathData.Points[Index].Location;
		const FVector& Prev = bHasPrev ? PathData.Points[PrevIndex].Location : Current;
		const FVector& Next = bHasNext ? PathData.Points[NextIndex].Location : Current;

		FVector Catmull;
		if (bHasPrev && bHasNext)
		{
			// Catmull-Rom: half the chord between the neighbours.
			Catmull = (Next - Prev) * 0.5f;
		}
		else if (bHasNext)
		{
			Catmull = Next - Current;
		}
		else
		{
			Catmull = Current - Prev;
		}

		NewTangents[Index] = Catmull;
	}

	for (int32 Index = 0; Index < PointCount; ++Index)
	{
		FTourPoint& Point = PathData.Points[Index];
		Point.ArriveTangent = FMath::Lerp(Point.ArriveTangent, NewTangents[Index], Strength);
		Point.LeaveTangent  = FMath::Lerp(Point.LeaveTangent, NewTangents[Index], Strength);
		Point.PointType     = ESplinePointType::CurveCustomTangent;
	}
}

FTourPathData UTourGeometryLibrary::MirrorPath(const FTourPathData& PathData, FVector PlaneOrigin, FVector PlaneNormal)
{
	using namespace ArchVizTour::Geometry;

	FTourPathData Result = PathData;

	FVector Normal = PlaneNormal;
	if (!Normal.Normalize())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("MirrorPath: degenerate plane normal; returning the input unchanged."));
		return Result;
	}

	for (FTourPoint& Point : Result.Points)
	{
		Point.Location      = MirrorVector(Point.Location - PlaneOrigin, Normal) + PlaneOrigin;
		Point.ArriveTangent = MirrorVector(Point.ArriveTangent, Normal);
		Point.LeaveTangent  = MirrorVector(Point.LeaveTangent, Normal);

		if (Point.bUseExplicitRotation)
		{
			// Mirroring a basis flips handedness, so the rotation has to be rebuilt from mirrored
			// forward and up axes; mirroring the Euler angles component-wise does not work.
			const FVector Forward = MirrorVector(Point.Rotation.Vector(), Normal);
			const FVector Up      = MirrorVector(FRotationMatrix(Point.Rotation).GetUnitAxis(EAxis::Z), Normal);
			Point.Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
		}
	}

	// Mirroring flips handedness, so traversal order has to flip too or the path runs backwards.
	Result = ReversePath(Result);

	Result.ArcGenerationParams.GeneratorType = ETourGeneratorType::None;
	return Result;
}

FTourPathData UTourGeometryLibrary::ReversePath(const FTourPathData& PathData)
{
	FTourPathData Result = PathData;

	Algo::Reverse(Result.Points);

	for (FTourPoint& Point : Result.Points)
	{
		// Reversing swaps which side each tangent faces and inverts its direction.
		const FVector OldArrive = Point.ArriveTangent;
		Point.ArriveTangent = -Point.LeaveTangent;
		Point.LeaveTangent  = -OldArrive;
	}

	return Result;
}

FTourPathData UTourGeometryLibrary::FlattenToHeight(const FTourPathData& PathData, float Height)
{
	FTourPathData Result = PathData;

	for (FTourPoint& Point : Result.Points)
	{
		Point.Location.Z = Height;
		// Removing the vertical component keeps the curve inside the plane instead of bulging
		// out of it between points.
		Point.ArriveTangent.Z = 0.0;
		Point.LeaveTangent.Z  = 0.0;
	}

	return Result;
}

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------

float UTourGeometryLibrary::EvaluateEase(float Alpha, float EaseIn, float EaseOut)
{
	using namespace ArchVizTour::Geometry;

	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	EaseIn = FMath::Clamp(EaseIn, 0.0f, 1.0f);
	EaseOut = FMath::Clamp(EaseOut, 0.0f, 1.0f);

	if (FMath::IsNearlyZero(EaseIn) && FMath::IsNearlyZero(EaseOut))
	{
		return Alpha;
	}

	const float X1 = EaseIn;
	const float X2 = 1.0f - EaseOut;

	// Invert x(t) = Alpha by Newton-Raphson. x is monotonic for control points inside [0, 1],
	// so a single seeded root is guaranteed; the derivative only vanishes at the endpoints,
	// which the guard below handles by bisecting instead.
	float T = Alpha;
	for (int32 Iteration = 0; Iteration < EaseNewtonIterations; ++Iteration)
	{
		const float X = BezierScalar(0.0f, X1, X2, 1.0f, T) - Alpha;
		if (FMath::Abs(X) < UE_SMALL_NUMBER)
		{
			break;
		}

		const float DX = BezierScalarDerivative(0.0f, X1, X2, 1.0f, T);
		if (FMath::Abs(DX) < UE_SMALL_NUMBER)
		{
			break;
		}

		T = FMath::Clamp(T - X / DX, 0.0f, 1.0f);
	}

	return FMath::Clamp(BezierScalar(0.0f, 0.0f, 1.0f, 1.0f, T), 0.0f, 1.0f);
}
