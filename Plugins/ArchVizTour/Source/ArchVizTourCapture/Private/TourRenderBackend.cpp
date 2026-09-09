// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourRenderBackend.h"

#include "TourRenderSettings.h"
#include "TourSequencePreset.h"

bool FTourRenderRequest::IsValid() const
{
	return World.IsValid()
		&& Tour != nullptr
		&& Settings != nullptr
		&& !OutputDirectory.IsEmpty()
		&& TotalFrames > 0;
}

FString ITourRenderBackend::GetReferencerName() const
{
	return FString::Printf(TEXT("ArchVizTour render backend '%s'"), *GetBackendName().ToString());
}
