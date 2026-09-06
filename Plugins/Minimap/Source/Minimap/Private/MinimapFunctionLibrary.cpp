#include "MinimapFunctionLibrary.h"

#include "MinimapModule.h"

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

FVector2D UMinimapFunctionLibrary::Rot2D(const FVector2D& V, float ThetaDegrees)
{
	if (!FMath::IsFinite(ThetaDegrees) || V.ContainsNaN())
	{
		return FVector2D::ZeroVector;
	}

	const double Theta = FMath::DegreesToRadians(static_cast<double>(ThetaDegrees));
	const double C = FMath::Cos(Theta);
	const double S = FMath::Sin(Theta);
	return FVector2D(V.X * C - V.Y * S,
	                 V.X * S + V.Y * C);
}

FVector2D UMinimapFunctionLibrary::GetEffectiveExtent(const FMinimapCalibration& Calibration)
{
	return Calibration.GetEffectiveExtent();
}

FVector2D UMinimapFunctionLibrary::GetZoomedExtent(const FMinimapCalibration& Calibration)
{
	return Calibration.GetZoomedExtent();
}

bool UMinimapFunctionLibrary::IsCalibrationValid(const FMinimapCalibration& Calibration)
{
	return Calibration.IsValidCalibration();
}

FMinimapProjectionContext UMinimapFunctionLibrary::MakeProjectionContext(
	const FMinimapCalibration& Calibration,
	const FVector2D& Anchor,
	float AnchorZ,
	float ViewYaw)
{
	FMinimapProjectionContext Context;
	Context.Build(Calibration, Anchor, AnchorZ, ViewYaw);
	return Context;
}

FMinimapCalibration UMinimapFunctionLibrary::MakeCalibrationFromWorldRange(
	float MinX, float MaxX, float MinY, float MaxY,
	bool bLegacyAxisMapping,
	bool bPreserveAspectRatio)
{
	FMinimapCalibration Calibration;

	// Order-independent: accept the range in either direction.
	const float LoX = FMath::Min(MinX, MaxX);
	const float HiX = FMath::Max(MinX, MaxX);
	const float LoY = FMath::Min(MinY, MaxY);
	const float HiY = FMath::Max(MinY, MaxY);

	Calibration.WorldCenter = FVector2D((LoX + HiX) * 0.5, (LoY + HiY) * 0.5);
	Calibration.WorldExtent = FVector2D((HiX - LoX) * 0.5, (HiY - LoY) * 0.5);
	Calibration.MapYaw = 0.0f;
	Calibration.bPreserveAspectRatio = bPreserveAspectRatio;
	Calibration.bSwapUV = bLegacyAxisMapping;

	if (!Calibration.IsValidCalibration())
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("MakeCalibrationFromWorldRange: degenerate range X[%.1f..%.1f] Y[%.1f..%.1f]; "
			     "returning calibration with a defaulted extent."),
			MinX, MaxX, MinY, MaxY);
		Calibration.WorldExtent = FVector2D(1000.0, 1000.0);
	}

	return Calibration;
}

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------

FVector2D UMinimapFunctionLibrary::ProjectWorldToNormalized(
	const FMinimapCalibration& Calibration,
	const FVector& WorldLocation,
	const FVector2D& Anchor,
	float ViewYaw)
{
	FMinimapProjectionContext Context;
	// AnchorZ is irrelevant to the XY result; only the height helpers consume it.
	Context.Build(Calibration, Anchor, 0.0f, ViewYaw);
	return Context.Project(WorldLocation);
}

FVector2D UMinimapFunctionLibrary::ProjectWorldToNormalizedCached(
	const FMinimapProjectionContext& Context,
	const FVector& WorldLocation)
{
	return Context.Project(WorldLocation);
}

FVector2D UMinimapFunctionLibrary::NormalizedToUV(const FVector2D& Normalized)
{
	return Normalized * 0.5 + FVector2D(0.5, 0.5);
}

FVector2D UMinimapFunctionLibrary::UVToNormalized(const FVector2D& UV)
{
	return (UV - FVector2D(0.5, 0.5)) * 2.0;
}

FVector2D UMinimapFunctionLibrary::NormalizedToMaterialParams(const FVector2D& Normalized)
{
	// The legacy M_Minimap wants -0.5..0.5, which is exactly half of the normalized range.
	return Normalized * 0.5;
}

FVector2D UMinimapFunctionLibrary::NormalizedToWidgetPixels(const FVector2D& Normalized, const FVector2D& WidgetSize)
{
	return NormalizedToUV(Normalized) * WidgetSize;
}

FVector2D UMinimapFunctionLibrary::ProjectWorldToWidgetPixels(
	const FMinimapCalibration& Calibration,
	const FVector& WorldLocation,
	const FVector2D& Anchor,
	float ViewYaw,
	const FVector2D& WidgetSize)
{
	const FVector2D N = ProjectWorldToNormalized(Calibration, WorldLocation, Anchor, ViewYaw);
	return NormalizedToWidgetPixels(N, WidgetSize);
}

float UMinimapFunctionLibrary::GetHeightRatio(const FMinimapCalibration& Calibration, float WorldZ)
{
	const float Span = Calibration.MaxZ - Calibration.MinZ;
	if (Span <= UE_KINDA_SMALL_NUMBER || !FMath::IsFinite(WorldZ))
	{
		return 0.5f;
	}
	return FMath::Clamp((WorldZ - Calibration.MinZ) / Span, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Out-of-bounds
// ---------------------------------------------------------------------------

float UMinimapFunctionLibrary::GetShapeMagnitude(const FVector2D& Normalized, bool bCircularMap)
{
	if (Normalized.ContainsNaN())
	{
		return 0.0f;
	}

	if (bCircularMap)
	{
		return static_cast<float>(Normalized.Size());
	}

	// Chebyshev distance: 1.0 exactly on a rectangle edge, which is what makes the
	// same hysteresis thresholds meaningful for both shapes.
	return static_cast<float>(FMath::Max(FMath::Abs(Normalized.X), FMath::Abs(Normalized.Y)));
}

FVector2D UMinimapFunctionLibrary::ClampNormalizedToShape(const FVector2D& Normalized, bool bCircularMap, bool& bOutWasClamped)
{
	bOutWasClamped = false;

	if (Normalized.ContainsNaN())
	{
		return FVector2D::ZeroVector;
	}

	if (bCircularMap)
	{
		const double Length = Normalized.Size();
		if (Length > 1.0)
		{
			// Degenerate length can't happen here (Length > 1), so the divide is safe.
			bOutWasClamped = true;
			return Normalized / Length;
		}
		return Normalized;
	}

	// Rectangular: ray-box intersection against the unit square.
	const double MaxComponent = FMath::Max(FMath::Abs(Normalized.X), FMath::Abs(Normalized.Y));
	if (MaxComponent <= UE_KINDA_SMALL_NUMBER)
	{
		// Exactly at the centre - nothing to clamp and no meaningful direction.
		return Normalized;
	}

	const double T = 1.0 / MaxComponent;
	if (T < 1.0)
	{
		bOutWasClamped = true;
		return Normalized * T;
	}
	return Normalized;
}

bool UMinimapFunctionLibrary::ResolveOutOfBounds(
	float ShapeMagnitude,
	bool bPreviouslyOutOfBounds,
	float EnterThreshold,
	float ExitThreshold)
{
	if (!FMath::IsFinite(ShapeMagnitude))
	{
		return bPreviouslyOutOfBounds;
	}

	// Guard against an inverted or equal pair of thresholds, which would make the
	// state oscillate instead of latching.
	const float Enter = EnterThreshold;
	const float Exit  = FMath::Min(ExitThreshold, EnterThreshold);

	if (bPreviouslyOutOfBounds)
	{
		// Stay out of bounds until we come back in past the lower threshold.
		return ShapeMagnitude >= Exit;
	}

	return ShapeMagnitude > Enter;
}

float UMinimapFunctionLibrary::GetEdgeAngle(const FVector2D& Normalized)
{
	if (Normalized.IsNearlyZero() || Normalized.ContainsNaN())
	{
		return 0.0f;
	}

	// atan2(x, -y): 0 = up, +90 = right. Clockwise-positive, matching
	// UWidget::SetRenderTransformAngle so the value needs no further conversion.
	const double Radians = FMath::Atan2(Normalized.X, -Normalized.Y);
	return NormalizeAngleDegrees(static_cast<float>(FMath::RadiansToDegrees(Radians)));
}

// ---------------------------------------------------------------------------
// Rotation outputs
// ---------------------------------------------------------------------------

float UMinimapFunctionLibrary::GetMarkerIconAngle(float ActorYaw, float ViewYaw)
{
	return NormalizeAngleDegrees(ActorYaw - ViewYaw);
}

float UMinimapFunctionLibrary::GetMapRotationTurns(float ViewYaw, float MapYawOffset, bool bNegate)
{
	const float Combined = bNegate ? -(ViewYaw + MapYawOffset) : (ViewYaw + MapYawOffset);
	if (!FMath::IsFinite(Combined))
	{
		return 0.0f;
	}

	// Frac towards -infinity so the result is always [0, 1) even for negative yaw.
	// FMath::Fmod would return a negative value here and make the material's Rotator
	// node jump by a full turn when the player crosses due north.
	const float Turns = Combined / 360.0f;
	return Turns - FMath::FloorToFloat(Turns);
}

float UMinimapFunctionLibrary::GetCompassAngle(float ViewYaw)
{
	return NormalizeAngleDegrees(-ViewYaw);
}

float UMinimapFunctionLibrary::NormalizeAngleDegrees(float AngleDegrees)
{
	if (!FMath::IsFinite(AngleDegrees))
	{
		return 0.0f;
	}
	// FRotator::NormalizeAxis returns (-180, 180].
	return FRotator::NormalizeAxis(AngleDegrees);
}
