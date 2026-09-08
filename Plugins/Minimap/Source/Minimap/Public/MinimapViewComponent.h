#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MinimapTypes.h"
#include "MinimapViewComponent.generated.h"

class APlayerController;
class UMinimapSubsystem;
class UMinimapViewComponent;

/** Fired once per batched update with every marker projected for THIS view. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMinimapViewUpdated,
	UMinimapViewComponent*, View, const TArray<FMinimapMarkerSnapshot>&, Snapshots);

/** Fired when the anchor position, view yaw or calibration changed for this view. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMinimapViewTransformChanged,
	UMinimapViewComponent*, View, FVector2D, Anchor, float, ViewYaw);

/** Fired when the actor this view follows is swapped (respawn, repossession, manual override). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMinimapViewActorChanged,
	UMinimapViewComponent*, View, AActor*, NewViewActor);

/**
 * Describes ONE minimap viewport: what it follows, which way is up, and how far it zooms.
 *
 * Put it on the PlayerController (recommended - it survives pawn death) or on a Pawn/HUD.
 * Several views can coexist against the same marker registry; the subsystem projects the
 * registry once per active view.
 *
 * View-actor resolution is re-evaluated every update and never permanently caches a pawn,
 * which is what makes respawn, repossession and view-target changes work without any
 * per-project glue code.
 */
UCLASS(ClassGroup = (Minimap), meta = (BlueprintSpawnableComponent, DisplayName = "Minimap View"))
class MINIMAP_API UMinimapViewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMinimapViewComponent();

	// ---------------------------------------------------------------------
	// View configuration
	// ---------------------------------------------------------------------

	/** Rotating map (default) or north-up. Drives ViewYaw, nothing else. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|View")
	EMinimapOrientationMode OrientationMode = EMinimapOrientationMode::RotatingMap;

	/** Viewer-centred (default) or pinned to the calibrated map centre. Drives Anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|View")
	EMinimapAnchorMode AnchorMode = EMinimapAnchorMode::ViewerCentered;

	/**
	 * Added to ViewYaw before the material's rotation is computed. This is the value the
	 * legacy Blueprint left on an UNCONNECTED pin, so it silently contributed 0. Use it to
	 * align a map texture whose "up" is not world north.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|View", meta = (Units = "deg"))
	float MapYawOffset = 0.0f;

	/**
	 * Flip the material rotation direction without re-authoring M_Minimap.
	 *
	 * The legacy graph fed the material +Yaw/360 but counter-rotated the north indicator
	 * with -Yaw. Those two are only consistent if M_Minimap negates internally. If the map
	 * and the compass turn opposite ways in-game, toggle this rather than editing either.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|View")
	bool bNegateMapRotation = false;

	/** Per-view zoom multiplier applied on top of the calibration's Zoom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|View",
		meta = (ClampMin = "0.01", UIMin = "0.25", UIMax = "8.0"))
	float ZoomMultiplier = 1.0f;

	/**
	 * Explicit actor to follow. Leave null to auto-resolve from the owner
	 * (controller -> pawn -> view target -> first local player controller's pawn).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|View")
	TWeakObjectPtr<AActor> ExplicitViewActor;

	/** Marks this as the view whose results feed the tracked components' own delegates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|View")
	bool bPrimaryView = true;

	// ---------------------------------------------------------------------
	// Out-of-bounds tuning
	// ---------------------------------------------------------------------

	/** Shape magnitude at which a marker becomes out of bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Bounds",
		meta = (ClampMin = "0.1"))
	float OutOfBoundsEnterThreshold = 1.0f;

	/** Shape magnitude below which it returns in bounds. Must be <= the enter threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Bounds",
		meta = (ClampMin = "0.1"))
	float OutOfBoundsExitThreshold = 0.98f;

	// ---------------------------------------------------------------------
	// Performance
	// ---------------------------------------------------------------------

	/** Anchor movement below this does not count as a view change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance",
		meta = (ClampMin = "0.0", Units = "cm"))
	float AnchorMoveTolerance = 1.0f;

	/** View yaw change below this does not count as a view change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance",
		meta = (ClampMin = "0.0", Units = "deg"))
	float ViewAngleTolerance = 0.25f;

	/**
	 * Maximum markers projected for this view per update. 0 = unlimited.
	 * When exceeded, the highest-Priority markers survive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance",
		meta = (ClampMin = "0"))
	int32 MaxMarkersPerView = 0;

	/** Register with the subsystem automatically on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Performance")
	bool bAutoRegister = true;

	// ---------------------------------------------------------------------
	// Delegates
	// ---------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapViewUpdated OnMinimapViewUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapViewTransformChanged OnViewTransformChanged;

	UPROPERTY(BlueprintAssignable, Category = "Minimap|Events")
	FOnMinimapViewActorChanged OnViewActorChanged;

	// ---------------------------------------------------------------------
	// Smoothing: compass "float" and zoom easing
	//
	// Both run on this component's own tick, which enables itself only while a value is
	// still settling and disables again once it arrives - so the resting cost is zero and
	// the system keeps its "no per-frame work" property.
	// ---------------------------------------------------------------------

	/** Let the compass lag behind the view, giving the indicators a floating feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Compass")
	bool bSmoothCompass = true;

	/**
	 * Higher converges faster and feels stiffer; lower floats more. 6-10 reads as a
	 * pleasant lag, below 3 feels sluggish.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Compass",
		meta = (EditCondition = "bSmoothCompass", ClampMin = "0.1", UIMin = "1.0", UIMax = "20.0"))
	float CompassInterpSpeed = 7.0f;

	/** Below this the compass snaps, so it cannot creep forever at sub-pixel amounts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Compass",
		meta = (EditCondition = "bSmoothCompass", ClampMin = "0.001", Units = "deg"))
	float CompassSettleTolerance = 0.05f;

	/** Ease zoom changes instead of snapping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Zoom")
	bool bSmoothZoom = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Zoom",
		meta = (EditCondition = "bSmoothZoom", ClampMin = "0.1", UIMin = "1.0", UIMax = "20.0"))
	float ZoomInterpSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Zoom",
		meta = (ClampMin = "0.05", UIMin = "0.1", UIMax = "4.0"))
	float MinZoomMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Zoom",
		meta = (ClampMin = "0.05", UIMin = "0.5", UIMax = "16.0"))
	float MaxZoomMultiplier = 4.0f;

	/** Multiplicative step per ZoomIn/ZoomOut call. 1.25 = 25% per press. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Zoom",
		meta = (ClampMin = "1.01", UIMin = "1.05", UIMax = "2.0"))
	float ZoomStep = 1.25f;

	/**
	 * Compass angle in degrees, already smoothed. Feed this straight into
	 * SetRenderTransformAngle. Equals GetCompassAngle() when smoothing is off.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Compass")
	float GetSmoothedCompassAngle() const { return bSmoothCompass ? SmoothedCompassAngle : GetCompassAngle(); }

	/**
	 * Screen angle for one cardinal indicator, in degrees clockwise from up, using the
	 * smoothed compass. Index 0 = North, 1 = East, 2 = South, 3 = West.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Compass")
	float GetCardinalScreenAngle(int32 CardinalIndex) const;

	/**
	 * Position for a cardinal indicator on a ring of the given radius, relative to the
	 * ring centre, in slate units. Y is screen-down, ready for a canvas slot offset.
	 */
	UFUNCTION(BlueprintPure, Category = "Minimap|Compass")
	FVector2D GetCardinalRingOffset(int32 CardinalIndex, float RingRadius) const;

	/** Set the zoom target. Eases toward it when bSmoothZoom is on, else snaps. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Zoom")
	void SetZoomTarget(float NewZoom);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Zoom")
	void ZoomIn();

	UFUNCTION(BlueprintCallable, Category = "Minimap|Zoom")
	void ZoomOut();

	/** 0 = fully zoomed out, 1 = fully zoomed in. Handy for driving a slider. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Zoom")
	float GetZoomAlpha() const;

	/** Set zoom from a normalized 0..1 slider value. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Zoom")
	void SetZoomAlpha(float Alpha);

	UFUNCTION(BlueprintPure, Category = "Minimap|Zoom")
	float GetZoomTarget() const { return TargetZoomMultiplier; }

	// ---------------------------------------------------------------------
	// Public API
	// ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Minimap|View")
	void RegisterView();

	UFUNCTION(BlueprintCallable, Category = "Minimap|View")
	void UnregisterView();

	/**
	 * Called by the widget layer so the subsystem can skip a view whose widget is not on
	 * screen. This is the "skip updates when not rendered" requirement.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|View")
	void SetViewRenderingEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	bool IsViewActive() const;

	UFUNCTION(BlueprintCallable, Category = "Minimap|View")
	void SetOrientationMode(EMinimapOrientationMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "Minimap|View")
	void SetZoomMultiplier(float NewZoom);

	/** Override the followed actor. Pass null to fall back to automatic resolution. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|View")
	void SetExplicitViewActor(AActor* NewViewActor);

	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	AActor* GetCurrentViewActor() const { return CachedViewActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	FVector2D GetAnchor() const { return Anchor; }

	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	float GetViewYaw() const { return ViewYaw; }

	/** Map rotation in turns, [0, 1), ready for the material's Rotator node. */
	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	float GetMapRotationTurns() const;

	/** North-indicator angle in degrees, = -ViewYaw. */
	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	float GetCompassAngle() const;

	/** Normalized position of the view actor against the FIXED map (anchor = WorldCenter). */
	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	FVector2D GetViewerNormalizedOnFixedMap() const { return ViewerNormalizedOnFixedMap; }

	/** The [-0.5, 0.5] pair for M_Minimap's PlayerX / PlayerY. */
	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	FVector2D GetMaterialPlayerParams() const;

	/** C++ hot-path accessors. The Blueprint-facing copies are below. */
	const TArray<FMinimapMarkerSnapshot>& GetSnapshots() const { return Snapshots; }
	const FMinimapProjectionContext& GetProjectionContext() const { return ProjectionContext; }

	UFUNCTION(BlueprintPure, Category = "Minimap|View", meta = (DisplayName = "Get Snapshots"))
	TArray<FMinimapMarkerSnapshot> BP_GetSnapshots() const { return Snapshots; }

	UFUNCTION(BlueprintPure, Category = "Minimap|View", meta = (DisplayName = "Get Projection Context"))
	FMinimapProjectionContext BP_GetProjectionContext() const { return ProjectionContext; }

	UFUNCTION(BlueprintPure, Category = "Minimap|View")
	bool IsProjectionValid() const { return ProjectionContext.bValid; }

	// ---------------------------------------------------------------------
	// Subsystem-facing internals
	// ---------------------------------------------------------------------

	/**
	 * Re-resolve the view actor, anchor and yaw, and rebuild the cached projection context.
	 * Returns true when anything changed enough to require re-projecting every marker.
	 */
	bool RefreshViewState(const FMinimapCalibration& Calibration, bool bCalibrationChanged);

	TArray<FMinimapMarkerSnapshot>& GetMutableSnapshots() { return Snapshots; }

	/** Broadcast OnMinimapViewUpdated with the current snapshot array. */
	void BroadcastViewUpdated();

	/**
	 * Pure, world-free view-actor resolution, in priority order:
	 *   explicit override -> controller's pawn -> controller's view target -> owning pawn.
	 * Split out as a static so the respawn/repossession behaviour is unit-testable.
	 */
	static AActor* ResolveViewActorFromCandidates(
		AActor* ExplicitOverride,
		APawn* ControlledPawn,
		AActor* ViewTarget,
		APawn* OwningPawn);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Turn the tick on while something is still easing, off once everything has settled. */
	void UpdateSmoothingTickState();

private:
	/** Full resolution path including the world lookups the static helper cannot do. */
	AActor* ResolveViewActor();

	/** Owner's player controller, or the first LOCAL player controller in the world. */
	APlayerController* ResolveOwningPlayerController() const;

	/** Control rotation where available, otherwise the actor's own yaw. */
	float ResolveViewYaw(AActor* ViewActor) const;

	UPROPERTY(Transient)
	TArray<FMinimapMarkerSnapshot> Snapshots;

	UPROPERTY(Transient)
	FMinimapProjectionContext ProjectionContext;

	/** Never a strong reference: a dead pawn must be collectable. */
	TWeakObjectPtr<AActor> CachedViewActor;

	FVector2D Anchor = FVector2D::ZeroVector;
	float AnchorZ = 0.0f;
	float ViewYaw = 0.0f;

	/** Viewer position on the fixed map - what M_Minimap's PlayerX/PlayerY consume. */
	FVector2D ViewerNormalizedOnFixedMap = FVector2D::ZeroVector;

	bool bRegistered = false;
	bool bRenderingEnabled = true;
	bool bStateInitialized = false;

	/** Compass angle currently displayed; chases GetCompassAngle(). */
	float SmoothedCompassAngle = 0.0f;
	bool bCompassInitialized = false;

	/** Zoom currently displayed; chases TargetZoomMultiplier. */
	float TargetZoomMultiplier = 1.0f;
};
