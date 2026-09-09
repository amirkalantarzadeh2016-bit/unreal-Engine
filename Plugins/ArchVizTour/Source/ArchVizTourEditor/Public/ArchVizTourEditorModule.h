// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FTourPathComponentVisualizer;
class IAssetTypeActions;

/**
 * Editor module for the ArchViz Tour plugin.
 *
 * Registers the component visualizer, the details customization, the asset actions and the
 * thumbnail renderers, and unregisters every one of them on shutdown - a live-coding reload of
 * this module leaves the editor holding stale registrations otherwise.
 */
class FArchVizTourEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterComponentVisualizers();
	void UnregisterComponentVisualizers();

	void RegisterDetailCustomizations();
	void UnregisterDetailCustomizations();

	void RegisterAssetActions();
	void UnregisterAssetActions();

	void RegisterThumbnailRenderers();

	/** Asset actions registered by this module, so exactly those can be unregistered. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetActions;

	/** Classes whose visualizers this module registered. */
	TArray<FName> RegisteredVisualizerClasses;

	/** Classes whose details customizations this module registered. */
	TArray<FName> RegisteredCustomizedClasses;
};
