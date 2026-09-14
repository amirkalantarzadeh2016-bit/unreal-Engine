#include "MinimapFunctionLibrary.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
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
	// Equivalent to GetCompassAngleEx with no map yaw and no offset.
	return NormalizeAngleDegrees(-ViewYaw);
}

float UMinimapFunctionLibrary::GetCompassAngleEx(float ViewYaw, float MapYaw, float CompassYawOffset)
{
	// Screen-up is world bearing (MapYaw + ViewYaw); north is bearing 0, so north sits at
	// the negation of that. The offset is then applied in screen space, clockwise-positive.
	return NormalizeAngleDegrees(-(ViewYaw + MapYaw) + CompassYawOffset);
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

// ---------------------------------------------------------------------------
// Scene-capture alignment
// ---------------------------------------------------------------------------

bool UMinimapFunctionLibrary::IsCaptureAlignmentSupported(const FMinimapCalibration& Calibration, FString& OutReason)
{
	if (!Calibration.IsValidCalibration(&OutReason))
	{
		return false;
	}

	// The view-space -> normalized map is:
	//     no swap: [[0, SignU], [-SignV, 0]]
	//     swap:    [[SignU, 0], [0, SignV]]
	// Both have determinant SignU * SignV. A camera orientation is a rotation, which is
	// orientation-preserving, so a negative determinant (a mirror) is unreachable.
	if (Calibration.bInvertU != Calibration.bInvertV)
	{
		OutReason = TEXT("bInvertU and bInvertV differ, which describes a mirrored axis "
		                 "convention. No camera orientation can produce a mirrored image, so "
		                 "automatic capture cannot be aligned to this calibration. Either set "
		                 "both invert flags the same, or mirror the map inside the material.");
		return false;
	}

	OutReason.Reset();
	return true;
}

bool UMinimapFunctionLibrary::ComputeCaptureYaw(const FMinimapCalibration& Calibration, float& OutCaptureYaw, FString& OutReason)
{
	OutCaptureYaw = 0.0f;

	if (!IsCaptureAlignmentSupported(Calibration, OutReason))
	{
		return false;
	}

	// See the header for the derivation. Each term is the rotation needed to bring the
	// map's "up" direction onto the camera's up vector.
	float Yaw = Calibration.MapYaw;
	if (Calibration.bSwapUV)
	{
		Yaw -= 90.0f;
	}
	if (Calibration.bInvertU) // == bInvertV, guaranteed above
	{
		Yaw += 180.0f;
	}

	OutCaptureYaw = NormalizeAngleDegrees(Yaw);
	return true;
}

float UMinimapFunctionLibrary::GetCaptureOrthoWidth(const FMinimapCalibration& Calibration)
{
	const FVector2D Extent = Calibration.GetZoomedExtent();
	// Guard: GetZoomedExtent already refuses zero, but a caller may pass a raw struct.
	return FMath::Max(static_cast<float>(Extent.X * 2.0), 1.0f);
}

FIntPoint UMinimapFunctionLibrary::ComputeCaptureResolution(const FMinimapCalibration& Calibration, int32 MaxDimension)
{
	const int32 ClampedMax = FMath::Clamp(MaxDimension, 16, 8192);

	const FVector2D Extent = Calibration.GetEffectiveExtent();
	if (Extent.X <= UE_KINDA_SMALL_NUMBER || Extent.Y <= UE_KINDA_SMALL_NUMBER)
	{
		return FIntPoint(ClampedMax, ClampedMax);
	}

	// Match the image aspect to the covered world aspect; anything else stretches.
	int32 Width;
	int32 Height;
	if (Extent.X >= Extent.Y)
	{
		Width  = ClampedMax;
		Height = FMath::RoundToInt(ClampedMax * (Extent.Y / Extent.X));
	}
	else
	{
		Height = ClampedMax;
		Width  = FMath::RoundToInt(ClampedMax * (Extent.X / Extent.Y));
	}

	// Even dimensions keep mip/format handling well behaved on every RHI.
	auto MakeEven = [](int32 Value) { return (Value % 2 == 0) ? Value : Value + 1; };

	return FIntPoint(
		MakeEven(FMath::Clamp(Width,  16, 8192)),
		MakeEven(FMath::Clamp(Height, 16, 8192)));
}


// ---------------------------------------------------------------------------
// Asset loading
// ---------------------------------------------------------------------------

UTexture2D* UMinimapFunctionLibrary::LoadPluginTexture(const FString& RelativePath)
{
	if (RelativePath.IsEmpty())
	{
		return nullptr;
	}

	// "/Minimap/" is the mount point the plugin gets from CanContainContent in the
	// descriptor. Accept a path with or without a leading slash so both read naturally.
	FString Trimmed = RelativePath;
	Trimmed.RemoveFromStart(TEXT("/"));

	const FString FullPath = FString::Printf(TEXT("/Minimap/%s"), *Trimmed);
	return LoadTextureByPath(FullPath);
}

UTexture2D* UMinimapFunctionLibrary::LoadTextureByPath(const FString& FullObjectPath)
{
	if (FullObjectPath.IsEmpty())
	{
		return nullptr;
	}

	// A package path without an object name ("/X/Y/T_Map") needs the ".T_Map" suffix to
	// address the object rather than the package. Add it when it is missing.
	FString ObjectPath = FullObjectPath;
	if (!ObjectPath.Contains(TEXT(".")))
	{
		FString AssetName;
		ObjectPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (!AssetName.IsEmpty())
		{
			ObjectPath = FString::Printf(TEXT("%s.%s"), *ObjectPath, *AssetName);
		}
	}

	UTexture2D* Texture = Cast<UTexture2D>(
		StaticLoadObject(UTexture2D::StaticClass(), nullptr, *ObjectPath));

	if (!Texture)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("LoadTextureByPath: could not load a UTexture2D at '%s'. Check the path, and that "
			     "the plugin's Content folder is present and cooked for this build."), *ObjectPath);
	}

	return Texture;
}


// ---------------------------------------------------------------------------
// Generated marker icons
// ---------------------------------------------------------------------------

namespace MinimapIconShapes
{
	/**
	 * Is a point inside the shape, in normalised coordinates where the icon spans
	 * [-1, 1] on both axes? Scale shrinks the shape so the same function serves both the
	 * fill and the slightly larger outline pass.
	 */
	static bool IsInside(EMinimapMarkerShape Shape, float X, float Y, float Scale)
	{
		switch (Shape)
		{
		case EMinimapMarkerShape::Circle:
			return (X * X + Y * Y) <= (Scale * Scale);

		case EMinimapMarkerShape::Ring:
			{
				const float DistSq = X * X + Y * Y;
				const float Inner = Scale * 0.52f;
				return DistSq <= (Scale * Scale) && DistSq >= (Inner * Inner);
			}

		case EMinimapMarkerShape::Square:
			return FMath::Max(FMath::Abs(X), FMath::Abs(Y)) <= Scale;

		case EMinimapMarkerShape::Diamond:
			return (FMath::Abs(X) + FMath::Abs(Y)) <= Scale;

		case EMinimapMarkerShape::Cross:
			{
				const float Arm = Scale * 0.30f;
				return (FMath::Abs(X) <= Arm && FMath::Abs(Y) <= Scale)
					|| (FMath::Abs(Y) <= Arm && FMath::Abs(X) <= Scale);
			}

		case EMinimapMarkerShape::Arrow:
			{
				// Chevron pointing UP (screen -Y), with a notched tail so it reads as a
				// direction rather than a plain triangle at small sizes.
				if (Y > Scale * 0.85f || Y < -Scale)
				{
					return false;
				}
				// Widen linearly from the apex down to the tail.
				const float Alpha = (Y + Scale) / (Scale * 1.85f);          // 0 at apex
				const float HalfWidth = FMath::Lerp(0.0f, Scale * 0.85f, Alpha);
				if (FMath::Abs(X) > HalfWidth)
				{
					return false;
				}
				// Notch: cut a smaller inverted wedge out of the tail.
				const float NotchTop = Scale * 0.15f;
				if (Y > NotchTop)
				{
					const float NotchAlpha = (Y - NotchTop) / (Scale * 0.85f - NotchTop);
					if (FMath::Abs(X) < FMath::Lerp(0.0f, Scale * 0.45f, NotchAlpha))
					{
						return false;
					}
				}
				return true;
			}

		default:
			return (X * X + Y * Y) <= (Scale * Scale);
		}
	}

	/** Fraction of a pixel covered by the shape, 4x4 supersampled. */
	static float Coverage(EMinimapMarkerShape Shape, int32 PX, int32 PY, int32 Size, float Scale)
	{
		constexpr int32 Samples = 4;
		int32 Hits = 0;

		for (int32 SY = 0; SY < Samples; ++SY)
		{
			for (int32 SX = 0; SX < Samples; ++SX)
			{
				// Sample at sub-pixel centres, mapped to [-1, 1].
				const float U = ((PX + (SX + 0.5f) / Samples) / Size) * 2.0f - 1.0f;
				const float V = ((PY + (SY + 0.5f) / Samples) / Size) * 2.0f - 1.0f;
				if (IsInside(Shape, U, V, Scale))
				{
					++Hits;
				}
			}
		}

		return static_cast<float>(Hits) / (Samples * Samples);
	}
}

UTexture2D* UMinimapFunctionLibrary::CreateMarkerShapeTexture(
	EMinimapMarkerShape Shape, int32 PixelSize, float OutlineThickness, FLinearColor OutlineColor)
{
	const int32 Size = FMath::Clamp(PixelSize, 8, 512);
	const float Outline = FMath::Clamp(OutlineThickness, 0.0f, 0.4f);

	// The outline pass draws the shape slightly larger; the fill pass draws it inset by the
	// outline thickness, and the difference between the two is the rim.
	const float OuterScale = 0.94f;
	const float InnerScale = FMath::Max(OuterScale - Outline, 0.05f);

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Size * Size);

	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float Outer = MinimapIconShapes::Coverage(Shape, X, Y, Size, OuterScale);
			const float Fill  = MinimapIconShapes::Coverage(Shape, X, Y, Size, InnerScale);

			// Alpha is the outer silhouette; colour blends from rim to fill. Keeping the
			// fill pure white is what lets a single texture serve every marker tint.
			const float FillRatio = (Outer > UE_KINDA_SMALL_NUMBER) ? (Fill / Outer) : 0.0f;
			const FLinearColor Blended = FMath::Lerp(OutlineColor, FLinearColor::White, FillRatio);

			FColor& Out = Pixels[Y * Size + X];
			Out.R = static_cast<uint8>(FMath::Clamp(Blended.R, 0.0f, 1.0f) * 255.0f + 0.5f);
			Out.G = static_cast<uint8>(FMath::Clamp(Blended.G, 0.0f, 1.0f) * 255.0f + 0.5f);
			Out.B = static_cast<uint8>(FMath::Clamp(Blended.B, 0.0f, 1.0f) * 255.0f + 0.5f);
			Out.A = static_cast<uint8>(FMath::Clamp(Outer, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
	if (!Texture)
	{
		UE_LOG(LogMinimap, Warning, TEXT("CreateMarkerShapeTexture: could not create a %dx%d texture."), Size, Size);
		return nullptr;
	}

	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->NeverStream = true;
	Texture->CompressionSettings = TC_EditorIcon;

	// FColor is B,G,R,A in memory, matching PF_B8G8R8A8 with no shuffling.
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();

	Texture->UpdateResource();
	return Texture;
}
