// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SDockTab;
struct FSpawnTabArgs;

/** Log category shared by every class in the Blueprint AI Bridge plugin. */
BLUEPRINTAIBRIDGE_API DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintAIBridge, Log, All);

/**
 * Editor module for the Blueprint AI Bridge.
 *
 * Owns the nomad tab spawner for SBPAIBridgePanel and the Window menu entry that opens it.
 */
class FBlueprintAIBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Tab id used by the nomad tab spawner. Public so other tools can invoke the tab. */
	static const FName TabName;

private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
	void RegisterMenuEntry();
};
