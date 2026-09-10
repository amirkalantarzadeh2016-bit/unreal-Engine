// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/ArchSkyState.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ArchSkySaveGame.generated.h"

class UArchSkySubsystem;

/** One architect-authored preset captured during a presentation. */
USTRUCT(BlueprintType)
struct ARCHSKYRUNTIME_API FArchSkyNamedPreset
{
	GENERATED_BODY()

	/** Free text as typed by the architect, e.g. "Client Review - Winter Morning". */
	UPROPERTY(BlueprintReadWrite, Category = "ArchSky|Presets")
	FString PresetName;

	/** Optional note about what this view is meant to demonstrate. */
	UPROPERTY(BlueprintReadWrite, Category = "ArchSky|Presets")
	FString Notes;

	/** The captured state. */
	UPROPERTY(BlueprintReadWrite, Category = "ArchSky|Presets")
	FArchSkyState State;

	/** UTC timestamp of capture, so the list can be sorted most-recent-first. */
	UPROPERTY(BlueprintReadWrite, Category = "ArchSky|Presets")
	FDateTime CapturedUtc;

	FArchSkyNamedPreset() = default;
};

/**
 * Runtime preset storage.
 *
 * ARCH NOTE: edit-time presets are UArchSkyStatePreset assets; runtime presets are these.
 * They cannot be the same thing - a packaged build cannot create a .uasset, and an
 * architect saving "Client Review - Winter Morning" mid-presentation is very much a
 * runtime action. Both share FArchSkyState, so a runtime preset can be transcribed into an
 * asset by hand, and ApplyStatePreset / ApplySkyState accept either.
 *
 * The save is written through USaveGame (platform-correct, works on console and in a
 * sandboxed install); ExportToJson/ImportFromJson exist alongside it so a studio can move
 * a preset library between machines as a text file.
 */
UCLASS(BlueprintType)
class ARCHSKYRUNTIME_API UArchSkySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bumped when the layout of the saved data changes, so old saves can be migrated. */
	UPROPERTY(BlueprintReadOnly, Category = "ArchSky|Presets")
	int32 SaveVersion = 1;

	/** Every preset the user has saved, newest last. */
	UPROPERTY(BlueprintReadWrite, Category = "ArchSky|Presets")
	TArray<FArchSkyNamedPreset> Presets;

	/** The slot this plugin writes to unless a caller names another. */
	static const TCHAR* GetDefaultSlotName();

	/** Current layout version, compared against SaveVersion on load. */
	static constexpr int32 CurrentSaveVersion = 1;
};

/** Blueprint-facing save/load of sky presets. */
UCLASS(meta = (ScriptName = "ArchSkyPresets"))
class ARCHSKYRUNTIME_API UArchSkyPresetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Captures the subsystem's current state under a name and writes it to the slot.
	 * An existing preset with the same name is replaced, so saving twice does not
	 * silently accumulate duplicates.
	 *
	 * @return False if the subsystem is unavailable or the write failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets", meta = (WorldContext = "WorldContextObject"))
	static bool SaveCurrentStateAsPreset(const UObject* WorldContextObject, const FString& PresetName,
		const FString& Notes, const FString& SlotName);

	/** Applies a saved preset by name. Returns false when the name is not in the slot. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets", meta = (WorldContext = "WorldContextObject"))
	static bool LoadPresetByName(const UObject* WorldContextObject, const FString& PresetName, const FString& SlotName);

	/** Every preset in the slot, newest first. Empty when the slot does not exist. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets")
	static TArray<FArchSkyNamedPreset> GetSavedPresets(const FString& SlotName);

	/** Removes a preset by name and rewrites the slot. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets")
	static bool DeletePreset(const FString& PresetName, const FString& SlotName);

	/** Serialises one state to a JSON string, for clipboard or file transfer. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets")
	static FString ExportStateToJson(const FArchSkyState& State);

	/** Parses a state from JSON. Returns false and leaves OutState untouched on malformed input. */
	UFUNCTION(BlueprintCallable, Category = "ArchSky|Presets")
	static bool ImportStateFromJson(const FString& Json, FArchSkyState& OutState);
};
