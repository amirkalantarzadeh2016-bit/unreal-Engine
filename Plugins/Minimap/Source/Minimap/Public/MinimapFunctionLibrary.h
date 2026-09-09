#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MinimapCaptureTypes.h"
#include "MinimapTypes.h"

class UTexture2D;
#include "MinimapFunctionLibrary.generated.h"

/**
 * Stateless world <-> map conversions.
 *
 * Every piece of projection maths in the plugin funnels through here, which means:
 *  - the subsystem, the widget adapter and Blueprint all agree by construction;
 *  - the whole model is unit-testable without a UWorld (see MinimapProjectionTests.cpp).
 *
 * All functions are safe against zero extents, NaN input and degenerate vectors.
 */
UCLASS()
class MINIMAP_API UMinimapFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------
	// Primitives
	// ---------------------------------------------------------------------

	/** Rot2D(theta) * (x, y) = (x*cos - y*sin, x*sin + y*cos). Theta in DEGREES. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Math")
	static FVector2D Rot2D(const FVector2D& V, float ThetaDegrees);

	/** Effective half-extent after the aspect rule, before Zoom. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Calibration")
	static FVector2D GetEffectiveExtent(const FMinimapCalibration& Calibration);

	/** Effective half-extent with Zoom applied - the projection denominator. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Calibration")
	static FVector2D GetZoomedExtent(const FMinimapCalibration& Calibration);

	/** True when the calibration can be projected through safely. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Calibration")
	static bool IsCalibrationValid(const FMinimapCalibration& Calibration);

	/** Bake a calibration + anchor + view yaw into a reusable projection context. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Calibration")
	static FMinimapProjectionContext MakeProjectionContext(
		const FMinimapCalibration& Calibration,
		const FVector2D& Anchor,
		float AnchorZ,
		float ViewYaw);

	/**
	 * Build a calibration from an axis-aligned world range, e.g. the legacy
	 * 0..1000 / -1150..50 values, without hardcoding them anywhere in the runtime path.
	 * Use this once to A/B the new system against the old Blueprint.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Calibration")
	static FMinimapCalibration MakeCalibrationFromWorldRange(
		float MinX, float MaxX, float MinY, float MaxY,
		bool bLegacyAxisMapping = true,
		bool bPreserveAspectRatio = false);

	// ---------------------------------------------------------------------
	// Projection
	// ---------------------------------------------------------------------

	/** World position -> normalized minimap coordinate, [-1, 1] inside the map. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static FVector2D ProjectWorldToNormalized(
		const FMinimapCalibration& Calibration,
		const FVector& WorldLocation,
		const FVector2D& Anchor,
		float ViewYaw);

	/** Same, but through a pre-baked context (no trigonometry). */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static FVector2D ProjectWorldToNormalizedCached(
		const FMinimapProjectionContext& Context,
		const FVector& WorldLocation);

	/** Normalized -> standard UV, [0, 1] with (0,0) top-left. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static FVector2D NormalizedToUV(const FVector2D& Normalized);

	/** Standard UV -> normalized. Inverse of NormalizedToUV. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static FVector2D UVToNormalized(const FVector2D& UV);

	/**
	 * Normalized -> the [-0.5, 0.5] pair the legacy M_Minimap expects on its
	 * PlayerX / PlayerY scalar parameters. This is literally Normalized * 0.5.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static FVector2D NormalizedToMaterialParams(const FVector2D& Normalized);

	/** Normalized -> pixel offset inside a widget of the given size, origin top-left. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static FVector2D NormalizedToWidgetPixels(const FVector2D& Normalized, const FVector2D& WidgetSize);

	/** World position -> pixel offset in one call. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static FVector2D ProjectWorldToWidgetPixels(
		const FMinimapCalibration& Calibration,
		const FVector& WorldLocation,
		const FVector2D& Anchor,
		float ViewYaw,
		const FVector2D& WidgetSize);

	/** World Z -> [0, 1] across the calibrated Z band. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Projection")
	static float GetHeightRatio(const FMinimapCalibration& Calibration, float WorldZ);

	// ---------------------------------------------------------------------
	// Out-of-bounds
	// ---------------------------------------------------------------------

	/**
	 * Distance from the map centre expressed in "edge units": exactly 1.0 on the boundary
	 * for both shapes. Circular uses |N|; rectangular uses max(|N.x|, |N.y|), which is the
	 * ray-box parameter and the reason edge markers keep their true bearing.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Bounds")
	static float GetShapeMagnitude(const FVector2D& Normalized, bool bCircularMap);

	/**
	 * Clamp to the map boundary.
	 *
	 * Circular:    if |N| > 1  ->  N / |N|
	 * Rectangular: t = 1 / max(|N.x|, |N.y|);  if t < 1  ->  N * t
	 *
	 * Both scale the WHOLE vector, so the marker slides along its true bearing. Clamping
	 * X and Y independently would pile every off-screen marker into the four corners.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Bounds")
	static FVector2D ClampNormalizedToShape(const FVector2D& Normalized, bool bCircularMap, bool& bOutWasClamped);

	/**
	 * Hysteresis gate. Enter out-of-bounds at EnterThreshold, leave only below ExitThreshold.
	 * Without this a marker parked exactly on the boundary re-broadcasts every update.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Bounds")
	static bool ResolveOutOfBounds(
		float ShapeMagnitude,
		bool bPreviouslyOutOfBounds,
		float EnterThreshold = 1.0f,
		float ExitThreshold = 0.98f);

	/**
	 * Bearing to a marker in degrees, clockwise from up: atan2(N.x, -N.y).
	 * Directly usable with UWidget::SetRenderTransformAngle.
	 * N = (0,-1) -> 0 (up); (1,0) -> 90 (right); (0,1) -> 180 (down); (-1,0) -> -90 (left).
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Bounds")
	static float GetEdgeAngle(const FVector2D& Normalized);

	// ---------------------------------------------------------------------
	// Rotation outputs
	// ---------------------------------------------------------------------

	/** Marker icon rotation in degrees, normalized to [-180, 180). */
	UFUNCTION(BlueprintPure, Category = "Minimap|Rotation")
	static float GetMarkerIconAngle(float ActorYaw, float ViewYaw);

	/**
	 * Map rotation for the material's Rotator node, in TURNS, always [0, 1).
	 * Frac((ViewYaw + MapYawOffset) / 360), matching the legacy graph's sign.
	 * Set bNegate to flip the direction without re-authoring M_Minimap.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Rotation")
	static float GetMapRotationTurns(float ViewYaw, float MapYawOffset = 0.0f, bool bNegate = false);

	/** Compass angle ignoring map yaw. Kept for compatibility; prefer GetCompassAngleEx. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Rotation")
	static float GetCompassAngle(float ViewYaw);

	/**
	 * Screen angle at which world NORTH appears, in degrees clockwise from up - ready for
	 * UWidget::SetRenderTransformAngle.
	 *
	 * Derivation: in the projection, screen-up corresponds to the world bearing
	 * (MapYaw + ViewYaw). World north is bearing 0, so it appears at -(MapYaw + ViewYaw).
	 *
	 * MapYaw matters whenever the map image is not aligned to world north - a rotated
	 * bounds volume, or a non-zero AdditionalMapYaw. Ignoring it leaves the compass wrong
	 * by exactly that angle.
	 *
	 * CompassYawOffset is a further constant, for a ring texture whose "N" is not drawn at
	 * the top of the image. Positive values rotate the indicator CLOCKWISE on screen.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Rotation")
	static float GetCompassAngleEx(float ViewYaw, float MapYaw, float CompassYawOffset);

	/** Normalize any angle to [-180, 180). Safe against NaN and huge magnitudes. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Rotation")
	static float NormalizeAngleDegrees(float AngleDegrees);

	// ---------------------------------------------------------------------
	// Scene-capture alignment
	//
	// These make the generated background agree with the marker projection using the
	// SAME calibration, so no axis transform is ever applied twice. Kept here (rather
	// than inside the capture component) specifically so they are unit-testable.
	// ---------------------------------------------------------------------

	/**
	 * Can the calibration's axis convention be reproduced by orienting a camera?
	 *
	 * The map from view space to normalized space is linear with determinant
	 * SignU * SignV. A camera can only produce ORIENTATION-PRESERVING transforms, so the
	 * determinant must be positive - that is, bInvertU must equal bInvertV.
	 *
	 * If exactly one invert flag is set the convention is a MIRROR, which no camera
	 * orientation can produce. Rather than emit a silently flipped map, capture mode
	 * refuses to activate and reports this.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	static bool IsCaptureAlignmentSupported(const FMinimapCalibration& Calibration, FString& OutReason);

	/**
	 * World yaw for a downward capture whose rendered image lines up with the calibration.
	 *
	 *   CaptureYaw = MapYaw + (bSwapUV ? -90 : 0) + (bInvertU ? 180 : 0)
	 *
	 * Derivation: with Pitch = -90 and Roll = 0, the camera's up vector in world is
	 * (cos Yaw, sin Yaw, 0). Setting that equal to the world direction that projects to
	 * N = (0, -1) - the top edge of the map - yields the expression above. Because the
	 * transform is a rotation, aligning "up" aligns "right" automatically.
	 *
	 * Returns false (and leaves OutCaptureYaw at 0) when the convention is a mirror.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	static bool ComputeCaptureYaw(const FMinimapCalibration& Calibration, float& OutCaptureYaw, FString& OutReason);

	/**
	 * Orthographic width covering the calibration's full effective extent.
	 * OrthoWidth spans the image's horizontal (U) axis, whose half-extent is
	 * EffectiveExtent.X, so this is simply 2 * EffectiveExtent.X.
	 *
	 * Note this uses the EFFECTIVE extent, so when bPreserveAspectRatio is on the capture
	 * covers the padded square - the same area the markers are normalized against.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	static float GetCaptureOrthoWidth(const FMinimapCalibration& Calibration);

	// ---------------------------------------------------------------------
	// Asset loading
	// ---------------------------------------------------------------------

	/**
	 * Load a texture that ships inside the plugin's own Content folder.
	 *
	 * Pass a path relative to the plugin content root, e.g. "Textures/T_MapPlaceholder";
	 * it resolves against the "/Minimap/" mount point the plugin declares via
	 * CanContainContent. Returns null and logs if the asset is missing, so a stale path
	 * cannot silently produce a blank map.
	 *
	 * Synchronous load - call it at setup time, not per frame.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Assets")
	static UTexture2D* LoadPluginTexture(const FString& RelativePath);

	/** Load any texture by full object path, plugin or project. Null and a log on failure. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Assets")
	static UTexture2D* LoadTextureByPath(const FString& FullObjectPath);

	/**
	 * Render-target size whose aspect matches the effective extent, with the longest edge
	 * equal to MaxDimension. Matching the aspect is what stops non-square bounds from
	 * stretching the image. Both axes are clamped to [16, 8192] and rounded to even.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	static FIntPoint ComputeCaptureResolution(const FMinimapCalibration& Calibration, int32 MaxDimension = 1024);
};
