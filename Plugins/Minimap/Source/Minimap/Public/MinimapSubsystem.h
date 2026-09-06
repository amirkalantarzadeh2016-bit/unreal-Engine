#pragma once

#include "CoreMinimal.h"
#include "MinimapTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MinimapSubsystem.generated.h"

class UMinimapTrackedComponent;
class UMinimapViewComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapCalibrationChanged, const FMinimapCalibration&, NewCalibration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapMarkerRegistryChanged, UMinimapTrackedComponent*, Marker);

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

	float TickInterval = 1.0f / 30.0f;
	float TimeAccumulator = 0.0f;

	bool bCalibrationValid = false;
	bool bCalibrationDirty = true;
	bool bMinimapEnabled = true;
};
