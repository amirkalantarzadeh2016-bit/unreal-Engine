// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_TourPreset.h"

#include "ArchVizTourLog.h"
#include "AssetToolsModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IAssetTools.h"
#include "IDesktopPlatform.h"
#include "LevelSequence.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "TourPath.h"
#include "TourPathPreset.h"
#include "TourSequenceBuilder.h"
#include "TourSequencePreset.h"

#define LOCTEXT_NAMESPACE "ArchVizTourEditor"

namespace ArchVizTourEditor::AssetActionPrivate
{
	/** Ask the user for a directory to write JSON exports into. Empty when cancelled. */
	static FString PickExportDirectory()
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform == nullptr)
		{
			return FString();
		}

		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

		FString ChosenDirectory;
		DesktopPlatform->OpenDirectoryDialog(
			ParentWindowHandle,
			LOCTEXT("PickExportDirectory", "Choose a directory for the exported JSON").ToString(),
			FPaths::ProjectSavedDir(),
			ChosenDirectory);

		return ChosenDirectory;
	}

	/** Ask the user for a JSON file to import. Empty when cancelled. */
	static FString PickImportFile()
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform == nullptr)
		{
			return FString();
		}

		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

		TArray<FString> OutFiles;
		const bool bPicked = DesktopPlatform->OpenFileDialog(
			ParentWindowHandle,
			LOCTEXT("PickImportFile", "Choose a tour JSON file").ToString(),
			FPaths::ProjectSavedDir(),
			FString(),
			TEXT("Tour JSON (*.json)|*.json"),
			EFileDialogFlags::None,
			OutFiles);

		return (bPicked && OutFiles.Num() > 0) ? OutFiles[0] : FString();
	}

	/** Narrow an object list to weak pointers of one type. */
	template <typename T>
	static TArray<TWeakObjectPtr<T>> ToWeakArray(const TArray<UObject*>& InObjects)
	{
		TArray<TWeakObjectPtr<T>> Result;
		Result.Reserve(InObjects.Num());

		for (UObject* Object : InObjects)
		{
			if (T* Typed = Cast<T>(Object))
			{
				Result.Add(Typed);
			}
		}

		return Result;
	}
}

// ---------------------------------------------------------------------------
// Path preset
// ---------------------------------------------------------------------------

FAssetTypeActions_TourPathPreset::FAssetTypeActions_TourPathPreset(uint32 InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

FText FAssetTypeActions_TourPathPreset::GetName() const
{
	return LOCTEXT("TourPathPresetName", "Tour Path Preset");
}

FColor FAssetTypeActions_TourPathPreset::GetTypeColor() const
{
	// Geometry assets take the cool half of the plugin's palette; tours take the warm half, so
	// the two are distinguishable at a glance in a folder holding both.
	return FColor(38, 156, 226);
}

UClass* FAssetTypeActions_TourPathPreset::GetSupportedClass() const
{
	return UTourPathPreset::StaticClass();
}

void FAssetTypeActions_TourPathPreset::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	using namespace ArchVizTourEditor::AssetActionPrivate;

	TArray<TWeakObjectPtr<UTourPathPreset>> Presets = ToWeakArray<UTourPathPreset>(InObjects);
	if (Presets.Num() == 0)
	{
		return;
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PathPresetDuplicate", "Duplicate Tour Path"),
		LOCTEXT("PathPresetDuplicateTooltip", "Create a copy of this path preset with a fresh identity."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FAssetTypeActions_TourPathPreset::ExecuteDuplicate, Presets)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PathPresetExportJson", "Export JSON"),
		LOCTEXT("PathPresetExportJsonTooltip", "Write this path preset to a JSON file that another project can import."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FAssetTypeActions_TourPathPreset::ExecuteExportJson, Presets)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PathPresetImportJson", "Import JSON"),
		LOCTEXT("PathPresetImportJsonTooltip", "Replace this path preset from a JSON file, applying schema migration if it is older."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FAssetTypeActions_TourPathPreset::ExecuteImportJson, Presets)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("PathPresetAssign", "Assign To Selected Actors"),
		LOCTEXT("PathPresetAssignTooltip", "Apply this preset's geometry to every Tour Path selected in the level. Each actor keeps its own placement."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FAssetTypeActions_TourPathPreset::ExecuteAssignToSelectedActors, Presets)));
}

void FAssetTypeActions_TourPathPreset::ExecuteDuplicate(TArray<TWeakObjectPtr<UTourPathPreset>> Presets)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	for (const TWeakObjectPtr<UTourPathPreset>& WeakPreset : Presets)
	{
		UTourPathPreset* Preset = WeakPreset.Get();
		if (Preset == nullptr)
		{
			continue;
		}

		const FString PackagePath = FPaths::GetPath(Preset->GetOutermost()->GetName());

		FString UniquePackageName;
		FString UniqueAssetName;
		AssetTools.CreateUniqueAssetName(PackagePath / Preset->GetName(), TEXT("_Copy"), UniquePackageName, UniqueAssetName);

		if (UObject* Duplicate = AssetTools.DuplicateAsset(UniqueAssetName, FPaths::GetPath(UniquePackageName), Preset))
		{
			if (UTourPathPreset* DuplicatePreset = Cast<UTourPathPreset>(Duplicate))
			{
				// A duplicate is a different asset, so it gets its own identity rather than
				// inheriting one that is supposed to be unique.
				DuplicatePreset->PresetId = FGuid::NewGuid();
				DuplicatePreset->MarkPackageDirty();
			}
		}
	}
}

void FAssetTypeActions_TourPathPreset::ExecuteExportJson(TArray<TWeakObjectPtr<UTourPathPreset>> Presets)
{
	using namespace ArchVizTourEditor::AssetActionPrivate;

	const FString Directory = PickExportDirectory();
	if (Directory.IsEmpty())
	{
		return;
	}

	for (const TWeakObjectPtr<UTourPathPreset>& WeakPreset : Presets)
	{
		if (UTourPathPreset* Preset = WeakPreset.Get())
		{
			Preset->ExportToJson(Directory / Preset->GetName() + TEXT(".json"));
		}
	}
}

void FAssetTypeActions_TourPathPreset::ExecuteImportJson(TArray<TWeakObjectPtr<UTourPathPreset>> Presets)
{
	using namespace ArchVizTourEditor::AssetActionPrivate;

	const FString File = PickImportFile();
	if (File.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ImportTourPathPresetTransaction", "Import Tour Path Preset"));

	for (const TWeakObjectPtr<UTourPathPreset>& WeakPreset : Presets)
	{
		if (UTourPathPreset* Preset = WeakPreset.Get())
		{
			Preset->ImportFromJson(File);
		}
	}
}

void FAssetTypeActions_TourPathPreset::ExecuteAssignToSelectedActors(TArray<TWeakObjectPtr<UTourPathPreset>> Presets)
{
	UTourPathPreset* Preset = Presets.Num() > 0 ? Presets[0].Get() : nullptr;
	if (Preset == nullptr || GEditor == nullptr)
	{
		return;
	}

	USelection* Selection = GEditor->GetSelectedActors();
	if (Selection == nullptr)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AssignTourPathPresetTransaction", "Assign Tour Path Preset"));

	int32 AssignedCount = 0;
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		ATourPath* Path = Cast<ATourPath>(*It);
		if (Path == nullptr)
		{
			continue;
		}

		Path->Modify();
		Path->LinkedPreset = Preset;
		Path->LoadFromPresetAsset(Preset);
		++AssignedCount;
	}

	if (AssignedCount == 0)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Assign To Selected Actors: no Tour Path actors are selected in the level."));
	}
	else
	{
		UE_LOG(LogArchVizTour, Log, TEXT("Assigned preset '%s' to %d tour path(s)."), *Preset->GetName(), AssignedCount);
	}
}

// ---------------------------------------------------------------------------
// Sequence preset
// ---------------------------------------------------------------------------

FAssetTypeActions_TourSequencePreset::FAssetTypeActions_TourSequencePreset(uint32 InAssetCategory)
	: AssetCategory(InAssetCategory)
{
}

FText FAssetTypeActions_TourSequencePreset::GetName() const
{
	return LOCTEXT("TourSequencePresetName", "Tour Sequence Preset");
}

FColor FAssetTypeActions_TourSequencePreset::GetTypeColor() const
{
	return FColor(226, 142, 38);
}

UClass* FAssetTypeActions_TourSequencePreset::GetSupportedClass() const
{
	return UTourSequencePreset::StaticClass();
}

void FAssetTypeActions_TourSequencePreset::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	using namespace ArchVizTourEditor::AssetActionPrivate;

	TArray<TWeakObjectPtr<UTourSequencePreset>> Presets = ToWeakArray<UTourSequencePreset>(InObjects);
	if (Presets.Num() == 0)
	{
		return;
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("TourCreateLevelSequence", "Create Level Sequence"),
		LOCTEXT("TourCreateLevelSequenceTooltip", "Bake this tour into a Level Sequence asset and link it back, enabling both the Sequencer playback backend and the Movie Render Pipeline render backend."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FAssetTypeActions_TourSequencePreset::ExecuteCreateLevelSequence, Presets)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("TourImportFromLevelSequence", "Rebuild From Level Sequence"),
		LOCTEXT("TourImportFromLevelSequenceTooltip", "Rebuild this tour's steps from its baked Level Sequence, so changes made in Sequencer come back to the preset."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FAssetTypeActions_TourSequencePreset::ExecuteImportFromLevelSequence, Presets)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("TourExportJson", "Export JSON"),
		LOCTEXT("TourExportJsonTooltip", "Write this tour to a JSON file that another project can import."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateStatic(&FAssetTypeActions_TourSequencePreset::ExecuteExportJson, Presets)));
}

void FAssetTypeActions_TourSequencePreset::ExecuteCreateLevelSequence(TArray<TWeakObjectPtr<UTourSequencePreset>> Presets)
{
	const FScopedTransaction Transaction(LOCTEXT("BakeTourSequenceTransaction", "Bake Tour To Level Sequence"));

	for (const TWeakObjectPtr<UTourSequencePreset>& WeakPreset : Presets)
	{
		if (UTourSequencePreset* Preset = WeakPreset.Get())
		{
			UTourSequenceBuilder::BuildLevelSequence(Preset, /*bLinkToPreset*/ true);
		}
	}
}

void FAssetTypeActions_TourSequencePreset::ExecuteImportFromLevelSequence(TArray<TWeakObjectPtr<UTourSequencePreset>> Presets)
{
	const FScopedTransaction Transaction(LOCTEXT("RebuildTourFromSequenceTransaction", "Rebuild Tour From Level Sequence"));

	for (const TWeakObjectPtr<UTourSequencePreset>& WeakPreset : Presets)
	{
		UTourSequencePreset* Preset = WeakPreset.Get();
		if (Preset == nullptr)
		{
			continue;
		}

		if (ULevelSequence* Sequence = Preset->BakedSequence.LoadSynchronous())
		{
			UTourSequenceBuilder::ImportFromLevelSequence(Sequence, Preset);
		}
		else
		{
			UE_LOG(LogArchVizTour, Warning,
				TEXT("Tour '%s' has no baked Level Sequence to rebuild from. Run Create Level Sequence first."),
				*Preset->GetName());
		}
	}
}

void FAssetTypeActions_TourSequencePreset::ExecuteExportJson(TArray<TWeakObjectPtr<UTourSequencePreset>> Presets)
{
	using namespace ArchVizTourEditor::AssetActionPrivate;

	const FString Directory = PickExportDirectory();
	if (Directory.IsEmpty())
	{
		return;
	}

	for (const TWeakObjectPtr<UTourSequencePreset>& WeakPreset : Presets)
	{
		if (UTourSequencePreset* Preset = WeakPreset.Get())
		{
			Preset->ExportToJson(Directory / Preset->GetName() + TEXT(".json"));
		}
	}
}

#undef LOCTEXT_NAMESPACE
