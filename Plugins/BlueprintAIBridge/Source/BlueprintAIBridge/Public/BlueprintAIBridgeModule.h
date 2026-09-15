// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class SBPAIBridgePanel;
class SDockTab;
class FSpawnTabArgs;
class UBlueprint;

/** Log category shared by every class in the Blueprint AI Bridge plugin. */
BLUEPRINTAIBRIDGE_API DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintAIBridge, Log, All);

/**
 * Editor module for the Blueprint AI Bridge.
 *
 * Owns the nomad tab spawner for SBPAIBridgePanel and the two places it can be opened from:
 * the Tools menu, and the toolbar of any Blueprint asset editor.
 */
class FBlueprintAIBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Tab id used by the nomad tab spawner. Public so other tools can invoke the tab. */
	static const FName TabName;

	/**
	 * Opens the panel, pointing it at Blueprint when one is given.
	 * A null Blueprint just opens the panel and leaves the current selection alone.
	 */
	void OpenPanel(UBlueprint* Blueprint);

private:
	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	/** Tools menu entry, for reaching the panel without a Blueprint open. */
	void RegisterToolsMenuEntry();

	/** Toolbar button on the Blueprint asset editors, which knows what is being edited. */
	void RegisterAssetEditorToolbars();

	/**
	 * The panel this module last spawned.
	 *
	 * Weak because the tab owns it: the developer can close the tab at any point, and the next
	 * OpenPanel has to notice that and let the spawner build a fresh one.
	 */
	TWeakPtr<SBPAIBridgePanel> ActivePanel;
};
