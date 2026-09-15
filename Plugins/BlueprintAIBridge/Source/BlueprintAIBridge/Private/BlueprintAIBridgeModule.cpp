// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintAIBridgeModule.h"

#include "SBPAIBridgePanel.h"
#include "Engine/Blueprint.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
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

	// UToolMenus is not necessarily up yet at PostEngineInit, so defer both registrations.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateLambda([this]()
		{
			RegisterToolsMenuEntry();
			RegisterAssetEditorToolbars();
		}));
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
	TSharedRef<SBPAIBridgePanel> Panel = SNew(SBPAIBridgePanel);
	ActivePanel = Panel;

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			Panel
		];
}

void FBlueprintAIBridgeModule::OpenPanel(UBlueprint* Blueprint)
{
	// Invoking the tab spawns the panel when there is not one already, and ActivePanel is set
	// from inside SpawnTab, so by the time this returns the pointer is good either way.
	FGlobalTabmanager::Get()->TryInvokeTab(TabName);

	if (Blueprint == nullptr)
	{
		return;
	}

	if (TSharedPtr<SBPAIBridgePanel> Panel = ActivePanel.Pin())
	{
		Panel->SetBlueprint(Blueprint);
	}
	else
	{
		UE_LOG(LogBlueprintAIBridge, Warning,
			TEXT("Opened the panel but could not reach it to select '%s'; pick it in the panel instead."),
			*Blueprint->GetName());
	}
}

void FBlueprintAIBridgeModule::RegisterToolsMenuEntry()
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
		FUIAction(FExecuteAction::CreateRaw(this, &FBlueprintAIBridgeModule::OpenPanel, static_cast<UBlueprint*>(nullptr))));
}

void FBlueprintAIBridgeModule::RegisterAssetEditorToolbars()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// The asset editors whose toolbars are worth carrying the button. Extending a menu that
	// does not exist in this editor build is harmless -- the entry simply never renders.
	static const TCHAR* ToolbarMenus[] = {
		TEXT("AssetEditor.BlueprintEditor.ToolBar"),
		TEXT("AssetEditor.WidgetBlueprintEditor.ToolBar")
	};

	for (const TCHAR* MenuName : ToolbarMenus)
	{
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(FName(MenuName));
		if (Toolbar == nullptr)
		{
			continue;
		}

		FToolMenuSection& Section = Toolbar->FindOrAddSection(
			TEXT("BlueprintAIBridge"),
			LOCTEXT("ToolbarSection", "AI"));

		FToolUIAction Action;
		Action.ExecuteAction = FToolMenuExecuteAction::CreateLambda(
			[this](const FToolMenuContext& Context)
			{
				// The asset editor puts its toolkit in the menu context, and the Blueprint that
				// toolkit is editing is what "this one" means. The toolkit's own accessor for
				// the edited objects is protected, so the context's public one is the way in.
				UBlueprint* Blueprint = nullptr;

				if (UAssetEditorToolkitMenuContext* ToolkitContext = Context.FindContext<UAssetEditorToolkitMenuContext>())
				{
					for (UObject* EditedObject : ToolkitContext->GetEditingObjects())
					{
						if (UBlueprint* Candidate = Cast<UBlueprint>(EditedObject))
						{
							Blueprint = Candidate;
							break;
						}
					}
				}

				// Unresolvable context is not a failure: the panel opens, unselected, as before.
				if (Blueprint == nullptr)
				{
					UE_LOG(LogBlueprintAIBridge, Verbose,
						TEXT("Toolbar button could not determine the Blueprint being edited; opening the panel unselected."));
				}

				OpenPanel(Blueprint);
			});

		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			TEXT("OpenBlueprintAIBridge"),
			Action,
			LOCTEXT("ToolbarLabel", "AI Bridge"),
			LOCTEXT("ToolbarTooltip", "Open the Blueprint AI Bridge on this Blueprint."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Blueprint")));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintAIBridgeModule, BlueprintAIBridge)
