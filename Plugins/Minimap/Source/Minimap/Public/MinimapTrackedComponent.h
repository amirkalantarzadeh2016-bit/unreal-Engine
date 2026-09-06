#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MinimapTypes.h"
#include "MinimapTrackedComponent.generated.h"

class UMinimapSubsystem;
class UMinimapTrackedComponent;

/** Fired when this marker's projected position changed by more than the tolerances. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMinimapPositionUpdated,
	UMinimapTrackedComponent*, Marker, const FMinimapMarkerSnapshot&, Snapshot);

/** Fired only on an actual out-of-bounds state transition (hysteresis applied). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMinimapOutOfBoundsChanged,
	UMinimapTrackedComponent*, Marker, bool, bIsOutOfBounds, float, EdgeAngle);

/** Fired only when effective visibility flips (distance cull, height filter, policy, manual). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMinimapVisibilityChanged,
	UMinimapTrackedComponent*, Marker, bool, bIsVisible);

/**
 * Add this to any Actor to make it appear on the minimap. That is the entire setup:
 * it self-registers with UMinimapSubsystem on BeginPlay and unregisters on EndPlay.
 *
 * The component does NOT tick by default. The subsystem drives every marker in one
 * batched pass, which is both faster (one tick registration instead of N) and more
 * cache-friendly. bAllowIndividualTick exists only for markers that genuinely need a
 * different cadence from the rest of the system.
 */
UCLASS(ClassGroup = (Minimap), meta = (BlueprintSpawnableComponent, DisplayName = "Minimap Tracked"))
class MINIMAP_API UMinimapTrackedComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMinimapTrackedComponent();

	// ---------------------------------------------------------------------
	// Appearance
	// ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	FMinimapMarkerStyle Style;

	/** Higher priority draws on top and survives the per-view marker budget first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	int32 Priority = 0;

	/** Rotate the icon to match the owner's yaw (relative to the view). Off = always upright. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Style")
	bool bUseActorYaw = false;

	// ---------------------------------------------------------------------
	// Filtering
	// ---------------------------------------------------------------------

	/** Author-facing visibility switch. Use SetMarkerVisible() at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Filtering")
	bool bMarkerVisible = true;

	/** Planar world distance beyond which the marker is culled. 0 or less = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Filtering",
		meta = (ClampMin = "0.0", Units = "cm"))
	float MaxTrackDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Filtering")
	EMinimapOutOfBoundsPolicy OutOfBoundsPolicy = EMinimapOutOfBoundsPolicy::ClampToEdge;

	/** Hide the marker when it is too far above/below the view anchor (multi-storey levels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Filtering")
	bool bUseHeightFilter = false;

	/** Max world units the marker may be ABOVE the anchor before it is culled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Filtering",
		meta = (EditCondition = "bUseHeightFilter", ClampMin = "0.0", Units = "cm"))
	float HeightFilterAbove = 300.0f;

	/** Max world units the marker may be BELOW the anchor before it is culled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Filtering",
		meta = (EditCondition = "bUseHeightFilter", ClampMin = "0.0", Units = "cm"))
	float HeightFilterBelow = 300.0f;

	// ---------------------------------------------------------------------
	// Update tuning
	// ---------------------------------------------------------------------

	/** Skip re-projection until the owner has moved at least this far. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance",
		meta = (ClampMin = "0.0", Units = "cm"))
	float MoveTolerance = 2.0f;

	/** Skip re-projection until the owner's yaw has changed by at least this much. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance",
		meta = (ClampMin = "0.0", Units = "deg"))
	float AngleTolerance = 0.5f;

	/**
	 * Register with the subsystem automatically on BeginPlay. Turn this off for markers
	 * that should only appear conditionally, then call RegisterWithMinimap() yourself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance")
	bool bAutoRegister = true;

	/**
	 * Opt this single component into its own tick. Off by default and almost never needed -
	 * the subsystem's batched pass already covers every registered marker.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance")
	bool bAllowIndividualTick = false;

	// ---------------------------------------------------------------------
	// Delegates
	// ---------------------------------------------------------------------

	/**
	 * NOTE ON MULTI-VIEW: these component-level delegates report the PRIMARY view only,
	 * because a marker has a different position in every view. Consumers that care about
	 * a specific non-primary view should bind UMinimapViewComponent::OnMinimapViewUpdated
	 * instead, which carries the full per-view snapshot array.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapPositionUpdated OnMinimapPositionUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapOutOfBoundsChanged OnOutOfBoundsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapVisibilityChanged OnVisibilityChanged;

	// ---------------------------------------------------------------------
	// Public API
	// ---------------------------------------------------------------------

	/** Register with the world's minimap subsystem. Safe to call twice. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void RegisterWithMinimap();

	/** Unregister from the world's minimap subsystem. Safe to call when not registered. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void UnregisterFromMinimap();

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsRegisteredWithMinimap() const { return bRegistered; }

	/** Show/hide this marker. Broadcasts OnVisibilityChanged on the next update if it flips. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void SetMarkerVisible(bool bNewVisible);

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsMarkerVisible() const { return bMarkerVisible; }

	/**
	 * Last snapshot computed for the primary view. Check HasSnapshot() before trusting it.
	 * C++ callers use this const reference; the Blueprint-facing copy is below.
	 */
	const FMinimapMarkerSnapshot& GetLastSnapshot() const { return LastSnapshot; }

	/**
	 * Blueprint-facing copy of GetLastSnapshot(). Returns by value: UFUNCTION return values
	 * are copied regardless, so exposing the reference version would gain nothing.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap", meta = (DisplayName = "Get Last Snapshot"))
	FMinimapMarkerSnapshot BP_GetLastSnapshot() const { return LastSnapshot; }

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool HasSnapshot() const { return bHasSnapshot; }

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsOutOfBounds() const { return bHasSnapshot && LastSnapshot.bOutOfBounds; }

	/** Force the next batched pass to recompute this marker regardless of tolerances. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void MarkDirty() { bForceUpdate = true; }

	/** Cheap gate used by the subsystem before doing any per-marker work. */
	bool IsMarkerEnabled() const;

	// ---------------------------------------------------------------------
	// Subsystem-facing internals (not Blueprint-exposed)
	// ---------------------------------------------------------------------

	/**
	 * Returns true when the owner has moved/rotated past the tolerances since the last
	 * accepted update, and latches the new transform when it has.
	 */
	bool CheckAndConsumeDirty(const FVector& WorldLocation, float WorldYaw);

	/** Store a freshly computed snapshot and broadcast only the delegates whose state changed. */
	void ApplySnapshot(const FMinimapMarkerSnapshot& NewSnapshot);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Resolve the subsystem for this component's world. Null in CDOs and unloaded worlds. */
	UMinimapSubsystem* GetMinimapSubsystem() const;

	UPROPERTY(Transient)
	FMinimapMarkerSnapshot LastSnapshot;

	/** Last transform that passed the tolerance gate. */
	FVector LastTrackedLocation = FVector::ZeroVector;
	float LastTrackedYaw = 0.0f;

	bool bRegistered = false;
	bool bHasSnapshot = false;
	bool bForceUpdate = true;
};
