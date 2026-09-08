// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningEasing.h"

#include "Curves/CurveFloat.h"
#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "ArchOpenings"

float FArchOpeningEasing::EvaluateBuiltIn(EArchOpeningEasing Type, float T)
{
	const float X = FMath::Clamp(T, 0.0f, 1.0f);

	switch (Type)
	{
	case EArchOpeningEasing::Linear:
		return X;

	case EArchOpeningEasing::EaseIn:
		return X * X;

	case EArchOpeningEasing::EaseOut:
		return 1.0f - (1.0f - X) * (1.0f - X);

	case EArchOpeningEasing::EaseInOut:
		// Cubic in/out. Continuous, f(0)=0, f(1)=1, monotonic.
		return (X < 0.5f)
			? 4.0f * X * X * X
			: 1.0f - FMath::Pow(-2.0f * X + 2.0f, 3.0f) * 0.5f;

	case EArchOpeningEasing::SmoothStep:
	case EArchOpeningEasing::Custom:	// Custom falls back here when no valid curve is available.
	default:
		return X * X * (3.0f - 2.0f * X);
	}
}

float FArchOpeningEasing::Evaluate(EArchOpeningEasing Type, const UCurveFloat* Curve, bool bCurveIsValid, float T)
{
	const float X = FMath::Clamp(T, 0.0f, 1.0f);

	if (Type == EArchOpeningEasing::Custom && bCurveIsValid && Curve != nullptr)
	{
		// Clamped so a curve that overshoots between its endpoints still cannot drive the leaf
		// outside the configured range.
		return FMath::Clamp(Curve->GetFloatValue(X), 0.0f, 1.0f);
	}

	return EvaluateBuiltIn(Type, X);
}

float FArchOpeningEasing::InverseEvaluate(EArchOpeningEasing Type, const UCurveFloat* Curve, bool bCurveIsValid, float Value)
{
	const float Target = FMath::Clamp(Value, 0.0f, 1.0f);

	if (Target <= 0.0f)
	{
		return 0.0f;
	}
	if (Target >= 1.0f)
	{
		return 1.0f;
	}

	// Linear inverts exactly; skip the search.
	if (Type == EArchOpeningEasing::Linear)
	{
		return Target;
	}

	float Low = 0.0f;
	float High = 1.0f;
	for (int32 Iteration = 0; Iteration < InversionIterations; ++Iteration)
	{
		const float Mid = (Low + High) * 0.5f;
		if (Evaluate(Type, Curve, bCurveIsValid, Mid) < Target)
		{
			Low = Mid;
		}
		else
		{
			High = Mid;
		}
	}

	return (Low + High) * 0.5f;
}

bool FArchOpeningEasing::ValidateCurve(const UCurveFloat* Curve, FText& OutReason)
{
	if (Curve == nullptr)
	{
		OutReason = LOCTEXT("CurveNull", "No easing curve is assigned.");
		return false;
	}

	const float Start = Curve->GetFloatValue(0.0f);
	const float End = Curve->GetFloatValue(1.0f);

	if (!FMath::IsNearlyEqual(Start, 0.0f, EndpointTolerance))
	{
		OutReason = FText::Format(
			LOCTEXT("CurveStartFmt", "Easing curve must evaluate to 0 at time 0 (it evaluates to {0})."),
			FText::AsNumber(Start));
		return false;
	}

	if (!FMath::IsNearlyEqual(End, 1.0f, EndpointTolerance))
	{
		OutReason = FText::Format(
			LOCTEXT("CurveEndFmt", "Easing curve must evaluate to 1 at time 1 (it evaluates to {0})."),
			FText::AsNumber(End));
		return false;
	}

	float Previous = Start;
	for (int32 Index = 1; Index < CurveValidationSamples; ++Index)
	{
		const float T = static_cast<float>(Index) / static_cast<float>(CurveValidationSamples - 1);
		const float Sample = Curve->GetFloatValue(T);

		if (Sample < Previous - MonotonicTolerance)
		{
			OutReason = FText::Format(
				LOCTEXT("CurveMonotonicFmt", "Easing curve must be non-decreasing; it decreases near time {0}."),
				FText::AsNumber(T));
			return false;
		}

		if (Sample < -EndpointTolerance || Sample > 1.0f + EndpointTolerance)
		{
			OutReason = FText::Format(
				LOCTEXT("CurveRangeFmt", "Easing curve must stay within 0..1; it reaches {0} near time {1}."),
				FText::AsNumber(Sample), FText::AsNumber(T));
			return false;
		}

		Previous = Sample;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
