// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TourTypes.h"

#include "TourPersistenceLibrary.generated.h"

class UTourSaveGame;
class UTourSequencePreset;

/**
 * Save-slot persistence for tours authored at runtime.
 *
 * Slot files live under <Project>/Saved/SaveGames, which exists in a packaged client, so a
 * tour authored on site can be saved, listed and reloaded without the editor.
 */
UCLASS()
class ARCHVIZTOURRUNTIME_API UTourPersistenceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Schema version written into new slots. */
	static constexpr int32 CurrentSaveSchemaVersion = 1;

	/**
	 * Write a tour to a save slot, overwriting whatever was there.
	 * @param Preset     Tour to serialise. Null is rejected with a warning.
	 * @param SlotName   Slot name. Empty is rejected.
	 * @param UserIndex  Platform user index; 0 on every desktop platform.
	 * @return true when the slot was written.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Persistence")
	static bool SaveTourToSlot(const UTourSequencePreset* Preset, const FString& SlotName, int32 UserIndex = 0);

	/**
	 * Write raw tour data to a save slot. Used by the in-game authoring widget, which has no
	 * preset asset to hand.
	 * @param Steps      Ordered steps.
	 * @param Paths      Path geometry referenced by the steps.
	 * @param PathNames  Name for each entry of Paths; must be the same length as Paths.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Persistence")
	static bool SaveTourDataToSlot(
		const TArray<FTourStep>& Steps,
		const TArray<FTourPathData>& Paths,
		const TArray<FName>& PathNames,
		const FText& TourTitle,
		bool bLoopTour,
		float GlobalTimeScale,
		const FString& SlotName,
		int32 UserIndex = 0);

	/**
	 * Read a slot back.
	 * @param SlotName   Slot to read.
	 * @param UserIndex  Platform user index.
	 * @param OutSave    Receives the loaded object; left null on failure.
	 * @return true when the slot existed and parsed.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Persistence")
	static bool LoadTourFromSlot(const FString& SlotName, int32 UserIndex, UTourSaveGame*& OutSave);

	/**
	 * Build a transient UTourSequencePreset from a slot, ready to hand to UTourSubsystem.
	 *
	 * Paths stored in the slot are published to the world's UTourSubsystem under their saved
	 * names first, so FTourStep::SplinePathRef resolves exactly as it would against level actors.
	 *
	 * @param WorldContextObject  Any object in the target world.
	 * @param SlotName            Slot to read.
	 * @param UserIndex           Platform user index.
	 * @return The preset, or null on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Persistence", meta = (WorldContext = "WorldContextObject"))
	static UTourSequencePreset* LoadTourPresetFromSlot(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex = 0);

	/**
	 * List every tour slot that exists on disk.
	 * @return Slot names, without the .sav extension, in filesystem order.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Persistence")
	static TArray<FString> EnumerateTourSlots();

	/**
	 * Delete a slot.
	 * @return true when the slot existed and was removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Persistence")
	static bool DeleteTourSlot(const FString& SlotName, int32 UserIndex = 0);

	/** True when a slot with this name exists. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Persistence")
	static bool DoesTourSlotExist(const FString& SlotName, int32 UserIndex = 0);

	/** Upgrade a loaded save payload in place. Pure, so it is directly testable. */
	static bool MigrateSaveGame(UTourSaveGame& InOutSave, int32 FromVersion);
};
