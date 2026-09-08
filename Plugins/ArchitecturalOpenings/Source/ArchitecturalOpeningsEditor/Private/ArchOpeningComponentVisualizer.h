// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentVisualizer.h"

class UArchOpeningComponent;

/**
 * Draws the setup information an artist needs to judge a configured opening at a glance:
 * the hinge marker and axis, the reference outside arrow, the swing arc with its angular range,
 * the slide path and its end point, the proximity trigger box, and the leaf bounds used for the
 * obstruction query.
 */
class FArchOpeningComponentVisualizer : public FComponentVisualizer
{
public:
	//~ Begin FComponentVisualizer Interface
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	//~ End FComponentVisualizer Interface

private:
	void DrawHinged(const UArchOpeningComponent* Opening, const FTransform& Frame, FPrimitiveDrawInterface* PDI) const;
	void DrawSliding(const UArchOpeningComponent* Opening, const FTransform& Frame, FPrimitiveDrawInterface* PDI) const;
	void DrawShared(const UArchOpeningComponent* Opening, const FTransform& Frame, FPrimitiveDrawInterface* PDI) const;
};
