#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MinimapTypes.h"
#include "MinimapWidgetBase.generated.h"

class UCanvasPanel;
class UImage;
class UMaterialInstanceDynamic;
class UMinimapMarkerWidget;
class UMinimapSubsystem;
class UMinimapTrackedComponent;
class UMinimapViewComponent;
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

	/** Called after the native update each pass, for extra Blueprint-side presentation. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Minimap|Events")
	void OnMinimapUpdated(const TArray<FMinimapMarkerSnapshot>& Snapshots);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Bound to the view's update delegate; this is the widget's only per-update work. */
	UFUNCTION()
	void HandleViewUpdated(UMinimapViewComponent* View, const TArray<FMinimapMarkerSnapshot>& Snapshots);

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

	bool bMinimapExpanded = false;
};
