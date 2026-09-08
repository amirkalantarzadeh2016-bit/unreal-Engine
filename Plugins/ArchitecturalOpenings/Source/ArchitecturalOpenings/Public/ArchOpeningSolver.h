// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchOpeningTypes.h"

/**
 * Pure transform maths for the plugin. No UObject state, so it is directly unit-testable.
 *
 * Coordinate model
 * ----------------
 * All motion is authored in the opening's *calibration frame*: the opening component's world
 * rotation and location, with a uniform scale factor applied only when the component's world scale
 * is uniform and positive.
 *
 * For every driven part the plugin stores one rest transform relative to that frame:
 *
 *     RestRelative = PartRestWorld.GetRelativeTransform(CalibrationFrame)
 *
 * and every animated pose is recomputed from it:
 *
 *     PartWorld = RestRelative * MotionDelta * CalibrationFrame
 *
 * where MotionDelta is a calibration-space transform derived only from the openness alpha. Because
 * poses are recomputed from the stored rest rather than accumulated from the previous frame, the
 * setup cannot drift over repeated cycles, cannot pick up Euler wrapping artefacts (quaternions and
 * axis-angle throughout), and returns to an exactly identical closed pose every time.
 *
 * Handles compose one extra delta before the leaf delta:
 *
 *     HandleWorld = RestRelative * HandleDelta * LeafDelta * CalibrationFrame
 *
 * which is what makes a handle inherit leaf motion while still animating locally.
 */
struct ARCHITECTURALOPENINGS_API FArchOpeningSolver
{
	/** Smallest length a user-supplied direction vector may have before it is treated as invalid. */
	static constexpr float MinDirectionLength = 1.0e-3f;

	/** Relative tolerance used to decide whether a world scale counts as uniform. */
	static constexpr float UniformScaleTolerance = 1.0e-3f;

	/** Result of inspecting a component's world scale for calibration suitability. */
	struct FScaleAnalysis
	{
		bool bUniform = true;
		bool bNegative = false;
		float UniformScale = 1.0f;
		FVector RawScale = FVector::OneVector;
	};

	static FScaleAnalysis AnalyzeScale(const FVector& WorldScale);

	/**
	 * Builds the calibration frame from a component's world transform.
	 * Non-uniform or negative scale is dropped to 1.0; callers surface that as a warning.
	 */
	static FTransform MakeCalibrationFrame(const FTransform& ComponentWorld, FScaleAnalysis& OutAnalysis);

	/** Observer's left when standing on the outside looking in: Up x Outside. */
	static FVector ComputeLeftDirection(const FVector& OutsideDirection, const FVector& UpDirection);

	/**
	 * Resolves the signed hinge axis in calibration space from the artist-facing presets.
	 * Returns the zero vector when the inputs are degenerate (parallel outside/up, zero custom axis).
	 */
	static FVector ComputeHingeAxis(const FArchOpeningHingedSettings& Hinged, const FArchOpeningCalibration& Calibration);

	/** Default slide direction used when the user has not authored one: the observer's left. */
	static FVector ComputeDefaultSlideDirection(const FArchOpeningCalibration& Calibration);

	/** Resolved, normalised and sign-applied slide direction. Zero vector when degenerate. */
	static FVector ComputeSlideDirection(const FArchOpeningSlidingSettings& Sliding, const FArchOpeningCalibration& Calibration);

	/** Calibration-space rotation of Angle degrees about Axis through PivotLocation. */
	static FTransform MakeRotationDelta(const FVector& PivotLocation, const FVector& Axis, float AngleDegrees);

	/** Calibration-space translation delta. */
	static FTransform MakeTranslationDelta(const FVector& Direction, float Distance);

	/** The leaf's calibration-space delta at a given geometric openness. */
	static FTransform ComputeLeafDelta(
		EArchOpeningMotionType MotionType,
		const FArchOpeningHingedSettings& Hinged,
		const FArchOpeningSlidingSettings& Sliding,
		const FArchOpeningCalibration& Calibration,
		float Openness);

	/** One handle group's own calibration-space delta at a given actuation alpha. */
	static FTransform ComputeHandleDelta(const FArchOpeningHandleGroup& Group, float Alpha);

	/** RestRelative * Delta * CalibrationFrame. */
	static FTransform ComposeWorld(const FTransform& RestRelative, const FTransform& Delta, const FTransform& CalibrationFrame);

	/** PartWorld.GetRelativeTransform(CalibrationFrame). */
	static FTransform CaptureRestRelative(const FTransform& PartWorld, const FTransform& CalibrationFrame);

	/**
	 * Full travel of the leaf's free edge, used to turn Speed mode into a duration.
	 * Hinged: the open angle in degrees. Sliding: the travel distance in centimetres.
	 */
	static float ComputeNominalTravel(
		EArchOpeningMotionType MotionType,
		const FArchOpeningHingedSettings& Hinged,
		const FArchOpeningSlidingSettings& Sliding);
};
