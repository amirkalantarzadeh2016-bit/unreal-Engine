// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintAIBridgeModule.h"

#include "SBPAIBridgePanel.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "BlueprintAIBridge"

DEFINE_LOG_CATEGORY(LogBlueprintAIBridge);

const FName FBlueprintAIBridgeModule::TabName("BlueprintAIBridge");

void FBlueprintAIBridgeModule::StartupModule()
{
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(
			TabName,
			FOnSpawnTab::CreateRaw(this, &FBlueprintAIBridgeModule::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Blueprint AI Bridge"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Export Blueprint graphs for AI editing and review AI-suggested changes."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Blueprint"));

	// UToolMenus is not necessarily up yet at PostEngineInit, so defer the menu entry.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlueprintAIBridgeModule::RegisterMenuEntry));
}

void FBlueprintAIBridgeModule::ShutdownModule()
{
	if (UObjectInitialized() && !IsEngineExitRequested())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
	}
}

TSharedRef<SDockTab> FBlueprintAIBridgeModule::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SBPAIBridgePanel)
		];
}

void FBlueprintAIBridgeModule::RegisterMenuEntry()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (Menu == nullptr)
	{
		UE_LOG(LogBlueprintAIBridge, Warning, TEXT("Could not extend the Tools menu; open the panel from Window > Developer Tools instead."));
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(
		TEXT("BlueprintAIBridge"),
		LOCTEXT("MenuSection", "Blueprint AI Bridge"));

	Section.AddMenuEntry(
		TEXT("OpenBlueprintAIBridge"),
		LOCTEXT("MenuEntryLabel", "Blueprint AI Bridge"),
		LOCTEXT("MenuEntryTooltip", "Open the Blueprint AI Bridge panel."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Blueprint"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FBlueprintAIBridgeModule::TabName);
		})));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintAIBridgeModule, BlueprintAIBridge)
