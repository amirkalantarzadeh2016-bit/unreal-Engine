// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchVizTourEditorModule.h"

#include "ArchVizTourLog.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_TourPreset.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "IAssetTools.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "TourPath.h"
#include "TourPathComponentVisualizer.h"
#include "TourPathDetails.h"
#include "TourPathPreset.h"
#include "TourPathPresetThumbnailRenderer.h"
#include "TourSplineComponent.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "ArchVizTourEditor"

void FArchVizTourEditorModule::StartupModule()
{
	RegisterComponentVisualizers();
	RegisterDetailCustomizations();
	RegisterAssetActions();
	RegisterThumbnailRenderers();

	UE_LOG(LogArchVizTour, Log, TEXT("ArchVizTourEditor started."));
}

void FArchVizTourEditorModule::ShutdownModule()
{
	// Order matters in reverse: the visualizer holds a component property path, so it goes
	// first, before anything it might resolve through is torn down.
	UnregisterComponentVisualizers();
	UnregisterDetailCustomizations();
	UnregisterAssetActions();

	UE_LOG(LogArchVizTour, Log, TEXT("ArchVizTourEditor shut down."));
}

// ---------------------------------------------------------------------------
// Component visualizers
// ---------------------------------------------------------------------------

void FArchVizTourEditorModule::RegisterComponentVisualizers()
{
	if (GUnrealEd == nullptr)
	{
		// Commandlets and -nullrhi runs have no editor engine; there is nothing to visualize.
		return;
	}

	const FName ComponentClassName = UTourSplineComponent::StaticClass()->GetFName();

	TSharedPtr<FComponentVisualizer> Visualizer = MakeShared<FTourPathComponentVisualizer>();
	GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
	Visualizer->OnRegister();

	RegisteredVisualizerClasses.Add(ComponentClassName);
}

void FArchVizTourEditorModule::UnregisterComponentVisualizers()
{
	if (GUnrealEd != nullptr)
	{
		for (const FName& ClassName : RegisteredVisualizerClasses)
		{
			GUnrealEd->UnregisterComponentVisualizer(ClassName);
		}
	}

	RegisteredVisualizerClasses.Reset();
}

// ---------------------------------------------------------------------------
// Details customizations
// ---------------------------------------------------------------------------

void FArchVizTourEditorModule::RegisterDetailCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	const FName TourPathClassName = ATourPath::StaticClass()->GetFName();
	PropertyModule.RegisterCustomClassLayout(
		TourPathClassName,
		FOnGetDetailCustomizationInstance::CreateStatic(&FTourPathDetails::MakeInstance));

	RegisteredCustomizedClasses.Add(TourPathClassName);

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FArchVizTourEditorModule::UnregisterDetailCustomizations()
{
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		for (const FName& ClassName : RegisteredCustomizedClasses)
		{
			PropertyModule->UnregisterCustomClassLayout(ClassName);
		}

		PropertyModule->NotifyCustomizationModuleChanged();
	}

	RegisteredCustomizedClasses.Reset();
}

// ---------------------------------------------------------------------------
// Asset actions
// ---------------------------------------------------------------------------

void FArchVizTourEditorModule::RegisterAssetActions()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	const EAssetTypeCategories::Type Category = AssetTools.RegisterAdvancedAssetCategory(
		FName(TEXT("ArchVizTour")),
		LOCTEXT("ArchVizTourAssetCategory", "ArchViz Tour"));

	auto RegisterAction = [&AssetTools, this](TSharedRef<IAssetTypeActions> Action)
	{
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredAssetActions.Add(Action);
	};

	RegisterAction(MakeShared<FAssetTypeActions_TourPathPreset>(Category));
	RegisterAction(MakeShared<FAssetTypeActions_TourSequencePreset>(Category));
}

void FArchVizTourEditorModule::UnregisterAssetActions()
{
	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();
		for (const TSharedRef<IAssetTypeActions>& Action : RegisteredAssetActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}

	RegisteredAssetActions.Reset();
}

// ---------------------------------------------------------------------------
// Thumbnails
// ---------------------------------------------------------------------------

void FArchVizTourEditorModule::RegisterThumbnailRenderers()
{
	// Path presets have no mesh or material, so the default thumbnail is a blank icon; the
	// renderer draws a top-down trace of the curve instead. There is deliberately no matching
	// unregister: UThumbnailManager keeps renderers for the process lifetime and offers no
	// removal, and a stale entry after a hot reload is harmless because the class is reloaded
	// with it.
	UThumbnailManager::Get().RegisterCustomRenderer(
		UTourPathPreset::StaticClass(),
		UTourPathPresetThumbnailRenderer::StaticClass());
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArchVizTourEditorModule, ArchVizTourEditor)
