// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchOpeningTypes.h"

class UCurveFloat;

/**
 * Easing evaluation and inversion.
 *
 * Every easing function here is a normalised map [0,1] -> [0,1] with f(0) = 0, f(1) = 1, and is
 * non-decreasing. The endpoint half of that contract is what guarantees a transition lands exactly
 * on its target openness; the monotonic half is what makes InverseEvaluate well defined.
 *
 * Custom curves are validated against the same contract before use (see ValidateCurve). A curve
 * that fails is rejected and Smooth Step is substituted; together with the clamp inside Evaluate,
 * that is what stops an authored curve from driving the leaf outside its configured range.
 *
 * Note on reversal: the runtime state machine does NOT invert the easing to reverse. It re-anchors
 * the transition on the current pose and scales its duration by the remaining travel, which is
 * both simpler and better behaved (see UArchOpeningComponent::BeginTransition). InverseEvaluate
 * exists for tooling that has to go the other way - turning a scrubbed pose back into curve
 * progress - which is what the editor preview scrub does.
 */
struct ARCHITECTURALOPENINGS_API FArchOpeningEasing
{
	/** Number of samples used for curve validation and for numeric inversion of custom curves. */
	static constexpr int32 CurveValidationSamples = 33;

	/** Bisection steps used by InverseEvaluate. 24 gives ~6e-8 resolution on [0,1]. */
	static constexpr int32 InversionIterations = 24;

	/** Endpoint tolerance a custom curve must satisfy at t=0 and t=1. */
	static constexpr float EndpointTolerance = 1.0e-3f;

	/** Allowed backwards step between consecutive samples before a curve is called non-monotonic. */
	static constexpr float MonotonicTolerance = 1.0e-4f;

	/** Evaluates a built-in easing type. Input and output are clamped to [0,1]. */
	static float EvaluateBuiltIn(EArchOpeningEasing Type, float T);

	/**
	 * Evaluates the easing for a transition. Curve is only consulted for EArchOpeningEasing::Custom,
	 * and only when bCurveIsValid is true; otherwise Smooth Step is used.
	 */
	static float Evaluate(EArchOpeningEasing Type, const UCurveFloat* Curve, bool bCurveIsValid, float T);

	/**
	 * Numerically inverts Evaluate: returns the T in [0,1] whose eased value is closest to Value.
	 * Relies on the monotonicity contract above.
	 */
	static float InverseEvaluate(EArchOpeningEasing Type, const UCurveFloat* Curve, bool bCurveIsValid, float Value);

	/**
	 * Checks that a curve satisfies the endpoint and monotonicity contract.
	 * @param OutReason  Filled with a human readable reason when the curve is rejected.
	 */
	static bool ValidateCurve(const UCurveFloat* Curve, FText& OutReason);
};
