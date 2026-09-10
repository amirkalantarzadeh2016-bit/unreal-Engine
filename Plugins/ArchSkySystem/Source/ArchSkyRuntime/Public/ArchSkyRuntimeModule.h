// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Runtime module for the ArchSky System.
 *
 * Loaded at PreDefault so that UArchSkySettings (a UDeveloperSettings) is registered
 * before any map loads and before UArchSkySubsystem is created for the first world.
 */
class ARCHSKYRUNTIME_API FArchSkyRuntimeModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	/** Convenience accessor. Returns nullptr if the module is not loaded. */
	static FArchSkyRuntimeModule* GetPtr();
};
