#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/ObjectMacros.h"
#include "MinimapTypes.generated.h"

class UMinimapTrackedComponent;
class UTexture2D;
class UUserWidget;

/**
 * ---------------------------------------------------------------------------
 * COORDINATE CONVENTIONS (read this before touching any sign in this file)
 * ---------------------------------------------------------------------------
 * World space  : Unreal left-handed Z-up. +X = North, +Y = East, +Z = Up.
 * Normalized   : "N". A 2D value that is [-1, 1] on both axes inside the map.
 *                N.x = -1 is the LEFT edge, N.x = +1 is the RIGHT edge.
 *                N.y = -1 is the TOP  edge, N.y = +1 is the BOTTOM edge.
 *                (N.y grows downward so it converts to UV/pixels without a flip.)
 * UV           : N * 0.5 + 0.5, i.e. [0, 1] with (0,0) top-left. Standard UE UV.
 * Material     : N * 0.5, i.e. [-0.5, 0.5]. This is what the legacy M_Minimap
 *                expects on its PlayerX / PlayerY scalar parameters.
 * Angles       : degrees, clockwise-positive, 0 = up. This matches
 *                UWidget::SetRenderTransformAngle so edge arrows and marker
 *                icons need no extra conversion.
 *
 * AXIS MAPPING - the single most important switch in this system.
 *
 *   Standard (bSwapUV == false), matches the design spec:
 *       N.x =  View.y / EffX      (world +Y / East  -> map right)
 *       N.y = -View.x / EffY      (world +X / North -> map up)
 *
 *   Legacy (bSwapUV == true), matches the existing M_Minimap:
 *       N.x =  View.x / EffX      (world +X -> U)
 *       N.y =  View.y / EffY      (world +Y -> V)
 *
 * The legacy path exists because the original Blueprint fed PlayerX from world X
 * and PlayerY from world Y. That is an axis SWAP, not a sign inversion, so it
 * cannot be expressed with bInvertU / bInvertV alone. The swap is applied to the
 * rotated view-space vector BEFORE normalization, so both conventions remain
 * exact under arbitrary MapYaw and ViewYaw.
 */

/** How the map image is oriented relative to the world while the game runs. */
UENUM(BlueprintType)
enum class EMinimapOrientationMode : uint8
{
	/** Map counter-rotates under a fixed marker. ViewYaw = viewer yaw. */
	RotatingMap		UMETA(DisplayName = "Rotating Map"),

	/** Map stays north-aligned and marker icons rotate instead. ViewYaw = 0. */
	NorthUp			UMETA(DisplayName = "North Up")
};

/** Which world position sits at the centre of the minimap widget. */
UENUM(BlueprintType)
enum class EMinimapAnchorMode : uint8
{
	/** The viewer is always at the centre and the map scrolls beneath it. */
	ViewerCentered	UMETA(DisplayName = "Viewer Centered"),

	/** The map is pinned; the viewer's marker moves across it. */
	FixedMapCenter	UMETA(DisplayName = "Fixed Map Center")
};

/** What a marker does once it leaves the visible map area. */
UENUM(BlueprintType)
enum class EMinimapOutOfBoundsPolicy : uint8
{
	/** Pin the marker to the map edge on its true bearing and keep it visible. */
	ClampToEdge		UMETA(DisplayName = "Clamp To Edge"),

	/** Hide the marker entirely while it is out of bounds. */
	Hide			UMETA(DisplayName = "Hide"),

	/** Never clamp and never hide. Position may exceed [-1, 1]. */
	AlwaysShow		UMETA(DisplayName = "Always Show (Unclamped)")
};

/**
 * Everything needed to turn a world XY into a normalized map coordinate.
 * Authored by AMinimapBoundsVolume, or built by hand / from a data asset.
 * Contains no per-frame state - it is safe to copy and cache.
 */
USTRUCT(BlueprintType)
struct MINIMAP_API FMinimapCalibration
{
	GENERATED_BODY()

	/** World-space XY that maps to the centre of the map image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	FVector2D WorldCenter = FVector2D::ZeroVector;

	/** Half-size of the mapped area in centimetres, along the map's own axes. Must be > 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (ClampMin = "1.0"))
	FVector2D WorldExtent = FVector2D(1000.0, 1000.0);

	/** Yaw of the map image relative to world +X, in degrees. Usually the bounds volume's yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	float MapYaw = 0.0f;

	/** Magnification. 1 = whole mapped area visible; 2 = half of it (zoomed in). Must be > 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "8.0"))
	float Zoom = 1.0f;

	/** Bottom of the mapped Z band. Used only for the height ratio / height filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	float MinZ = -10000.0f;

	/** Top of the mapped Z band. Must be greater than MinZ. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	float MaxZ = 10000.0f;

	/**
	 * Force a square effective extent using max(X, Y).
	 * The original Blueprint used 1000 cm on X and 1200 cm on Y, which squashed the
	 * map ~20% on one axis. Leave this on for undistorted distances; turn it off only
	 * to reproduce the legacy look exactly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	bool bPreserveAspectRatio = true;

	/** Circular map: radial clamping and |N| magnitude. Otherwise rectangular ray-box clamping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	bool bCircularMap = false;

	/**
	 * Swap the axis mapping so world X drives U and world Y drives V.
	 * REQUIRED to match the existing M_Minimap. See the header comment above.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration|Axis")
	bool bSwapUV = false;

	/** Negate the horizontal axis after normalization (mirrors the map left/right). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration|Axis")
	bool bInvertU = false;

	/** Negate the vertical axis after normalization (mirrors the map top/bottom). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration|Axis")
	bool bInvertV = false;

	/**
	 * Effective half-extent, after the aspect-ratio rule but BEFORE Zoom is applied.
	 * Never returns a component <= 0; callers still guard with IsValidCalibration().
	 */
	FVector2D GetEffectiveExtent() const
	{
		FVector2D Extent = WorldExtent.GetAbs();
		if (bPreserveAspectRatio)
		{
			// Expand the smaller axis so world distance is isotropic on the map.
			// Expanding (never cropping) means some dead world becomes visible
			// rather than playable area being hidden.
			const double Square = FMath::Max(Extent.X, Extent.Y);
			Extent = FVector2D(Square, Square);
		}
		return Extent;
	}

	/** Effective half-extent with Zoom folded in. This is the projection denominator. */
	FVector2D GetZoomedExtent() const
	{
		const float SafeZoom = (Zoom > UE_KINDA_SMALL_NUMBER) ? Zoom : 1.0f;
		return GetEffectiveExtent() / SafeZoom;
	}

	/** True when this calibration can be projected through without producing NaN/Inf. */
	bool IsValidCalibration(FString* OutReason = nullptr) const
	{
		const FVector2D Extent = WorldExtent.GetAbs();
		if (Extent.X <= UE_KINDA_SMALL_NUMBER || Extent.Y <= UE_KINDA_SMALL_NUMBER)
		{
			if (OutReason) { *OutReason = TEXT("WorldExtent has a zero or negative component."); }
			return false;
		}
		if (Zoom <= UE_KINDA_SMALL_NUMBER)
		{
			if (OutReason) { *OutReason = TEXT("Zoom must be greater than zero."); }
			return false;
		}
		if (MaxZ <= MinZ)
		{
			if (OutReason) { *OutReason = TEXT("MaxZ must be greater than MinZ."); }
			return false;
		}
		if (WorldCenter.ContainsNaN() || WorldExtent.ContainsNaN() || !FMath::IsFinite(MapYaw))
		{
			if (OutReason) { *OutReason = TEXT("Calibration contains NaN or non-finite values."); }
			return false;
		}
		return true;
	}

	bool operator==(const FMinimapCalibration& Other) const
	{
		return WorldCenter.Equals(Other.WorldCenter)
			&& WorldExtent.Equals(Other.WorldExtent)
			&& FMath::IsNearlyEqual(MapYaw, Other.MapYaw)
			&& FMath::IsNearlyEqual(Zoom, Other.Zoom)
			&& FMath::IsNearlyEqual(MinZ, Other.MinZ)
			&& FMath::IsNearlyEqual(MaxZ, Other.MaxZ)
			&& bPreserveAspectRatio == Other.bPreserveAspectRatio
			&& bCircularMap == Other.bCircularMap
			&& bSwapUV == Other.bSwapUV
			&& bInvertU == Other.bInvertU
			&& bInvertV == Other.bInvertV;
	}

	bool operator!=(const FMinimapCalibration& Other) const { return !(*this == Other); }
};

/**
 * A calibration baked against a specific anchor and view yaw, with sin/cos and the
 * reciprocal extent precomputed.
 *
 * This is the whole point of the performance design: the subsystem builds ONE of these
 * per view per update, then every marker costs 2 subtractions, 4 multiplies, 2 adds and
 * 2 more multiplies. No trigonometry and no division inside the marker loop.
 */
USTRUCT(BlueprintType)
struct MINIMAP_API FMinimapProjectionContext
{
	GENERATED_BODY()

	/** World XY that sits at the centre of the map. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	FVector2D Anchor = FVector2D::ZeroVector;

	/** World Z of the anchor, used as the reference plane for height filtering. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float AnchorZ = 0.0f;

	/** Yaw that points "up" on the map, in degrees. 0 for North-Up. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float ViewYaw = 0.0f;

	/** Circular map shape, copied from the calibration. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	bool bCircularMap = false;

	/** False until Build() has succeeded against a valid calibration. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	bool bValid = false;

	/** cos(-(MapYaw + ViewYaw)) in radians. */
	UPROPERTY()
	double CosTheta = 1.0;

	/** sin(-(MapYaw + ViewYaw)) in radians. */
	UPROPERTY()
	double SinTheta = 0.0;

	/** 1 / GetZoomedExtent(), so the marker loop multiplies instead of dividing. */
	UPROPERTY()
	FVector2D InvExtent = FVector2D(1.0, 1.0);

	/** +1 or -1 from bInvertU. */
	UPROPERTY()
	float SignU = 1.0f;

	/** +1 or -1 from bInvertV. */
	UPROPERTY()
	float SignV = 1.0f;

	/** Copied from the calibration so Project() needs no extra lookups. */
	UPROPERTY()
	bool bSwapUV = false;

	UPROPERTY()
	float MinZ = -10000.0f;

	UPROPERTY()
	float MaxZ = 10000.0f;

	/**
	 * Bake a calibration + anchor + view yaw into this context.
	 * Leaves bValid == false (and the context at identity) if the calibration is unusable,
	 * so callers can early-out rather than propagating NaN into widget transforms.
	 */
	void Build(const FMinimapCalibration& Calibration, const FVector2D& InAnchor, float InAnchorZ, float InViewYaw)
	{
		Anchor   = InAnchor;
		AnchorZ  = InAnchorZ;
		ViewYaw  = InViewYaw;
		bCircularMap = Calibration.bCircularMap;
		bSwapUV  = Calibration.bSwapUV;
		SignU    = Calibration.bInvertU ? -1.0f : 1.0f;
		SignV    = Calibration.bInvertV ? -1.0f : 1.0f;
		MinZ     = Calibration.MinZ;
		MaxZ     = Calibration.MaxZ;

		if (!Calibration.IsValidCalibration())
		{
			CosTheta  = 1.0;
			SinTheta  = 0.0;
			InvExtent = FVector2D(1.0, 1.0);
			bValid    = false;
			return;
		}

		// Rotating the world by -(MapYaw + ViewYaw) brings it into map space:
		// undo the map image's own yaw, then undo the viewer's yaw.
		const double Theta = FMath::DegreesToRadians(static_cast<double>(-(Calibration.MapYaw + InViewYaw)));
		CosTheta = FMath::Cos(Theta);
		SinTheta = FMath::Sin(Theta);

		const FVector2D Zoomed = Calibration.GetZoomedExtent();
		InvExtent = FVector2D(1.0 / Zoomed.X, 1.0 / Zoomed.Y);
		bValid = true;
	}

	/** World XY -> normalized map coordinate. Returns zero for an invalid context. */
	FVector2D ProjectXY(const FVector2D& WorldXY) const
	{
		if (!bValid)
		{
			return FVector2D::ZeroVector;
		}

		// Rel: offset from the anchor, still in world axes.
		const FVector2D Rel = WorldXY - Anchor;

		// View: Rot2D(theta) * Rel, with theta = -(MapYaw + ViewYaw).
		//   (x*cos - y*sin, x*sin + y*cos)
		const double ViewX = Rel.X * CosTheta - Rel.Y * SinTheta;
		const double ViewY = Rel.X * SinTheta + Rel.Y * CosTheta;

		// Axis mapping. See the convention block at the top of this header.
		//   standard: right = +East (View.y),  down = -North (-View.x)
		//   legacy  : U = View.x,              V = View.y
		const double AxisX = bSwapUV ? ViewX :  ViewY;
		const double AxisY = bSwapUV ? ViewY : -ViewX;

		return FVector2D(
			SignU * AxisX * InvExtent.X,
			SignV * AxisY * InvExtent.Y);
	}

	/** Convenience overload; ignores Z. */
	FVector2D Project(const FVector& WorldLocation) const
	{
		return ProjectXY(FVector2D(WorldLocation.X, WorldLocation.Y));
	}

	/** World Z -> [0, 1] across the calibrated Z band. Returns 0.5 for a degenerate band. */
	float GetHeightRatio(float WorldZ) const
	{
		const float Span = MaxZ - MinZ;
		if (Span <= UE_KINDA_SMALL_NUMBER)
		{
			return 0.5f;
		}
		return FMath::Clamp((WorldZ - MinZ) / Span, 0.0f, 1.0f);
	}
};

/** Visual description of a marker. Pure data; the widget layer decides how to use it. */
USTRUCT(BlueprintType)
struct MINIMAP_API FMinimapMarkerStyle
{
	GENERATED_BODY()

	/** Icon drawn while the marker is inside the map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Optional distinct icon (usually an arrow) drawn while clamped to the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	TObjectPtr<UTexture2D> OutOfBoundsIcon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	FLinearColor Tint = FLinearColor::White;

	/** Desired icon size in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	FVector2D IconSize = FVector2D(24.0, 24.0);

	/** Optional per-marker widget class; falls back to the view widget's default when null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	TSubclassOf<UUserWidget> MarkerWidgetClass;
};

/**
 * The result of projecting one marker for one view. Produced fresh each batched update
 * and handed to the widget layer as a flat array - deliberately POD-ish so a future
 * Slate or instanced-material renderer can consume the same data unchanged.
 */
USTRUCT(BlueprintType)
struct MINIMAP_API FMinimapMarkerSnapshot
{
	GENERATED_BODY()

	/**
	 * Strong pointer, but only ever held for the lifetime of a single update inside a
	 * UPROPERTY array, so it is GC-visible and cannot dangle. Always null-check: the
	 * owning actor can still be destroyed between the update and the widget reading it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	TObjectPtr<UMinimapTrackedComponent> Tracked = nullptr;

	/** Raw normalized position. May exceed [-1, 1] when out of bounds. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	FVector2D Normalized = FVector2D::ZeroVector;

	/** Shape-clamped position, ready to convert to UV or pixels. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	FVector2D Clamped = FVector2D::ZeroVector;

	/** Bearing to the marker in degrees, clockwise from up. Valid mainly when clamped. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float EdgeAngle = 0.0f;

	/** Icon rotation in degrees (ActorYaw - ViewYaw). Zero when bUseActorYaw is off. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float IconAngle = 0.0f;

	/** World Z normalized across the calibrated Z band, [0, 1]. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float HeightRatio = 0.5f;

	/** Signed world Z offset from the view anchor. Negative = below the viewer. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float HeightOffset = 0.0f;

	/** Planar (XY) world distance from the view anchor, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float DistanceToAnchor = 0.0f;

	/** Hysteresis-filtered out-of-bounds state. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	bool bOutOfBounds = false;

	/** False when culled by distance, height filter, policy or manual visibility. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	bool bVisible = true;

	/** Copied from the component; higher draws on top. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	int32 Priority = 0;
};
