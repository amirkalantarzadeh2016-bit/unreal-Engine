// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"

#include "TourPresetManagerWidgetBase.generated.h"

class ATourPath;
class ULevelSequence;
class UTourPathPreset;
class UTourSequencePreset;

/**
 * Base class for the plugin's Editor Utility Widget.
 *
 * The plugin ships this C++ base rather than a binary .uasset: an Editor Utility Widget's
 * layout is a project's own business, and a shipped blueprint would have to be migrated by
 * hand every time it is customised. Create an Editor Utility Widget, reparent it to this
 * class, and every operation below is one node.
 *
 * Nothing at runtime depends on this class or on any widget derived from it - it lives in the
 * editor module, which a packaged client never links.
 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "Tour Preset Manager Widget Base"))
class ARCHVIZTOUREDITOR_API UTourPresetManagerWidgetBase : public UEditorUtilityWidget
{
	GENERATED_BODY()

public:
	/** Every UTourPathPreset in the project, discovered through the asset registry. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	TArray<UTourPathPreset*> GetAllPathPresets() const;

	/** Every UTourSequencePreset in the project. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	TArray<UTourSequencePreset*> GetAllTourPresets() const;

	/** Every ATourPath in the level currently open in the editor. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	TArray<ATourPath*> GetTourPathsInLevel() const;

	/**
	 * Apply a preset's geometry to a path actor.
	 * The actor keeps its own placement, so one preset can be reused at several locations.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	bool ApplyPresetToPath(UTourPathPreset* Preset, ATourPath* Path);

	/** Write a path actor's current geometry back into a preset asset. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	bool SavePathToPreset(ATourPath* Path, UTourPathPreset* Preset);

	/** Bake a tour into a Level Sequence asset and link it back to the tour. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	ULevelSequence* BakeTourToLevelSequence(UTourSequencePreset* Preset);

	/** Write a preset to a JSON file. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	bool ExportPathPresetToJson(UTourPathPreset* Preset, const FString& AbsolutePath);

	/** Replace a preset from a JSON file, applying schema migration. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Preset Manager")
	bool ImportPathPresetFromJson(UTourPathPreset* Preset, const FString& AbsolutePath);
};
