#pragma once

#include "Components/SceneCaptureComponent2D.h"
#include "CoreMinimal.h"
#include "MinimapCaptureTypes.h"
#include "MinimapTypes.h"
#include "MinimapCaptureComponent.generated.h"

class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMinimapBackgroundCaptured,
	UMinimapCaptureComponent*, Capture, UTextureRenderTarget2D*, RenderTarget);

/**
 * Renders the level top-down into a render target that the minimap widget displays as its
 * background, replacing a hand-authored static map image.
 *
 * Created lazily by AMinimapBoundsVolume - it is never a default subobject, so nothing is
 * allocated when capture mode is off or on a dedicated server.
 *
 * ALIGNMENT: the capture is driven by the SAME FMinimapCalibration the markers use. The
 * orthographic width covers the calibration's effective extent and the yaw comes from
 * UMinimapFunctionLibrary::ComputeCaptureYaw, so the axis convention is applied exactly
 * once - here, to the camera - and never again to the image.
 *
 * COST: capture is explicit. bCaptureEveryFrame and bCaptureOnMovement are forced off and
 * CaptureScene() is called only from a coalesced refresh. Marker updates never touch this.
 */
UCLASS(ClassGroup = (Minimap), meta = (BlueprintSpawnableComponent, DisplayName = "Minimap Capture"))
class MINIMAP_API UMinimapCaptureComponent : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UMinimapCaptureComponent();

	/** Portable capture configuration. Usually pushed from the bounds volume's preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture")
	FMinimapCaptureSettings Settings;

	/** Level-specific actors hidden from the capture only (roofs, ceilings, helpers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility")
	TArray<TSoftObjectPtr<AActor>> ExcludedActors;

	/** Level-specific actors rendered when Settings.bUseShowOnlyList is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility")
	TArray<TSoftObjectPtr<AActor>> IncludedActors;

	/** Fired after each successful capture. Widgets bind this to pick up the texture. */
	UPROPERTY(BlueprintAssignable, Category = "Minimap|Capture|Events")
	FOnMinimapBackgroundCaptured OnBackgroundCaptured;

	// ---------------------------------------------------------------------
	// API
	// ---------------------------------------------------------------------

	/**
	 * Point the camera at the calibrated area and size the render target to match.
	 * Safe to call repeatedly; the render target is only rebuilt when its required
	 * dimensions actually change. Returns false and fills OutError when the calibration
	 * cannot be captured (invalid bounds, or a mirrored axis convention).
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Capture")
	bool ApplyCalibration(const FMinimapCalibration& Calibration, FString& OutError);

	/**
	 * Ask for a re-render. Requests inside Settings.RefreshCoalesceSeconds collapse into a
	 * single capture, so a batch of furniture edits costs one render rather than one each.
	 * This is the function a furniture system should call after an edit.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Capture")
	void RequestBackgroundRefresh();

	/** Capture right now, bypassing coalescing. Prefer RequestBackgroundRefresh(). */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Capture")
	bool RefreshBackgroundImmediate();

	/** Re-read Settings (clamping them), re-apply, and request a refresh. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Capture")
	void ApplyCaptureSettingsAndRefresh();

	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	UTextureRenderTarget2D* GetMinimapRenderTarget() const { return MinimapRenderTarget; }

	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	bool HasCapturedBackground() const { return bHasCaptured && MinimapRenderTarget != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	bool IsCaptureEnabled() const { return Settings.BackgroundSource == EMinimapBackgroundSource::SceneCapture; }

	/** Last error from ApplyCalibration / capture, or empty. Surfaced by validation. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	const FString& GetLastCaptureError() const { return LastCaptureError; }

	/** World Z the camera will use, given the current settings and calibration. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Capture")
	float ResolveCaptureHeight(const FMinimapCalibration& Calibration) const;

	/** Free the render target and stop all timers. Called on EndPlay and teardown. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Capture")
	void ReleaseCaptureResources();

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Create or resize the render target. Returns false if the size is unusable. */
	bool EnsureRenderTarget(const FIntPoint& DesiredSize);

	/** Manual exposure and no post-process noise, for a readable map. */
	void ApplyVisualDefaults();

	/** Rebuild HiddenActors / ShowOnlyActors. One actor iteration, only on refresh. */
	void ApplyVisibilityFilters();

	/** Tallest eligible actor top inside the bounds, for AutoAboveGeometry. */
	float ScanGeometryTop(const FMinimapCalibration& Calibration) const;

	void HandleCoalescedRefresh();
	void UpdatePeriodicTimer();

	/** True on a dedicated server, where no capture resources may be allocated. */
	bool IsDedicatedServerWorld() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MinimapRenderTarget;

	/** Last calibration successfully applied; reused by refreshes and height scans. */
	FMinimapCalibration AppliedCalibration;

	FTimerHandle CoalesceTimerHandle;
	FTimerHandle PeriodicTimerHandle;

	FString LastCaptureError;

	bool bCalibrationApplied = false;
	bool bHasCaptured = false;
	bool bRefreshQueued = false;
};
