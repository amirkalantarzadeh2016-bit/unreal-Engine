// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchSkyRuntimeModule.h"

#include "Modules/ModuleManager.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "FArchSkyRuntimeModule"

/**
 * Defined in ArchSkyConsole.cpp. In a Shipping build both are empty stubs, so the module
 * needs no configuration-dependent code of its own.
 */
extern void ArchSkyConsole_RegisterDebugHooks();
extern void ArchSkyConsole_UnregisterDebugHooks();

void FArchSkyRuntimeModule::StartupModule()
{
	// The debug HUD and sun-path drawing hang off a world-tick delegate whose lifetime is
	// the module's, not any world's - so they keep working in a level whose Director has
	// been deleted, which is exactly when someone reaches for them.
	ArchSkyConsole_RegisterDebugHooks();

	UE_LOG(LogArchSky, Log, TEXT("ArchSkyRuntime started."));
}

void FArchSkyRuntimeModule::ShutdownModule()
{
	ArchSkyConsole_UnregisterDebugHooks();

	UE_LOG(LogArchSky, Log, TEXT("ArchSkyRuntime shut down."));
}

FArchSkyRuntimeModule* FArchSkyRuntimeModule::GetPtr()
{
	return FModuleManager::GetModulePtr<FArchSkyRuntimeModule>(TEXT("ArchSkyRuntime"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArchSkyRuntimeModule, ArchSkyRuntime)
