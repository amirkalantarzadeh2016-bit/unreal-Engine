#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MinimapCaptureTypes.h"
#include "MinimapTypes.h"
#include "MinimapBoundsVolume.generated.h"

class UBillboardComponent;
class UBoxComponent;
class UMinimapCaptureComponent;
class UMinimapPresetAsset;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;

/**
 * Drop one of these in the level, scale its box to cover the playable area, and the minimap
 * calibrates itself on BeginPlay. This is the actor the original Blueprint reached for
 * (BP-OverlapActor) but never actually used - the ranges were hardcoded instead.
 *
 * The derived calibration is:
 *   WorldCenter = box world location (XY)
 *   WorldExtent = box scaled extent   (XY)
 *   MapYaw      = actor yaw           (so a rotated volume rotates the map with it)
 *   MinZ / MaxZ = box world Z +/- scaled extent Z
 *
 * No hardcoded world ranges appear anywhere: every number comes from the transform.
 */
UCLASS(Blueprintable, HideCategories = (Input, Replication, Collision, LOD, Cooking),
	meta = (DisplayName = "Minimap Bounds Volume"))
class MINIMAP_API AMinimapBoundsVolume : public AActor
{
	GENERATED_BODY()

public:
	AMinimapBoundsVolume();

	/** Scale this to cover the playable area. Rotate the actor to tilt the map. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Minimap")
	TObjectPtr<UBoxComponent> BoundsBox;

#if WITH_EDITORONLY_DATA
	/** Editor-only sprite so the volume is easy to find in a busy level. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorSprite;
#endif

	// ---------------------------------------------------------------------
	// Behaviour
	// ---------------------------------------------------------------------

	/** Push this volume's calibration to the subsystem on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	bool bApplyOnBeginPlay = true;

	/**
	 * Ignore the box entirely and publish ManualCalibration verbatim.
	 * Use when the map image was authored against known coordinates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration")
	bool bManualOverride = false;

	/** Used instead of the box transform when bManualOverride is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "bManualOverride"))
	FMinimapCalibration ManualCalibration;

	// --- Options folded into the derived calibration ----------------------

	/** Take MapYaw from this actor's yaw. Turn off to pin the map to world north. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride"))
	bool bUseActorYawAsMapYaw = true;

	/** Extra yaw added on top, for a map texture whose "up" is not the volume's forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride", Units = "deg"))
	float AdditionalMapYaw = 0.0f;

	/** Square the effective extent so world distances are undistorted on the map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride"))
	bool bPreserveAspectRatio = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride"))
	bool bCircularMap = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Calibration",
		meta = (EditCondition = "!bManualOverride", ClampMin = "0.01"))
	float Zoom = 1.0f;

	// --- Axis convention --------------------------------------------------

	/**
	 * Match the legacy M_Minimap: world X -> U, world Y -> V.
	 * Leave OFF for the standard convention (world +Y -> map right, world +X -> map up).
	 * This is an axis SWAP and cannot be expressed by the invert flags below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Axis",
		meta = (EditCondition = "!bManualOverride"))
	bool bSwapUV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Axis",
		meta = (EditCondition = "!bManualOverride"))
	bool bInvertU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Axis",
		meta = (EditCondition = "!bManualOverride"))
	bool bInvertV = false;

	// ---------------------------------------------------------------------
	// API
	// ---------------------------------------------------------------------

	/** Build the calibration this volume represents. Does not touch the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	FMinimapCalibration BuildCalibration() const;

	/** Build and publish to the subsystem. Returns false if invalid or unavailable. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap")
	bool ApplyCalibration();

	// ---------------------------------------------------------------------
	// Bounds selection (deterministic across levels)
	// ---------------------------------------------------------------------

	/**
	 * Marks this volume as the authoritative one when several exist. Checked first by
	 * UMinimapSubsystem::ResolveAuthoritativeBounds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Selection")
	bool bPreferredBounds = false;

	/**
	 * Optional identifier. When the subsystem is configured with a matching tag this
	 * volume is chosen, which is how a project selects between per-floor volumes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Selection")
	FName BoundsSelectionTag = NAME_None;

	// ---------------------------------------------------------------------
	// Preset + capture
	// ---------------------------------------------------------------------

	/** Portable settings shared across levels/projects. Per-instance overrides below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset")
	TObjectPtr<UMinimapPresetAsset> Preset;

	/** Ignore the preset's capture settings and use CaptureSettingsOverride instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset")
	bool bOverridePresetCaptureSettings = false;

	/**
	 * Used when there is no preset, or when bOverridePresetCaptureSettings is set.
	 * Defaults to StaticTexture, so an existing project is unaffected until opted in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset",
		meta = (EditCondition = "bOverridePresetCaptureSettings || Preset == nullptr"))
	FMinimapCaptureSettings CaptureSettingsOverride;

	/**
	 * The hand-authored map image for Static Texture mode.
	 *
	 * OPTIONAL and backward compatible: leave it empty and nothing changes - the widget
	 * keeps whatever image M_Minimap already samples. Assign it and the plugin pushes it
	 * through the same path the capture uses, which also makes it previewable in Details.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Background")
	TObjectPtr<UTexture2D> StaticMapTexture;

	/** Level-specific actors hidden from the minimap capture only (roofs, ceilings). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility")
	TArray<TSoftObjectPtr<AActor>> CaptureExcludedActors;

	/** Level-specific actors rendered when the preset enables the show-only workflow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility")
	TArray<TSoftObjectPtr<AActor>> CaptureIncludedActors;

	// ---------------------------------------------------------------------
	// Fit filtering (stops sky spheres producing enormous bounds)
	// ---------------------------------------------------------------------

	/** Actors with any of these tags are ignored by Fit To Level Bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Fit")
	TArray<FName> FitExcludeTags;

	/** When non-empty, ONLY actors carrying one of these tags are considered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Fit")
	TArray<FName> FitRequireTags;

	/** Actor classes ignored by the fit (sky spheres, fog, post-process volumes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Fit")
	TArray<TSubclassOf<AActor>> FitExcludeClasses;

	/**
	 * Actors whose bounds exceed this half-extent on X or Y are skipped. This is the
	 * guard that keeps one sky sphere from inflating the map to kilometres.
	 * 0 disables the check.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Fit",
		meta = (ClampMin = "0.0", Units = "cm"))
	float FitMaxActorExtent = 100000.0f;

	/** Skip actors with no collision AND no visible geometry contribution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Fit")
	bool bFitIgnoreHiddenActors = true;

	/** Uniform padding added around the fitted result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Fit",
		meta = (ClampMin = "0.0", Units = "cm"))
	float FitPadding = 200.0f;

	// ---------------------------------------------------------------------
	// Runtime API
	// ---------------------------------------------------------------------

	/** Preset settings merged with the per-instance override, sanitized. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	FMinimapCaptureSettings GetEffectiveCaptureSettings() const;

	/** The capture component, created on demand. Null when capture mode is off. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UMinimapCaptureComponent* GetCaptureComponent() const { return CaptureComponent; }

	/**
	 * Coalesced background re-render within the CURRENT bounds. This is what a furniture
	 * system calls after an edit - it never moves or resizes the calibration.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap",
		meta = (DisplayName = "Refresh Background"))
	void RefreshMinimapBackground();

	/**
	 * Re-fit the bounds to the level, re-apply the calibration, then refresh.
	 * This DOES move the map calibration - markers and image both shift. Distinct from
	 * RefreshMinimapBackground on purpose.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap",
		meta = (DisplayName = "Fit Bounds And Refresh"))
	void FitBoundsAndRefresh();

	/** Re-read the preset/override, re-apply to the capture, and refresh. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap",
		meta = (DisplayName = "Apply Capture Settings And Refresh"))
	void ApplyCaptureSettingsAndRefresh();

	/** Check the whole setup and report every problem found. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap",
		meta = (DisplayName = "Validate Minimap Setup"))
	FMinimapValidationReport ValidateMinimapSetup();

	/**
	 * Explicit readiness entry point for streamed levels and procedural content: call it
	 * once the geometry that should appear on the map exists. Applies the calibration if
	 * that has not happened yet, then requests a refresh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void NotifyMinimapContentReady();

	/** Create and register the capture component if capture mode is on. Idempotent. */
	UMinimapCaptureComponent* EnsureCaptureComponent();

	// ---------------------------------------------------------------------
	// Preview (works in the editor, does not change configuration)
	// ---------------------------------------------------------------------

	/**
	 * Render a capture for preview purposes WITHOUT switching Background Source and
	 * WITHOUT touching StaticMapTexture. Works in the editor world as well as in PIE.
	 * Returns false and fills OutError on failure.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap|Preview",
		meta = (DisplayName = "Capture Preview Now"))
	bool CapturePreviewNow();

	/** Last preview/live render target, or null if nothing has been captured. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Preview")
	UTextureRenderTarget2D* GetPreviewRenderTarget() const;

	/** Which source is actually driving the widget right now. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Preview")
	EMinimapBackgroundSource GetActiveBackgroundSource() const;

	/** Human-readable state of the capture preview, for the Details panel. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Preview")
	FString GetPreviewStatusText() const;

	/** Error text from the last preview attempt, or empty. */
	const FString& GetLastPreviewError() const { return LastPreviewError; }

#if WITH_EDITOR
	/**
	 * Editor-only convenience: resize the box to encompass eligible level actors.
	 *
	 * This DOES iterate all actors, which is exactly what the performance rules forbid
	 * at runtime. It is safe here because it is a button an author presses in the editor,
	 * never part of the update path, and it is compiled out of packaged builds entirely.
	 *
	 * Filtering is controlled by the Minimap|Fit properties so distant helpers and sky
	 * geometry cannot silently produce enormous bounds.
	 */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Minimap",
		meta = (DisplayName = "Fit To Level Bounds"))
	void FitToLevelBounds();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** True when the actor passes the Minimap|Fit filters. */
	bool PassesFitFilter(const AActor* Actor, const FVector& ActorExtent) const;

	/** Created lazily; never a default subobject, so nothing is allocated when unused. */
	UPROPERTY(Transient)
	TObjectPtr<UMinimapCaptureComponent> CaptureComponent;

	bool bCalibrationAppliedThisPlay = false;

	/** Reported by the Details panel when a preview capture fails. */
	FString LastPreviewError;

	/** Set only while an editor preview is running; lets the capture exist in static mode. */
	bool bAllowCaptureInStaticMode = false;
};
