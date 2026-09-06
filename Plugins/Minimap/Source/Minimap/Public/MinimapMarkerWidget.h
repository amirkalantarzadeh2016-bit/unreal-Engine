#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MinimapTypes.h"
#include "MinimapMarkerWidget.generated.h"

class UImage;

/**
 * One pooled marker icon on the minimap canvas.
 *
 * Reparent a WBP to this, optionally add an Image named "IconImage", and the C++ side will
 * drive the brush, tint and rotation. Override OnMarkerUpdated in Blueprint for anything
 * fancier (health rings, labels, pulsing) - the native implementation still runs first.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Minimap Marker Widget"))
class MINIMAP_API UMinimapMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Optional. When present the native implementation drives its brush, tint and angle. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	/**
	 * Rotate the icon by the snapshot's EdgeAngle while clamped to the edge, so a plain
	 * arrow texture points outward at the target. Ignored while in bounds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bRotateToEdgeAngleWhenOutOfBounds = true;

	/** Push a freshly computed snapshot into this widget. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Minimap")
	void OnMarkerUpdated(const FMinimapMarkerSnapshot& Snapshot);
	virtual void OnMarkerUpdated_Implementation(const FMinimapMarkerSnapshot& Snapshot);

	/** The marker this widget is currently bound to. May be null while pooled. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UMinimapTrackedComponent* GetBoundMarker() const { return BoundMarker.Get(); }

	/** Called when the pool hands this widget a new marker (or null on release). */
	UFUNCTION(BlueprintNativeEvent, Category = "Minimap")
	void OnMarkerBound(UMinimapTrackedComponent* Marker);
	virtual void OnMarkerBound_Implementation(UMinimapTrackedComponent* Marker) {}

	void SetBoundMarker(UMinimapTrackedComponent* Marker);

private:
	TWeakObjectPtr<UMinimapTrackedComponent> BoundMarker;
};
