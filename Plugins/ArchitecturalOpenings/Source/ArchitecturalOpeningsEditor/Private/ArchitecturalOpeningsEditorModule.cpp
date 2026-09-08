// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchitecturalOpeningsEditorModule.h"

#include "ArchOpeningComponent.h"
#include "ArchOpeningComponentDetails.h"
#include "ArchOpeningComponentVisualizer.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningPreviewManager.h"
#include "SArchOpeningExtractionPanel.h"
#include "SArchOpeningSetupPanel.h"

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "ArchitecturalOpeningsEditor"

const FName FArchitecturalOpeningsEditorModule::SetupPanelTabId(TEXT("ArchitecturalOpeningsSetup"));
static const FName ArchOpeningExtractionTabId(TEXT("ArchitecturalOpeningsExtraction"));

FArchitecturalOpeningsEditorModule& FArchitecturalOpeningsEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FArchitecturalOpeningsEditorModule>("ArchitecturalOpeningsEditor");
}

bool FArchitecturalOpeningsEditorModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("ArchitecturalOpeningsEditor")
		&& Get().PreviewManager.IsValid();
}

void FArchitecturalOpeningsEditorModule::StartupModule()
{
	PreviewManager = MakeShared<FArchOpeningPreviewManager>();
	PreviewManager->Initialize();

	// Details panel commands.
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomClassLayout(
			UArchOpeningComponent::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FArchOpeningComponentDetails::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();
		bRegisteredDetails = true;
	}

	// Viewport visualization of hinge, axis, outside arrow, arc, slide path and trigger volume.
	if (GUnrealEd != nullptr)
	{
		GUnrealEd->RegisterComponentVisualizer(
			UArchOpeningComponent::StaticClass()->GetFName(),
			MakeShared<FArchOpeningComponentVisualizer>());
		bRegisteredVisualizer = true;
	}

	// Tabs.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		SetupPanelTabId,
		FOnSpawnTab::CreateRaw(this, &FArchitecturalOpeningsEditorModule::SpawnSetupPanelTab))
		.SetDisplayName(LOCTEXT("SetupTabTitle", "Architectural Openings"))
		.SetTooltipText(LOCTEXT("SetupTabTooltip", "Configure doors, windows and sliding panels from existing scene meshes."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		ArchOpeningExtractionTabId,
		FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::NomadTab)
				[
					SNew(SArchOpeningExtractionPanel)
				];
		}))
		.SetDisplayName(LOCTEXT("ExtractionTabTitle", "Opening Leaf Extraction"))
		.SetTooltipText(LOCTEXT("ExtractionTabTooltip", "Split a one-mesh source asset into a movable leaf and the fixed remainder."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FArchitecturalOpeningsEditorModule::RegisterMenus));
}

void FArchitecturalOpeningsEditorModule::ShutdownModule()
{
	// Order matters: preview must be restored before anything else is torn down, so no opening is
	// left holding a preview pose.
	if (PreviewManager.IsValid())
	{
		PreviewManager->Shutdown();
		PreviewManager.Reset();
	}

	UnregisterMenus();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SetupPanelTabId);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ArchOpeningExtractionTabId);

	if (bRegisteredVisualizer && GUnrealEd != nullptr)
	{
		GUnrealEd->UnregisterComponentVisualizer(UArchOpeningComponent::StaticClass()->GetFName());
		bRegisteredVisualizer = false;
	}

	if (bRegisteredDetails && FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.UnregisterCustomClassLayout(UArchOpeningComponent::StaticClass()->GetFName());
		PropertyEditor.NotifyCustomizationModuleChanged();
		bRegisteredDetails = false;
	}
}

TSharedRef<SDockTab> FArchitecturalOpeningsEditorModule::SpawnSetupPanelTab(const FSpawnTabArgs& /*Args*/)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SArchOpeningSetupPanel)
		];
}

void FArchitecturalOpeningsEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
	if (WindowMenu == nullptr)
	{
		return;
	}

	FToolMenuSection& Section = WindowMenu->FindOrAddSection(
		TEXT("ArchitecturalOpenings"), LOCTEXT("MenuSection", "Architectural Openings"));

	Section.AddMenuEntry(
		TEXT("OpenArchOpeningSetup"),
		LOCTEXT("MenuSetup", "Architectural Openings"),
		LOCTEXT("MenuSetupTip", "Open the opening setup panel."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FArchitecturalOpeningsEditorModule::SetupPanelTabId);
		})));

	Section.AddMenuEntry(
		TEXT("OpenArchOpeningExtraction"),
		LOCTEXT("MenuExtraction", "Opening Leaf Extraction"),
		LOCTEXT("MenuExtractionTip", "Open the leaf extraction tool."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(ArchOpeningExtractionTabId);
		})));
}

void FArchitecturalOpeningsEditorModule::UnregisterMenus()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArchitecturalOpeningsEditorModule, ArchitecturalOpeningsEditor)
