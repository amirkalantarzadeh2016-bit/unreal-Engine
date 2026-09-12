// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FArchOpeningPreviewManager;

class FArchitecturalOpeningsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FArchitecturalOpeningsEditorModule& Get();
	static bool IsAvailable();

	TSharedPtr<FArchOpeningPreviewManager> GetPreviewManager() const { return PreviewManager; }

	/** Tab id of the Architectural Openings setup panel. */
	static const FName SetupPanelTabId;

	/** Tab id of the leaf extraction panel. */
	static const FName ExtractionPanelTabId;

private:
	void RegisterMenus();
	void UnregisterMenus();

	TSharedRef<class SDockTab> SpawnSetupPanelTab(const class FSpawnTabArgs& Args);

	TSharedPtr<FArchOpeningPreviewManager> PreviewManager;
	bool bRegisteredVisualizer = false;
	bool bRegisteredDetails = false;
};
