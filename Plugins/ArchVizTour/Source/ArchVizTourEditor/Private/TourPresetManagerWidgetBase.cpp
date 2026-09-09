// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPresetManagerWidgetBase.h"

#include "ArchVizTourLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "TourPath.h"
#include "TourPathPreset.h"
#include "TourSequenceBuilder.h"
#include "TourSequencePreset.h"

#define LOCTEXT_NAMESPACE "ArchVizTourEditor"

namespace ArchVizTourEditor::PresetManagerPrivate
{
	/** Load every asset of a class through the asset registry. */
	template <typename T>
	static TArray<T*> FindAllAssetsOfClass()
	{
		TArray<T*> Result;

		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		TArray<FAssetData> AssetData;
		AssetRegistryModule.Get().GetAssetsByClass(T::StaticClass()->GetClassPathName(), AssetData, /*bSearchSubClasses*/ true);

		Result.Reserve(AssetData.Num());
		for (const FAssetData& Data : AssetData)
		{
			// GetAsset loads the package; a preset manager is expected to show real contents, and
			// the presets involved are small.
			if (T* Asset = Cast<T>(Data.GetAsset()))
			{
				Result.Add(Asset);
			}
		}

		return Result;
	}
}

TArray<UTourPathPreset*> UTourPresetManagerWidgetBase::GetAllPathPresets() const
{
	return ArchVizTourEditor::PresetManagerPrivate::FindAllAssetsOfClass<UTourPathPreset>();
}

TArray<UTourSequencePreset*> UTourPresetManagerWidgetBase::GetAllTourPresets() const
{
	return ArchVizTourEditor::PresetManagerPrivate::FindAllAssetsOfClass<UTourSequencePreset>();
}

TArray<ATourPath*> UTourPresetManagerWidgetBase::GetTourPathsInLevel() const
{
	TArray<ATourPath*> Result;

	UWorld* EditorWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (EditorWorld == nullptr)
	{
		return Result;
	}

	for (TActorIterator<ATourPath> It(EditorWorld); It; ++It)
	{
		if (ATourPath* Path = *It)
		{
			Result.Add(Path);
		}
	}

	return Result;
}

bool UTourPresetManagerWidgetBase::ApplyPresetToPath(UTourPathPreset* Preset, ATourPath* Path)
{
	if (Preset == nullptr || Path == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ApplyPresetToPath: the preset or the path is null."));
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyPresetToPathTransaction", "Apply Tour Path Preset"));
	Path->Modify();
	Path->LinkedPreset = Preset;

	return Path->LoadFromPresetAsset(Preset);
}

bool UTourPresetManagerWidgetBase::SavePathToPreset(ATourPath* Path, UTourPathPreset* Preset)
{
	if (Preset == nullptr || Path == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SavePathToPreset: the preset or the path is null."));
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SavePathToPresetTransaction", "Save Tour Path To Preset"));
	return Path->SaveToPresetAsset(Preset);
}

ULevelSequence* UTourPresetManagerWidgetBase::BakeTourToLevelSequence(UTourSequencePreset* Preset)
{
	const FScopedTransaction Transaction(LOCTEXT("BakeTourTransaction", "Bake Tour To Level Sequence"));
	return UTourSequenceBuilder::BuildLevelSequence(Preset, /*bLinkToPreset*/ true);
}

bool UTourPresetManagerWidgetBase::ExportPathPresetToJson(UTourPathPreset* Preset, const FString& AbsolutePath)
{
	if (Preset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ExportPathPresetToJson: null preset."));
		return false;
	}

	return Preset->ExportToJson(AbsolutePath);
}

bool UTourPresetManagerWidgetBase::ImportPathPresetFromJson(UTourPathPreset* Preset, const FString& AbsolutePath)
{
	if (Preset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("ImportPathPresetFromJson: null preset."));
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("ImportPresetTransaction", "Import Tour Path Preset"));
	return Preset->ImportFromJson(AbsolutePath);
}

#undef LOCTEXT_NAMESPACE
