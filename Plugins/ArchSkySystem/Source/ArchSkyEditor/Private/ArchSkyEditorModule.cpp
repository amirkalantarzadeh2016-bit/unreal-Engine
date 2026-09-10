// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchSkyDirectorDetails.h"
#include "ArchSkyDirectorVisualizer.h"
#include "ArchSkySceneValidator.h"

#include "Components/SceneComponent.h"
#include "Core/ArchSkyDirector.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "UnrealEdGlobals.h"
#include "Util/ArchSkyLog.h"

#define LOCTEXT_NAMESPACE "FArchSkyEditorModule"

/**
 * Editor module for the ArchSky System.
 *
 * Registers the viewport visualiser, the Sun Study details customisation, the scene
 * validator's message-log category, and an "ArchSky" section in the Level Editor toolbar's
 * Tools menu.
 */
class FArchSkyEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

private:
	/** Registers FArchSkyDirectorVisualizer against the Director's root component class. */
	void RegisterVisualizer();
	void UnregisterVisualizer();

	/** Registers the Sun Study details panel for AArchSkyDirector. */
	void RegisterDetailsCustomization();
	void UnregisterDetailsCustomization();

	/** Creates the "ArchSky" message-log category the validator writes to. */
	void RegisterMessageLog();
	void UnregisterMessageLog();

	/** Adds the ArchSky section to the Level Editor's Tools menu. */
	void RegisterMenus();

	/** Spawns a Director at the world origin, or selects the existing one. */
	static void AddSkyDirectorToLevel();

	/** Runs the scene validator against the current editor world. */
	static void ValidateSceneSetup();

	/** The world the editor is currently showing. */
	static UWorld* GetEditorWorld();

	/** Component class the visualiser is registered against, kept for clean unregistration. */
	FName RegisteredVisualizerClassName;

	/** True once the details customisation is registered. */
	bool bDetailsCustomizationRegistered = false;
};

IMPLEMENT_MODULE(FArchSkyEditorModule, ArchSkyEditor)

// ---------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------

void FArchSkyEditorModule::StartupModule()
{
	RegisterMessageLog();
	RegisterVisualizer();
	RegisterDetailsCustomization();

	// ToolMenus may not be up yet at PostEngineInit on every path, so register through the
	// startup callback rather than assuming.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FArchSkyEditorModule::RegisterMenus));

	UE_LOG(LogArchSky, Log, TEXT("ArchSkyEditor started."));
}

void FArchSkyEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	UnregisterDetailsCustomization();
	UnregisterVisualizer();
	UnregisterMessageLog();

	UE_LOG(LogArchSky, Log, TEXT("ArchSkyEditor shut down."));
}

// ---------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------

void FArchSkyEditorModule::RegisterVisualizer()
{
	if (!GUnrealEd)
	{
		// A commandlet or -nullrhi run has no editor engine; there is nothing to draw into.
		UE_LOG(LogArchSky, Verbose, TEXT("No GUnrealEd; skipping ArchSky visualiser registration."));
		return;
	}

	// ARCH NOTE: registered against USceneComponent, not against a bespoke component class.
	// The Director's root is a plain USceneComponent, and inventing a UArchSkyRootComponent
	// purely to hang a visualiser off would add a class to the public API for no other
	// reason. The visualiser's first act is to check that the owner is an AArchSkyDirector
	// and bail otherwise, so the broad registration costs one cast per selected component.
	RegisteredVisualizerClassName = USceneComponent::StaticClass()->GetFName();

	GUnrealEd->RegisterComponentVisualizer(RegisteredVisualizerClassName, MakeShared<FArchSkyDirectorVisualizer>());
}

void FArchSkyEditorModule::UnregisterVisualizer()
{
	if (GUnrealEd && !RegisteredVisualizerClassName.IsNone())
	{
		GUnrealEd->UnregisterComponentVisualizer(RegisteredVisualizerClassName);
		RegisteredVisualizerClassName = NAME_None;
	}
}

void FArchSkyEditorModule::RegisterDetailsCustomization()
{
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	PropertyModule.RegisterCustomClassLayout(
		AArchSkyDirector::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FArchSkyDirectorDetails::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
	bDetailsCustomizationRegistered = true;
}

void FArchSkyEditorModule::UnregisterDetailsCustomization()
{
	if (!bDetailsCustomizationRegistered)
	{
		return;
	}

	// LoadModuleChecked would revive an already-unloaded PropertyEditor during shutdown.
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyModule->UnregisterCustomClassLayout(AArchSkyDirector::StaticClass()->GetFName());
		PropertyModule->NotifyCustomizationModuleChanged();
	}

	bDetailsCustomizationRegistered = false;
}

void FArchSkyEditorModule::RegisterMessageLog()
{
	if (FMessageLogModule* MessageLogModule = FModuleManager::GetModulePtr<FMessageLogModule>(TEXT("MessageLog")))
	{
		FMessageLogInitializationOptions InitOptions;
		InitOptions.bShowFilters = true;
		InitOptions.bShowPages = true;
		InitOptions.bAllowClear = true;

		MessageLogModule->RegisterLogListing(
			UArchSkySceneValidator::MessageLogName,
			LOCTEXT("ArchSkyMessageLog", "ArchSky"),
			InitOptions);
	}
}

void FArchSkyEditorModule::UnregisterMessageLog()
{
	if (FMessageLogModule* MessageLogModule = FModuleManager::GetModulePtr<FMessageLogModule>(TEXT("MessageLog")))
	{
		MessageLogModule->UnregisterLogListing(UArchSkySceneValidator::MessageLogName);
	}
}

void FArchSkyEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!ToolsMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
		TEXT("ArchSky"), LOCTEXT("ArchSkySection", "ArchSky"));

	Section.AddMenuEntry(
		TEXT("AddSkyDirector"),
		LOCTEXT("AddSkyDirectorLabel", "Add Sky Director to Level"),
		LOCTEXT("AddSkyDirectorTooltip",
			"Places an ArchSky Director at the world origin, or selects the existing one. "
			"The Director brings its own sun, moon, sky light, atmosphere, clouds and fog."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.DirectionalLight")),
		FUIAction(FExecuteAction::CreateStatic(&FArchSkyEditorModule::AddSkyDirectorToLevel)));

	Section.AddMenuEntry(
		TEXT("ValidateSceneSetup"),
		LOCTEXT("ValidateSceneLabel", "Validate Scene Setup"),
		LOCTEXT("ValidateSceneTooltip",
			"Checks this level for duplicate or non-movable lights, a missing sky atmosphere, a wrong "
			"AtmosphereSunLightIndex, a static sky light and other setup mistakes. Results go to the "
			"ArchSky message log."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("MessageLog.Warning")),
		FUIAction(FExecuteAction::CreateStatic(&FArchSkyEditorModule::ValidateSceneSetup)));
}

// ---------------------------------------------------------------------------------------
// Menu actions
// ---------------------------------------------------------------------------------------

UWorld* FArchSkyEditorModule::GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void FArchSkyEditorModule::AddSkyDirectorToLevel()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		UE_LOG(LogArchSky, Warning, TEXT("No editor world; cannot add an ArchSky Director."));
		return;
	}

	// One Director per level is the contract, so adding a second is a mistake we prevent
	// rather than a mistake we warn about afterwards.
	for (TActorIterator<AArchSkyDirector> It(World); It; ++It)
	{
		UE_LOG(LogArchSky, Log,
			TEXT("This level already has an ArchSky Director ('%s'); selecting it instead of adding another."),
			*It->GetActorNameOrLabel());

		if (GEditor)
		{
			GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
			GEditor->SelectActor(*It, /*bInSelected*/ true, /*bNotify*/ true);
		}
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddSkyDirectorTransaction", "Add ArchSky Director"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AArchSkyDirector* Director = World->SpawnActor<AArchSkyDirector>(
		AArchSkyDirector::StaticClass(), FTransform::Identity, SpawnParams);

	if (!Director)
	{
		UE_LOG(LogArchSky, Error, TEXT("Failed to spawn an ArchSky Director."));
		return;
	}

	Director->SetActorLabel(TEXT("ArchSkyDirector"));

	if (GEditor)
	{
		GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
		GEditor->SelectActor(Director, /*bInSelected*/ true, /*bNotify*/ true);
	}

	UE_LOG(LogArchSky, Log, TEXT("Added an ArchSky Director at the world origin."));
}

void FArchSkyEditorModule::ValidateSceneSetup()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		UE_LOG(LogArchSky, Warning, TEXT("No editor world; cannot validate the scene."));
		return;
	}

	UArchSkySceneValidator::ValidateAndReport(World);
}

#undef LOCTEXT_NAMESPACE
