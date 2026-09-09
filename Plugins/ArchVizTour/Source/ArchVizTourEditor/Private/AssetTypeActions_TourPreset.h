// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"
#include "CoreMinimal.h"

class UTourPathPreset;
class UTourSequencePreset;

/**
 * Content Browser actions for UTourPathPreset.
 *
 * The right-click menu is where the authoring loop lives for anyone who is not currently
 * looking at the level, so the same operations the details panel offers are duplicated here
 * rather than forcing a round trip through an actor.
 */
class FAssetTypeActions_TourPathPreset : public FAssetTypeActions_Base
{
public:
	explicit FAssetTypeActions_TourPathPreset(uint32 InAssetCategory);

	// --- IAssetTypeActions ------------------------------------------------
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return AssetCategory; }
	virtual void GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder) override;

private:
	/** Write each selected preset to a JSON file in a directory the user picks. */
	static void ExecuteExportJson(TArray<TWeakObjectPtr<UTourPathPreset>> Presets);

	/** Replace each selected preset from a JSON file the user picks. */
	static void ExecuteImportJson(TArray<TWeakObjectPtr<UTourPathPreset>> Presets);

	/** Apply the first selected preset to every ATourPath selected in the level. */
	static void ExecuteAssignToSelectedActors(TArray<TWeakObjectPtr<UTourPathPreset>> Presets);

	/** Duplicate each selected preset beside the original. */
	static void ExecuteDuplicate(TArray<TWeakObjectPtr<UTourPathPreset>> Presets);

	uint32 AssetCategory;
};

/**
 * Content Browser actions for UTourSequencePreset.
 *
 * "Create Level Sequence" is the important one: it is the bridge between the procedural tour
 * and both the Sequencer playback backend and the Movie Render Pipeline render backend.
 */
class FAssetTypeActions_TourSequencePreset : public FAssetTypeActions_Base
{
public:
	explicit FAssetTypeActions_TourSequencePreset(uint32 InAssetCategory);

	// --- IAssetTypeActions ------------------------------------------------
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return AssetCategory; }
	virtual void GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder) override;

private:
	/** Bake each selected tour into a ULevelSequence asset and link it back to the preset. */
	static void ExecuteCreateLevelSequence(TArray<TWeakObjectPtr<UTourSequencePreset>> Presets);

	/** Rebuild each selected tour from its baked ULevelSequence. */
	static void ExecuteImportFromLevelSequence(TArray<TWeakObjectPtr<UTourSequencePreset>> Presets);

	/** Write each selected tour to a JSON file. */
	static void ExecuteExportJson(TArray<TWeakObjectPtr<UTourSequencePreset>> Presets);

	uint32 AssetCategory;
};
