#pragma once

#include "CoreMinimal.h"
#include "MinimapCaptureTypes.h"
#include "MinimapTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MinimapSubsystem.generated.h"

class AMinimapBoundsVolume;
class UMinimapCaptureComponent;
class UMinimapTrackedComponent;
class UMinimapViewComponent;
class UTexture;
class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapCalibrationChanged, const FMinimapCalibration&, NewCalibration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapMarkerRegistryChanged, UMinimapTrackedComponent*, Marker);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapBackgroundTextureChanged, UTexture*, BackgroundTexture);

/**
 * Central minimap manager: owns the calibration, the marker registry, the view registry,
 * and the single batched update loop.
 *
 * Design notes:
 *  - This is the ONLY ticking object in the plugin. Markers and views are passive.
 *  - The registry is populated by components registering themselves on BeginPlay, so
 *    GetAllActorsOfClass never appears in the runtime path.
 *  - Registries hold TWeakObjectPtr and are compacted at the start of every pass, so a
 *    destroyed actor can never be dereferenced.
 *  - UTickableWorldSubsystem ticks every frame; the TickInterval accumulator below
 *    throttles the actual work to a fixed cadence (default 1/30 s).
 */
UCLASS(DisplayName = "Minimap Subsystem")
class MINIMAP_API UMinimapSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- USubsystem -------------------------------------------------------
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- FTickableGameObject ---------------------------------------------
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	/** Blueprint-friendly accessor. Returns null outside a valid world. */
	UFUNCTION(BlueprintPure, Category = "Minimap",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Minimap Subsystem"))
	static UMinimapSubsystem* Get(const UObject* WorldContextObject);

	// ---------------------------------------------------------------------
	// Calibration
	// ---------------------------------------------------------------------

	/**
	 * Install a new calibration. Rejected with a warning if invalid, leaving the previous
	 * calibration intact rather than pushing NaN into every widget transform.
	 * Returns true when accepted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Calibration")
	bool SetCalibration(const FMinimapCalibration& NewCalibration);

	/** C++ hot-path accessor; the Blueprint-facing copy is below. */
	const FMinimapCalibration& GetCalibration() const { return Calibration; }

	UFUNCTION(BlueprintPure, Category = "Minimap|Calibration", meta = (DisplayName = "Get Calibration"))
	FMinimapCalibration BP_GetCalibration() const { return Calibration; }

	UFUNCTION(BlueprintPure, Category = "Minimap|Calibration")
	bool HasValidCalibration() const { return bCalibrationValid; }

	/** Change only the zoom without rebuilding the rest of the calibration. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Calibration")
	void SetZoom(float NewZoom);

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapCalibrationChanged OnCalibrationChanged;

	// ---------------------------------------------------------------------
	// Registry
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Minimap|Registry")
	void RegisterMarker(UMinimapTrackedComponent* Marker);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Registry")
	void UnregisterMarker(UMinimapTrackedComponent* Marker);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Registry")
	void RegisterView(UMinimapViewComponent* View);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Registry")
	void UnregisterView(UMinimapViewComponent* View);

	/** The view whose results drive the tracked components' own delegates. May be null. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Registry")
	UMinimapViewComponent* GetPrimaryView() const;

	UFUNCTION(BlueprintPure, Category = "Minimap|Registry")
	int32 GetRegisteredMarkerCount() const { return Markers.Num(); }

	UFUNCTION(BlueprintPure, Category = "Minimap|Registry")
	int32 GetRegisteredViewCount() const { return Views.Num(); }

	/** Currently registered markers, invalid entries removed. Allocates - debug/tooling use. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Registry")
	TArray<UMinimapTrackedComponent*> GetRegisteredMarkers() const;

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapMarkerRegistryChanged OnMarkerRegistered;

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapMarkerRegistryChanged OnMarkerUnregistered;

	// ---------------------------------------------------------------------
	// Bounds selection
	// ---------------------------------------------------------------------

	/** Called by AMinimapBoundsVolume::BeginPlay. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Bounds")
	void RegisterBoundsVolume(AMinimapBoundsVolume* Volume);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Bounds")
	void UnregisterBoundsVolume(AMinimapBoundsVolume* Volume);

	/**
	 * Deterministic selection, in strict priority order:
	 *   1. a volume with bPreferredBounds set;
	 *   2. a volume whose BoundsSelectionTag matches RequiredBoundsTag;
	 *   3. the unique registered volume.
	 *
	 * Ambiguity (several equally eligible volumes) returns null and explains why, rather
	 * than picking an arbitrary first actor. OutReason is always filled in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Bounds")
	AMinimapBoundsVolume* ResolveAuthoritativeBounds(FString& OutReason) const;

	/** When set, only a volume with this BoundsSelectionTag is eligible in step 2. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Bounds")
	void SetRequiredBoundsTag(FName NewTag);

	UFUNCTION(BlueprintPure, Category = "Minimap|Bounds")
	FName GetRequiredBoundsTag() const { return RequiredBoundsTag; }

	// ---------------------------------------------------------------------
	// Background
	// ---------------------------------------------------------------------

	/** Called by the capture component once it is aligned. Replaces any prior provider. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Background")
	void RegisterBackgroundProvider(UMinimapCaptureComponent* Provider);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Background")
	void UnregisterBackgroundProvider(UMinimapCaptureComponent* Provider);

	UFUNCTION(BlueprintPure, Category = "Minimap|Background")
	UMinimapCaptureComponent* GetBackgroundProvider() const;

	/**
	 * The captured render target, or null when running in static-texture mode.
	 * Several widgets may share this one resource rather than each owning a capture.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Background")
	UTexture* GetBackgroundTexture() const;

	/**
	 * Coalesced background re-render within the CURRENT bounds. Call this after a batch
	 * of furniture edits. Does NOT move or resize the calibration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Background")
	void RequestBackgroundRefresh();

	/**
	 * Re-fit the authoritative bounds volume to the level, re-apply the calibration, then
	 * refresh. This DOES change the calibration - markers and image both move.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Background")
	void RefitBoundsAndRefresh();

	/** Re-read preset/override capture settings, re-apply, and refresh. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Background")
	void ApplyCaptureSettingsAndRefresh();

	/** Explicit readiness entry point for streamed or procedurally generated content. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Background")
	void NotifyMinimapContentReady();

	/**
	 * Publish an authored static texture as the background. Lets Static Texture mode use
	 * the same delivery path as capture, so both are previewable and the widget has one
	 * code path. Clears any active capture provider.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Background")
	void SetStaticBackgroundTexture(UTexture* StaticTexture);

	/** Fired when the background texture becomes available or is replaced. */
	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapBackgroundTextureChanged OnBackgroundTextureChanged;

	// ---------------------------------------------------------------------
	// Update control
	// ---------------------------------------------------------------------

	/** Seconds between batched updates. Default 1/30. Values <= 0 mean every frame. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Performance")
	void SetTickInterval(float NewInterval);

	UFUNCTION(BlueprintPure, Category = "Minimap|Performance")
	float GetTickInterval() const { return TickInterval; }

	/** Master switch for the whole system. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Performance")
	void SetMinimapEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Minimap|Performance")
	bool IsMinimapEnabled() const { return bMinimapEnabled; }

	/** Run one batched pass immediately, ignoring the interval and all dirty checks. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Performance")
	void ForceUpdate();

private:
	/**
	 * One marker's transform for a single update pass, with its dirty state already
	 * resolved.
	 *
	 * The dirty check LATCHES the transform it accepts, so it may only be consumed once
	 * per pass. Evaluating it inside the per-view loop meant that with two or more views
	 * the first view consumed the flag and every later view - including the primary one -
	 * saw the marker as clean and reused a stale snapshot. Resolving it once here and
	 * sharing the result fixes that, and also drops the per-marker transform reads from
	 * one per view to one per pass.
	 */
	struct FMinimapMarkerUpdate
	{
		UMinimapTrackedComponent* Marker = nullptr;
		FVector WorldLocation = FVector::ZeroVector;
		float WorldYaw = 0.0f;
		bool bDirty = false;
	};

	/** Resolve every registered marker's transform and dirty state, once per pass. */
	void GatherMarkerUpdates();

	/** Bound to the active provider's capture delegate; re-broadcasts to widgets. */
	UFUNCTION()
	void HandleBackgroundCaptured(UMinimapCaptureComponent* Capture, UTextureRenderTarget2D* RenderTarget);

	/** One batched pass across all active views. */
	void UpdateAllViews(bool bForceFullUpdate);

	/** Project every registered marker for one view. Returns true if anything changed. */
	bool UpdateMarkersForView(UMinimapViewComponent& View, bool bViewDirty, bool bIsPrimary, bool bForceFullUpdate);

	/** Project one marker. Pure given the context - no side effects on the component. */
	FMinimapMarkerSnapshot BuildSnapshot(
		UMinimapTrackedComponent& Marker,
		const UMinimapViewComponent& View,
		const FVector& WorldLocation,
		float WorldYaw) const;

	/** Drop dead weak references from both registries. */
	void CompactRegistries();

	UPROPERTY(Transient)
	FMinimapCalibration Calibration;

	/** Weak so a destroyed marker's owner is never kept alive by the registry. */
	TArray<TWeakObjectPtr<UMinimapTrackedComponent>> Markers;
	TArray<TWeakObjectPtr<UMinimapViewComponent>> Views;

	/** Scratch buffer reused across passes to keep the update allocation-free. */
	TArray<FMinimapMarkerSnapshot> SnapshotScratch;

	/**
	 * Marker transforms for the pass in flight. Raw pointers are safe because the array is
	 * built and consumed inside one synchronous call and cleared before returning, so no
	 * garbage collection can run while it holds them.
	 */
	TArray<FMinimapMarkerUpdate> MarkerUpdateScratch;

	float TickInterval = 1.0f / 30.0f;
	float TimeAccumulator = 0.0f;

	/** Weak: the registry must never keep a level actor alive across a transition. */
	TArray<TWeakObjectPtr<AMinimapBoundsVolume>> BoundsVolumes;

	TWeakObjectPtr<UMinimapCaptureComponent> BackgroundProvider;

	/** Authored texture published in Static Texture mode. Null when capture drives it. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture> StaticBackgroundTexture;

	FName RequiredBoundsTag = NAME_None;

	bool bCalibrationValid = false;
	bool bCalibrationDirty = true;
	bool bMinimapEnabled = true;
};
