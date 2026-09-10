// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchSkyRuntimeModule.h"

#include "Modules/ModuleManager.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "FArchSkyRuntimeModule"

void FArchSkyRuntimeModule::StartupModule()
{
	UE_LOG(LogArchSky, Log, TEXT("ArchSkyRuntime started."));
}

void FArchSkyRuntimeModule::ShutdownModule()
{
	UE_LOG(LogArchSky, Log, TEXT("ArchSkyRuntime shut down."));
}

FArchSkyRuntimeModule* FArchSkyRuntimeModule::GetPtr()
{
	return FModuleManager::GetModulePtr<FArchSkyRuntimeModule>(TEXT("ArchSkyRuntime"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArchSkyRuntimeModule, ArchSkyRuntime)
