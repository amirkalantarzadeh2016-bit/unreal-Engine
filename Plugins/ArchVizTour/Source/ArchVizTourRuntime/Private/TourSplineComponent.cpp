// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourSplineComponent.h"

UTourSplineComponent::UTourSplineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// The spline is a derived cache of ATourPath::Points; nothing about it is authored directly,
	// so it never needs to be saved, edited or selected in its own right.
	bAllowDiscardingSplinePoints = false;
	bDrawDebug = false;
}
