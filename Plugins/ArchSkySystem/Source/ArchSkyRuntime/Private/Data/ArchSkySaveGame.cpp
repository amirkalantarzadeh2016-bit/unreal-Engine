// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ArchSkySaveGame.h"

#include "Core/ArchSkySubsystem.h"
#include "JsonObjectConverter.h"
#include "Kismet/GameplayStatics.h"
#include "Util/ArchSkyLog.h"

const TCHAR* UArchSkySaveGame::GetDefaultSlotName()
{
	return TEXT("ArchSkyPresets");
}

namespace ArchSkyPresetDetail
{
	/** Resolves an empty slot name to the plugin default. */
	FString ResolveSlot(const FString& SlotName)
	{
		return SlotName.IsEmpty() ? FString(UArchSkySaveGame::GetDefaultSlotName()) : SlotName;
	}

	/** Loads the slot, or creates an empty in-memory save if it does not exist yet. */
	UArchSkySaveGame* LoadOrCreate(const FString& SlotName)
	{
		const FString Slot = ResolveSlot(SlotName);

		if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
		{
			if (UArchSkySaveGame* Loaded = Cast<UArchSkySaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0)))
			{
				if (Loaded->SaveVersion > UArchSkySaveGame::CurrentSaveVersion)
				{
					// Forward compatibility is not something we can fake: refuse rather
					// than load a layout written by a newer build and misread it.
					UE_LOG(LogArchSky, Warning,
						TEXT("Preset slot '%s' was written by a newer version (%d > %d); refusing to load it."),
						*Slot, Loaded->SaveVersion, UArchSkySaveGame::CurrentSaveVersion);
					return nullptr;
				}
				return Loaded;
			}

			UE_LOG(LogArchSky, Warning, TEXT("Preset slot '%s' exists but could not be read; starting a fresh one."), *Slot);
		}

		return Cast<UArchSkySaveGame>(UGameplayStatics::CreateSaveGameObject(UArchSkySaveGame::StaticClass()));
	}
}

bool UArchSkyPresetLibrary::SaveCurrentStateAsPreset(const UObject* WorldContextObject, const FString& PresetName,
	const FString& Notes, const FString& SlotName)
{
	if (PresetName.TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogArchSky, Warning, TEXT("Refusing to save a preset with an empty name."));
		return false;
	}

	UArchSkySubsystem* Subsystem = UArchSkySubsystem::Get(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogArchSky, Warning, TEXT("SaveCurrentStateAsPreset: no ArchSky subsystem for this world."));
		return false;
	}

	UArchSkySaveGame* Save = ArchSkyPresetDetail::LoadOrCreate(SlotName);
	if (!Save)
	{
		return false;
	}

	FArchSkyNamedPreset Preset;
	Preset.PresetName = PresetName.TrimStartAndEnd();
	Preset.Notes = Notes;
	Preset.State = Subsystem->GetSkyState();
	Preset.CapturedUtc = FDateTime::UtcNow();

	// A preset the architect saves twice under one name should be updated, not duplicated.
	const int32 ExistingIndex = Save->Presets.IndexOfByPredicate(
		[&Preset](const FArchSkyNamedPreset& Candidate)
		{
			return Candidate.PresetName.Equals(Preset.PresetName, ESearchCase::IgnoreCase);
		});

	if (ExistingIndex != INDEX_NONE)
	{
		Save->Presets[ExistingIndex] = Preset;
	}
	else
	{
		Save->Presets.Add(Preset);
	}

	Save->SaveVersion = UArchSkySaveGame::CurrentSaveVersion;

	const FString Slot = ArchSkyPresetDetail::ResolveSlot(SlotName);
	if (!UGameplayStatics::SaveGameToSlot(Save, Slot, 0))
	{
		UE_LOG(LogArchSky, Error, TEXT("Failed to write preset slot '%s'."), *Slot);
		return false;
	}

	UE_LOG(LogArchSky, Log, TEXT("Saved sky preset '%s' to slot '%s'."), *Preset.PresetName, *Slot);
	return true;
}

bool UArchSkyPresetLibrary::LoadPresetByName(const UObject* WorldContextObject, const FString& PresetName, const FString& SlotName)
{
	UArchSkySubsystem* Subsystem = UArchSkySubsystem::Get(WorldContextObject);
	if (!Subsystem)
	{
		UE_LOG(LogArchSky, Warning, TEXT("LoadPresetByName: no ArchSky subsystem for this world."));
		return false;
	}

	const FString Slot = ArchSkyPresetDetail::ResolveSlot(SlotName);
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		UE_LOG(LogArchSky, Warning, TEXT("Preset slot '%s' does not exist."), *Slot);
		return false;
	}

	UArchSkySaveGame* Save = Cast<UArchSkySaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Save)
	{
		UE_LOG(LogArchSky, Warning, TEXT("Preset slot '%s' could not be read."), *Slot);
		return false;
	}

	const FArchSkyNamedPreset* Found = Save->Presets.FindByPredicate(
		[&PresetName](const FArchSkyNamedPreset& Candidate)
		{
			return Candidate.PresetName.Equals(PresetName, ESearchCase::IgnoreCase);
		});

	if (!Found)
	{
		UE_LOG(LogArchSky, Warning, TEXT("No preset named '%s' in slot '%s'."), *PresetName, *Slot);
		return false;
	}

	Subsystem->ApplySkyState(Found->State);
	UE_LOG(LogArchSky, Log, TEXT("Applied sky preset '%s'."), *Found->PresetName);
	return true;
}

TArray<FArchSkyNamedPreset> UArchSkyPresetLibrary::GetSavedPresets(const FString& SlotName)
{
	TArray<FArchSkyNamedPreset> Result;

	const FString Slot = ArchSkyPresetDetail::ResolveSlot(SlotName);
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		return Result;
	}

	if (const UArchSkySaveGame* Save = Cast<UArchSkySaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0)))
	{
		Result = Save->Presets;

		// Newest first: the preset an architect just saved is the one they will reach for.
		Result.Sort([](const FArchSkyNamedPreset& A, const FArchSkyNamedPreset& B)
		{
			return A.CapturedUtc > B.CapturedUtc;
		});
	}

	return Result;
}

bool UArchSkyPresetLibrary::DeletePreset(const FString& PresetName, const FString& SlotName)
{
	const FString Slot = ArchSkyPresetDetail::ResolveSlot(SlotName);
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		return false;
	}

	UArchSkySaveGame* Save = Cast<UArchSkySaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Save)
	{
		return false;
	}

	const int32 RemovedCount = Save->Presets.RemoveAll(
		[&PresetName](const FArchSkyNamedPreset& Candidate)
		{
			return Candidate.PresetName.Equals(PresetName, ESearchCase::IgnoreCase);
		});

	if (RemovedCount == 0)
	{
		return false;
	}

	return UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
}

FString UArchSkyPresetLibrary::ExportStateToJson(const FArchSkyState& State)
{
	FString Json;

	// Reflection-driven, so adding a field to FArchSkyState needs no change here.
	if (!FJsonObjectConverter::UStructToJsonObjectString(State, Json))
	{
		UE_LOG(LogArchSky, Warning, TEXT("Failed to serialise the sky state to JSON."));
		return FString();
	}

	return Json;
}

bool UArchSkyPresetLibrary::ImportStateFromJson(const FString& Json, FArchSkyState& OutState)
{
	FArchSkyState Parsed;

	// CheckFlags/SkipFlags zeroed so every UPROPERTY participates regardless of its
	// Blueprint visibility.
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Parsed, 0, 0))
	{
		UE_LOG(LogArchSky, Warning, TEXT("Failed to parse a sky state from JSON; the input is left unapplied."));
		return false;
	}

	// Never trust a hand-edited file: clamp before it reaches the subsystem.
	Parsed.Sanitise();
	OutState = Parsed;
	return true;
}
