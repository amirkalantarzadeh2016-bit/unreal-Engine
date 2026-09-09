// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchVizTourCaptureModule.h"

#include "ArchVizTourLog.h"
#include "Modules/ModuleManager.h"

void FArchVizTourCaptureModule::StartupModule()
{
	UE_LOG(LogArchVizTour, Log, TEXT("ArchVizTourCapture started (Movie Render Pipeline backend %s)."),
		WITH_ARCHVIZTOUR_MRP ? TEXT("compiled in") : TEXT("unavailable"));
}

void FArchVizTourCaptureModule::ShutdownModule()
{
	UE_LOG(LogArchVizTour, Log, TEXT("ArchVizTourCapture shut down."));
}

IMPLEMENT_MODULE(FArchVizTourCaptureModule, ArchVizTourCapture)
