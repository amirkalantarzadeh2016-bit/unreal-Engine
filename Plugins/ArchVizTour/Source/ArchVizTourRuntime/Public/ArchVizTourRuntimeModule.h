// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Runtime module for the ArchViz Tour plugin.
 *
 * Deliberately does no work beyond module lifetime bookkeeping: everything the plugin owns
 * is either a UObject discovered through the reflection system or a world subsystem, so
 * there is nothing to register at startup.
 */
class FArchVizTourRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
