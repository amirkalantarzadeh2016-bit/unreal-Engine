// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchVizTourRuntimeModule.h"

#include "ArchVizTourLog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogArchVizTour);

DEFINE_STAT(STAT_ArchVizTour_SubsystemTick);
DEFINE_STAT(STAT_ArchVizTour_PathEvaluate);
DEFINE_STAT(STAT_ArchVizTour_RigApplyState);
DEFINE_STAT(STAT_ArchVizTour_RailRebuild);

void FArchVizTourRuntimeModule::StartupModule()
{
	UE_LOG(LogArchVizTour, Log, TEXT("ArchVizTourRuntime started."));
}

void FArchVizTourRuntimeModule::ShutdownModule()
{
	UE_LOG(LogArchVizTour, Log, TEXT("ArchVizTourRuntime shut down."));
}

IMPLEMENT_MODULE(FArchVizTourRuntimeModule, ArchVizTourRuntime)
