// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComponentVisualizer.h"
#include "CoreMinimal.h"

class AArchSkyDirector;
class FPrimitiveDrawInterface;
class FSceneView;
class UArchSkySubsystem;

/**
 * Draws the sun study in the viewport whenever an ArchSky Director is selected.
 *
 * What it draws, and why each element earns its place:
 *
 *   - Three reference sun-path arcs (summer solstice, equinox, winter solstice). Together
 *     they bound the sun's entire annual range, which is the envelope an architect needs
 *     to see when placing a shading device.
 *   - The current date's arc, highlighted, with labelled hour markers along it.
 *   - The sun's current position as a sphere, with a line back to the Director.
 *   - Two compass arrows in different colours: TRUE north and the PLAN's north. If they
 *     disagree, the north offset is set - and if they should disagree but do not, the
 *     shadow study is silently wrong. This is the whole reason for drawing both.
 *   - Optionally the analemma: the figure-eight the sun traces when sampled at the same
 *     clock time across a year. Off by default because it is 365 extra solar solves.
 *
 * ARCH NOTE: this is a FComponentVisualizer registered against USceneComponent rather than
 * an actor-level drawing hook, because component visualisers are the only mechanism the
 * editor gives us that draws with the correct depth priority and participates properly in
 * hit-proxy selection. The visualiser is registered for the Director's root component.
 */
class FArchSkyDirectorVisualizer : public FComponentVisualizer
{
public:
	//~ Begin FComponentVisualizer
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View,
		FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport,
		const FSceneView* View, FCanvas* Canvas) override;
	//~ End FComponentVisualizer

	/** Console-settable radius, in centimetres, of the drawn celestial sphere. */
	static float GetSphereRadius();

private:
	/** Resolves the Director that owns a component, or null when it is not one of ours. */
	static const AArchSkyDirector* GetDirector(const UActorComponent* Component);

	/** Draws one full-day sun path for a given day of year. */
	void DrawSunPathForDay(const AArchSkyDirector& Director, const UArchSkySubsystem& Subsystem,
		int32 DayOfYear, const FLinearColor& AboveHorizonColour, const FLinearColor& BelowHorizonColour,
		float Thickness, FPrimitiveDrawInterface* PDI) const;

	/** Draws the hour ticks along the current date's arc. */
	void DrawHourMarkers(const AArchSkyDirector& Director, const UArchSkySubsystem& Subsystem,
		FPrimitiveDrawInterface* PDI) const;

	/** Draws the analemma for the current clock time across a year. */
	void DrawAnalemma(const AArchSkyDirector& Director, const UArchSkySubsystem& Subsystem,
		FPrimitiveDrawInterface* PDI) const;

	/** Draws the true-north and plan-north arrows. */
	void DrawCompass(const AArchSkyDirector& Director, float NorthOffsetDegrees,
		FPrimitiveDrawInterface* PDI) const;

	/** Draws the horizon ring, so "below the horizon" reads as a plane rather than a guess. */
	void DrawHorizonRing(const AArchSkyDirector& Director, FPrimitiveDrawInterface* PDI) const;

	/** Screen-space positions of the hour labels, filled during DrawVisualization. */
	mutable TArray<TPair<FVector, FString>> PendingHourLabels;
};
