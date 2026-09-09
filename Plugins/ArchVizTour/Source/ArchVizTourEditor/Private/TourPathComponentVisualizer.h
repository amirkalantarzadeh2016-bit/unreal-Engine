// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComponentVisualizer.h"
#include "CoreMinimal.h"

class ATourPath;
class UTourSplineComponent;

/**
 * Viewport visualizer for ATourPath.
 *
 * Registered against UTourSplineComponent rather than USplineComponent so the stock spline
 * visualizer is left alone for every other spline in the project.
 *
 * Drags edit ATourPath::Points, which is the authoritative data, and then push the result into
 * the spline cache. Editing the spline component directly would be overwritten the next time
 * anything called SyncSplineFromPoints.
 */
class FTourPathComponentVisualizer : public FComponentVisualizer
{
public:
	// --- FComponentVisualizer --------------------------------------------
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	virtual bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	virtual bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
	virtual bool HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual void EndEditing() override;
	virtual TSharedPtr<SWidget> GenerateContextMenu() const override;

private:
	/** Which part of a point a hit proxy refers to. */
	enum class ESelectedElement : uint8
	{
		None,
		Point,
		ArriveTangent,
		LeaveTangent
	};

	/** The path currently being edited, resolved from the cached component property path. */
	ATourPath* GetEditedPath() const;

	/** Reset the selection, so a stale index cannot be applied to a different path. */
	void ClearSelection();

	/** Insert a point midway along the segment after the selected one. */
	void InsertPointAfterSelection();

	/** Delete the selected point, refusing to drop below the two a path needs. */
	void DeleteSelectedPoint();

	/** Set the selected point's type. */
	void SetSelectedPointType(TEnumAsByte<ESplinePointType::Type> NewType);

	/** Property path back to the component being visualized, which is how selection survives GC. */
	FComponentPropertyPath EditedComponentPath;

	/** Index of the selected point in ATourPath::Points, or INDEX_NONE. */
	int32 SelectedPointIndex = INDEX_NONE;

	/** Which handle of the selected point is being dragged. */
	ESelectedElement SelectedElement = ESelectedElement::None;
};

/**
 * Hit proxy for one handle of one tour point.
 *
 * HComponentVisProxy carries the component; the index and element identify which handle of it
 * was clicked, which is what lets a single visualizer own points and both tangents.
 */
struct HTourPointProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY()

	HTourPointProxy(const UActorComponent* InComponent, int32 InPointIndex, int32 InElement)
		: HComponentVisProxy(InComponent, HPP_Wireframe)
		, PointIndex(InPointIndex)
		, Element(InElement)
	{
	}

	/** Index into ATourPath::Points. */
	int32 PointIndex;

	/** Matches FTourPathComponentVisualizer::ESelectedElement. */
	int32 Element;
};
