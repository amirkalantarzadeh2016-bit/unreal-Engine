#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MinimapCaptureTypes.h"
#include "MinimapTypes.h"
#include "MinimapWidgetBase.generated.h"

class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UMinimapMarkerWidget;
class UMinimapSubsystem;
class UMinimapTrackedComponent;
class UMinimapViewComponent;
class UTexture;
class UTextureRenderTarget2D;
class UWidget;

/**
 * Drop-in replacement base class for the existing WBP_Minimap.
 *
 * Reparent WBP_Minimap to this class and it keeps working: the widget-bound names match
 * the existing hierarchy (Background, North_Container), the material parameter names are
 * unchanged (PlayerX, PlayerY, MapRotation), and the pop animation events are preserved
 * through HandlePopEffect.
 *
 * What changes underneath:
 *  - GetDynamicMaterial() is called ONCE and cached, not every Tick.
 *  - Material scalars are written only when they move past a tolerance.
 *  - The widget does not Tick at all; it reacts to the view's update delegate.
 *  - Markers are pooled UMG widgets on a canvas instead of extra material parameters.
 *
 * WHY HandlePopEffect INSTEAD OF PlayPopEffect/StopPopEffect:
 * WBP_Minimap already declares Blueprint custom events with those exact names. Declaring
 * matching BlueprintImplementableEvents in C++ would collide on reparent and fail to
 * compile. HandlePopEffect(bool) is a distinct name, so the existing events survive
 * untouched - implement it in the graph and route it to them with a single Branch.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Minimap Widget Base"))
class MINIMAP_API UMinimapWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------
	// Bound widgets - names match the existing WBP_Minimap hierarchy
	// ---------------------------------------------------------------------

	/** The map image whose material carries PlayerX / PlayerY / MapRotation. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Background;

	/** Compass ring / north indicator. Counter-rotated to -ViewYaw. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> North_Container;

	/**
	 * Cardinal indicators. All optional - bind whichever you add.
	 *
	 * "North_Container" is the existing binding and keeps working unchanged. Add
	 * South/East/West_Container to get the full set. The base class drives their rotation
	 * (and optionally their position on a ring) from the view's SMOOTHED compass angle, so
	 * they float rather than snapping.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Compass", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> South_Container;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Compass", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> East_Container;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap|Compass", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> West_Container;

	/**
	 * Move the indicators around a ring as the compass turns, instead of only spinning
	 * them in place. Requires each indicator to sit in a Canvas Panel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Compass")
	bool bOrbitCardinalIndicators = false;

	/** Ring radius in slate units, used when orbiting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Compass",
		meta = (EditCondition = "bOrbitCardinalIndicators", ClampMin = "0.0"))
	float CardinalRingRadius = 110.0f;

	/**
	 * Keep the icons upright while they orbit. Off means each icon also spins, which is
	 * what you want for an arrow and not for a letter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Compass",
		meta = (EditCondition = "bOrbitCardinalIndicators"))
	bool bKeepCardinalIconsUpright = true;

	/** Canvas that pooled marker widgets are parented to. Add one named "MarkerCanvas". */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap", meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	// ---------------------------------------------------------------------
	// Material compatibility
	// ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material")
	FName PlayerXParameterName = TEXT("PlayerX");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material")
	FName PlayerYParameterName = TEXT("PlayerY");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material")
	FName MapRotationParameterName = TEXT("MapRotation");

	/** Write a scalar only once it has moved at least this much. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material",
		meta = (ClampMin = "0.0"))
	float MaterialUpdateTolerance = 0.0005f;

	/** Drive the material's pan/rotation parameters at all. Off for a plain static image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material")
	bool bDriveMaterialParameters = true;

	/**
	 * Texture parameter on M_Minimap that receives the automatically captured map.
	 * Only used in Automatic Scene Capture mode; the static-texture path never touches it,
	 * so existing materials without this parameter are unaffected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material")
	FName MapTextureParameterName = TEXT("MapTexture");

	/** How a captured background reaches the Background image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material")
	EMinimapBackgroundApplyMode BackgroundApplyMode = EMinimapBackgroundApplyMode::Automatic;

	/** Counter-rotate North_Container to -ViewYaw each update. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material")
	bool bDriveNorthIndicator = true;

	// ---------------------------------------------------------------------
	// Marker pool
	// ---------------------------------------------------------------------

	/** Widget class spawned for markers that do not specify their own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Markers")
	TSubclassOf<UMinimapMarkerWidget> DefaultMarkerWidgetClass;

	/** Widgets pre-created on construct, to avoid spawning during the first updates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Markers",
		meta = (ClampMin = "0"))
	int32 InitialMarkerPoolSize = 16;

	/** Hard ceiling on pooled widgets. 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Markers",
		meta = (ClampMin = "0"))
	int32 MaxMarkerWidgets = 64;

	/** Spawn and place marker widgets. Off to consume the snapshots in Blueprint instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Markers")
	bool bManageMarkerWidgets = true;

	/**
	 * Fallback map size in slate units, used only before the canvas has a cached geometry
	 * (i.e. on the very first update, before the first paint).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Markers")
	FVector2D FallbackMapSize = FVector2D(256.0, 256.0);

	// ---------------------------------------------------------------------
	// API
	// ---------------------------------------------------------------------

	/**
	 * Bind this widget to a view. Call with an explicit view for split-screen; otherwise
	 * NativeConstruct auto-binds to the owning player's view.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void InitializeMinimap(UMinimapViewComponent* InView);

	/** Unbind from the current view and release every pooled marker widget. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void ShutdownMinimap();

	UFUNCTION(BlueprintPure, Category = "Minimap")
	UMinimapViewComponent* GetMinimapView() const { return BoundView.Get(); }

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsMinimapInitialized() const { return BoundView.IsValid(); }

	/** The cached MID for Background. Null until the widget is constructed. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UMaterialInstanceDynamic* GetCachedMapMaterial() const { return CachedMapMID; }

	/**
	 * Display a background texture (normally the capture render target).
	 * Safe to call with null, which restores whatever the material/brush already had.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void ApplyBackgroundTexture(UTexture* BackgroundTexture);

	/**
	 * Composite the final minimap view into the plugin's own render target: the map is
	 * drawn once, panned/zoomed/rotated to the current view, on a black background.
	 *
	 * This is what makes tiling impossible - the plugin places a single quad rather than
	 * asking a sampler to fetch UVs beyond [0,1]. Everything the quad does not cover stays
	 * at the clear colour, so outside the bounds is solid black by construction.
	 *
	 * Called automatically each update in Composited View mode.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	bool UpdateCompositedBackground(UMinimapViewComponent* View);

	/** The composited view render target, or null when the mode is not active. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UTextureRenderTarget2D* GetCompositedRenderTarget() const { return CompositedRenderTarget; }

	/** Edge length of the composited render target. Defaults to the widget's own size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Material",
		meta = (ClampMin = "64", ClampMax = "4096"))
	int32 CompositedResolution = 512;

	/** The background texture currently applied, or null in static-texture mode. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UTexture* GetAppliedBackgroundTexture() const { return AppliedBackgroundTexture; }

	/**
	 * Expand/collapse the minimap. Calls HandlePopEffect only on an actual state change,
	 * so the animation is never restarted from the top while already expanded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void SetMinimapExpanded(bool bExpanded);

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsMinimapExpanded() const { return bMinimapExpanded; }

	/**
	 * Implement in WBP_Minimap: call the existing PlayPopEffect when bPlayForward is true,
	 * StopPopEffect otherwise. One Branch node, and the existing animation setup is kept.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Minimap|Effects")
	void HandlePopEffect(bool bPlayForward);

	/**
	 * Drive every bound cardinal indicator from the view's smoothed compass angle.
	 * Called automatically each frame while smoothing is active; safe to call manually.
	 */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Compass")
	void UpdateCardinalIndicators();

	/** Smoothed compass angle, or 0 with no view bound. Bind this in Blueprint if you
	 *  would rather drive the indicators yourself. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Compass")
	float GetSmoothedCompassAngle() const;

	// --- Zoom passthroughs, so Blueprint can bind buttons without reaching for the view --

	UFUNCTION(BlueprintCallable, Category = "Minimap|Zoom")
	void ZoomIn();

	UFUNCTION(BlueprintCallable, Category = "Minimap|Zoom")
	void ZoomOut();

	UFUNCTION(BlueprintCallable, Category = "Minimap|Zoom")
	void SetZoomAlpha(float Alpha);

	UFUNCTION(BlueprintPure, Category = "Minimap|Zoom")
	float GetZoomAlpha() const;

	/** Called after the native update each pass, for extra Blueprint-side presentation. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Minimap|Events")
	void OnMinimapUpdated(const TArray<FMinimapMarkerSnapshot>& Snapshots);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Only drives the compass smoothing. Everything else - markers, background, material
	 * scalars - remains event-driven off the subsystem's batched update.
	 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Apply angle (and optionally ring position) to one indicator. */
	void ApplyCardinalTransform(UWidget* Indicator, int32 CardinalIndex);

	/** Bound to the view's update delegate; this is the widget's only per-update work. */
	UFUNCTION()
	void HandleViewUpdated(UMinimapViewComponent* View, const TArray<FMinimapMarkerSnapshot>& Snapshots);

	/** Bound to the subsystem's background delegate. Runs on capture, not per frame. */
	UFUNCTION()
	void HandleBackgroundTextureChanged(UTexture* BackgroundTexture);

	/** Locate (or create) the owning local player's view component. */
	UMinimapViewComponent* ResolveViewComponent();

	/** Write PlayerX / PlayerY / MapRotation, skipping values inside the tolerance. */
	void UpdateMaterialParameters(const UMinimapViewComponent& View);

	/** Position, show and hide pooled marker widgets for this pass. */
	void UpdateMarkerWidgets(const TArray<FMinimapMarkerSnapshot>& Snapshots);

	/** Fetch a pooled widget by index, growing the pool on demand. */
	UMinimapMarkerWidget* AcquireMarkerWidget(int32 Index, const FMinimapMarkerSnapshot& Snapshot);

	/** Current canvas size in slate units, or FallbackMapSize before the first paint. */
	FVector2D GetMapWidgetSize() const;

private:
	/** Set a scalar only if it moved past MaterialUpdateTolerance. */
	void SetScalarIfChanged(FName ParameterName, float NewValue, float& CachedValue, bool& bCacheInitialized);

	TWeakObjectPtr<UMinimapViewComponent> BoundView;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CachedMapMID;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMinimapMarkerWidget>> MarkerPool;

	// Last values written to the material, so identical writes can be skipped.
	float CachedPlayerX = 0.0f;
	float CachedPlayerY = 0.0f;
	float CachedMapRotation = 0.0f;
	bool bPlayerXInitialized = false;
	bool bPlayerYInitialized = false;
	bool bMapRotationInitialized = false;

	float CachedCompassAngle = 0.0f;
	bool bCompassInitialized = false;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> AppliedBackgroundTexture;

	/** Owned by the widget so it dies with it; recreated on demand. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CompositedRenderTarget;

	/** Source map the compositor draws from (the capture RT or the static texture). */
	UPROPERTY(Transient)
	TObjectPtr<UTexture> CompositorSourceTexture;

	/** Set once the material is known to lack MapTextureParameterName, to stop log spam. */
	bool bWarnedMissingTextureParameter = false;

	bool bMinimapExpanded = false;
};
