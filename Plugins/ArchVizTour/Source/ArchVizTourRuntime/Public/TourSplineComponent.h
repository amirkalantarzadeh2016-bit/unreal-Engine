// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SplineComponent.h"

#include "TourSplineComponent.generated.h"

/**
 * Marker subclass of USplineComponent used as ATourPath's root.
 *
 * It exists purely so the editor module can register FTourPathComponentVisualizer against a
 * class of its own. Registering against USplineComponent itself would replace the stock
 * spline visualizer for every spline in the project, which is not ours to change.
 */
UCLASS(ClassGroup = "ArchViz Tour", meta = (BlueprintSpawnableComponent, DisplayName = "Tour Spline"))
class ARCHVIZTOURRUNTIME_API UTourSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UTourSplineComponent();
};
