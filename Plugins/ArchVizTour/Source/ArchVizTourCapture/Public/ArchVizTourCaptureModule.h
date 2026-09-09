// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Runtime capture / video-export module.
 *
 * Separate from ArchVizTourRuntime so a project that only needs playback does not link the
 * render-target, image-write and encoder machinery into its client.
 */
class FArchVizTourCaptureModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
