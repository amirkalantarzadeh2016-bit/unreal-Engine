// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class UArchOpeningPieceSetComponent;

/**
 * Hit proxy for one piece, so a click in the 3D viewport routes back to the piece it landed on.
 *
 * Drawn at foreground priority so a piece box wins over the source mesh underneath it: the artist
 * clicks what they see highlighted, not the door behind it.
 */
struct HArchOpeningPieceProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

	HArchOpeningPieceProxy(const UActorComponent* InComponent, int32 InPieceIndex)
		: HComponentVisProxy(InComponent, HPP_Foreground)
		, PieceIndex(InPieceIndex)
	{
	}

	int32 PieceIndex;
};

/**
 * Draws the piece decomposition live, every frame, colour-coded by group, and turns viewport
 * clicks into group assignments.
 *
 * Live drawing is the point: the previous tool drew a one-shot debug-line snapshot on a button
 * press, so an artist had to guess, press, wait, and guess again. A visualizer redraws with the
 * viewport, so changing a group colour, hovering a list row or assigning a piece is visible
 * immediately.
 */
class FArchOpeningPieceSetVisualizer : public FComponentVisualizer
{
public:
	/** Total wireframe lines the optional per-triangle overlay is allowed to draw. */
	static constexpr int32 MaxWireframeLines = 40000;

	//~ Begin FComponentVisualizer Interface
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	//~ End FComponentVisualizer Interface

private:
	void DrawPieceWireframe(const UArchOpeningPieceSetComponent* PieceSet, const FTransform& ToWorld, FPrimitiveDrawInterface* PDI) const;
};
