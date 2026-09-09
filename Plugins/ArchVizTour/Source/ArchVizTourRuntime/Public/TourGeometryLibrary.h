// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TourTypes.h"

#include "TourGeometryLibrary.generated.h"

class AActor;

/**
 * Distance -> spline-key lookup table for an FTourPathData.
 *
 * USplineComponent owns an equivalent table, but it only exists once a component has been
 * registered in a world. Tours are generated, resampled, measured and unit-tested long
 * before that, so the same arc-length parameterisation is implemented here against the raw
 * data. Both paths use identical Hermite evaluation, so a path measured here and a path
 * traversed through the component agree to within the sampling tolerance.
 */
struct ARCHVIZTOURRUNTIME_API FTourArcLengthTable
{
	/**
	 * Build the table by adaptively sampling every segment.
	 * @param PathData    Curve to measure.
	 * @param SubSteps    MINIMUM samples per segment. Segments longer than the internal target
	 *                    sample spacing are subdivided further, so accuracy does not depend on
	 *                    how long any one segment happens to be.
	 */
	void Build(const FTourPathData& PathData, int32 SubSteps = 16);

	/** Total arc length in centimetres, or 0 for a degenerate path. */
	float GetTotalLength() const { return TotalLength; }

	/** True when the table describes a traversable curve. */
	bool IsValid() const { return Keys.Num() >= 2 && TotalLength > UE_KINDA_SMALL_NUMBER; }

	/**
	 * Convert a distance along the curve into a spline input key.
	 * @param Distance  Distance from the start, in centimetres. Clamped to [0, TotalLength].
	 * @return Fractional spline key (point index plus segment fraction).
	 */
	float GetKeyAtDistance(float Distance) const;

	/**
	 * Convert a spline input key into a distance along the curve.
	 * @param Key  Fractional spline key. Clamped to the table's key range.
	 * @return Distance from the start, in centimetres.
	 */
	float GetDistanceAtKey(float Key) const;

private:
	/** Monotonically increasing spline keys, one per sample. */
	TArray<float> Keys;

	/** Cumulative arc length at each sample, in centimetres. Same length as Keys. */
	TArray<float> Distances;

	float TotalLength = 0.0f;
};

/**
 * Pure static path mathematics. No editor dependencies, no world access, no UObject state:
 * every entry point is a function of its arguments alone, which is what makes the arc
 * accuracy and constant-speed properties directly unit-testable.
 */
UCLASS()
class ARCHVIZTOURRUNTIME_API UTourGeometryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------
	// Curve evaluation
	// ---------------------------------------------------------------------

	/**
	 * Evaluate the Hermite curve described by PathData at a fractional spline key.
	 * @param PathData  Curve to sample. A single-point path returns that point's location.
	 * @param Key       Fractional spline key; clamped for open paths, wrapped for closed ones.
	 * @return Local-space position in centimetres.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Geometry")
	static FVector EvaluatePositionAtKey(const FTourPathData& PathData, float Key);

	/**
	 * Evaluate the curve's first derivative at a fractional spline key.
	 * @return Local-space tangent in centimetres per unit key. Zero for degenerate input.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Geometry")
	static FVector EvaluateTangentAtKey(const FTourPathData& PathData, float Key);

	/**
	 * Arc-length parameterised sample: constant steps in Distance produce constant steps in
	 * space regardless of how unevenly the points are spaced.
	 *
	 * Rebuilds the arc-length table on every call, so it is a convenience entry point rather
	 * than a hot-path one. Code that samples repeatedly should keep an FTourArcLengthTable, and
	 * playback goes through USplineComponent's own table (see ATourPath::EvaluateAtDistance).
	 *
	 * @param PathData  Curve to sample.
	 * @param Distance  Distance from the start, in centimetres.
	 * @return Local-space position in centimetres.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Geometry")
	static FVector EvaluatePositionAtDistance(const FTourPathData& PathData, float Distance);

	/**
	 * Total length of the curve.
	 * @param PathData  Curve to measure.
	 * @param SubSteps  Samples per segment; higher is more accurate and linearly slower.
	 * @return Arc length in centimetres.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Geometry", meta = (AdvancedDisplay = "SubSteps"))
	static float ComputeArcLength(const FTourPathData& PathData, int32 SubSteps = 16);

	// ---------------------------------------------------------------------
	// Generators
	// ---------------------------------------------------------------------

	/**
	 * Circular arc whose tangents use the exact cubic-Bezier circular approximation.
	 *
	 * Auto-tangents deviate from a true circle by a fraction of a percent of the radius,
	 * which is invisible on a fast cut but reads as a distinct wobble on the slow pans
	 * ArchViz work is made of, so the tangents are computed analytically instead.
	 *
	 * @param Center          Arc centre, in centimetres.
	 * @param Radius          Arc radius, in centimetres. Must be > 0.
	 * @param StartAngleDeg   Angle of the first point about UpAxis, in degrees.
	 * @param SweepAngleDeg   Signed sweep, in degrees. Negative sweeps run clockwise.
	 * @param PointCount      Number of generated points; clamped to >= 2.
	 * @param UpAxis          Rotation axis. A degenerate axis falls back to world +Z.
	 * @param HeightDelta     Total rise along UpAxis across the sweep, in centimetres.
	 * @param Transform       Applied to every generated point, so arcs respect actor orientation.
	 * @return Generated path. Empty Points on invalid input, with a warning logged.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData GenerateArc(
		FVector Center,
		float Radius,
		float StartAngleDeg,
		float SweepAngleDeg,
		int32 PointCount,
		FVector UpAxis = FVector(0.0, 0.0, 1.0),
		float HeightDelta = 0.0f,
		const FTransform& Transform = FTransform::Identity);

	/**
	 * Helix: an arc of Turns full revolutions rising by HeightDelta in total.
	 * @param Turns  Revolutions; fractional values are allowed. Must be > 0.
	 * @see GenerateArc for the shared parameters.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData GenerateHelix(
		FVector Center,
		float Radius,
		float StartAngleDeg,
		float Turns,
		float HeightDelta,
		int32 PointCount,
		FVector UpAxis = FVector(0.0, 0.0, 1.0),
		const FTransform& Transform = FTransform::Identity);

	/**
	 * Orbit centred on an actor's origin, with every point aimed back at that origin.
	 * @param Target       Actor to orbit. Null returns an empty path with a warning.
	 * @param Radius       Orbit radius, in centimetres.
	 * @param Height       Height above the actor's origin, in centimetres.
	 * @param SweepDeg     Signed sweep, in degrees.
	 * @param PointCount   Number of generated points; clamped to >= 2.
	 * @param StartAngleDeg Angle of the first point, in degrees.
	 * @return Generated path in world space, with bUseExplicitRotation set on every point.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData GenerateOrbitAround(
		const AActor* Target,
		float Radius,
		float Height,
		float SweepDeg,
		int32 PointCount,
		float StartAngleDeg = 0.0f);

	/**
	 * Straight dolly between two points with collinear tangents, so the move has no curvature.
	 * @param A           Start, in centimetres.
	 * @param B           End, in centimetres.
	 * @param PointCount  Number of generated points; clamped to >= 2.
	 * @param Transform   Applied to every generated point.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData GenerateDollyLine(
		FVector A,
		FVector B,
		int32 PointCount = 2,
		const FTransform& Transform = FTransform::Identity);

	/**
	 * Rebuild a path from an existing FTourArcGenerationParams block.
	 * Lets the details panel regenerate in place after a radius or point-count tweak.
	 * @param Params     Generator parameters.
	 * @param Transform  Applied to every generated point.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData GenerateFromParams(const FTourArcGenerationParams& Params, const FTransform& Transform);

	// ---------------------------------------------------------------------
	// Operators
	// ---------------------------------------------------------------------

	/**
	 * Resample a path so its points are equally spaced in arc length.
	 * Camera attributes (focal length, aperture, speed, eases) are linearly interpolated from
	 * the source points; per-point labels and dwell times are not carried over, because a
	 * resampled point rarely coincides with an authored one.
	 * @param PathData   Source curve.
	 * @param SpacingCm  Desired spacing, in centimetres. Must be > 0.
	 * @return Resampled path, or a copy of the input when it is not traversable.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData ResampleUniform(const FTourPathData& PathData, float SpacingCm);

	/**
	 * Replace every tangent with a Catmull-Rom estimate, blended against the existing tangents.
	 * @param PathData  Curve modified in place.
	 * @param Strength  0 leaves the tangents untouched, 1 replaces them outright.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static void SmoothTangents(UPARAM(ref) FTourPathData& PathData, float Strength = 1.0f);

	/** Mirror every point across a plane through PlaneOrigin with normal PlaneNormal. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData MirrorPath(const FTourPathData& PathData, FVector PlaneOrigin, FVector PlaneNormal);

	/** Reverse traversal order, swapping and negating tangents so the curve shape is preserved. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData ReversePath(const FTourPathData& PathData);

	/** Set every point's Z to Height (centimetres), leaving X and Y and all camera data alone. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Geometry")
	static FTourPathData FlattenToHeight(const FTourPathData& PathData, float Height);

	// ---------------------------------------------------------------------
	// Easing
	// ---------------------------------------------------------------------

	/**
	 * CSS-style cubic-Bezier easing with control points (EaseIn, 0) and (1 - EaseOut, 1).
	 *
	 * The same curve shape is emitted as Sequencer tangent weights by UTourSequenceBuilder,
	 * so procedural playback and a baked ULevelSequence accelerate identically.
	 *
	 * @param Alpha    Normalised time, 0..1. Clamped.
	 * @param EaseIn   0..1 damping at the start.
	 * @param EaseOut  0..1 damping at the end.
	 * @return Eased alpha, 0..1, monotonically increasing in Alpha.
	 */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Geometry")
	static float EvaluateEase(float Alpha, float EaseIn, float EaseOut);

	/**
	 * Bezier control-handle length for one segment of a circular arc.
	 * @param Radius            Circle radius, in centimetres.
	 * @param SegmentAngleRad   Angle subtended by the segment, in radians.
	 * @return Handle length in centimetres: Radius * (4/3) * tan(SegmentAngle / 4).
	 */
	static float ComputeArcBezierHandleLength(float Radius, float SegmentAngleRad);
};
