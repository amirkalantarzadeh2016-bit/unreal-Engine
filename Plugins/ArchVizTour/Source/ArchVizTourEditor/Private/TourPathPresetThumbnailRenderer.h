// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"

#include "TourPathPresetThumbnailRenderer.generated.h"

/**
 * Draws a top-down sketch of a path preset's curve as its Content Browser thumbnail.
 *
 * A path preset has no mesh and no material, so the default thumbnail is a blank icon and a
 * folder of them is unreadable. A 2D trace of the actual curve, auto-fitted to the tile, tells
 * an arc from an orbit from a hand-authored walkthrough at a glance.
 */
UCLASS()
class UTourPathPresetThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()

public:
	// --- UThumbnailRenderer ----------------------------------------------
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* Viewport, FCanvas* Canvas, bool bAdditionalViewFamily) override;
};
