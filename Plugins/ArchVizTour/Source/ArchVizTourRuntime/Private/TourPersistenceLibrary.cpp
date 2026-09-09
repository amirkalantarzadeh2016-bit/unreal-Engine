// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPersistenceLibrary.h"

#include "ArchVizTourLog.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "TourPath.h"
#include "TourPathPreset.h"
#include "TourSaveGame.h"
#include "TourSequencePreset.h"
#include "TourSubsystem.h"

namespace ArchVizTour::PersistencePrivate
{
	/** Extension USaveGame slots are written with. Fixed by the engine's save system. */
	static const TCHAR* SlotExtension = TEXT("sav");

	/** Directory USaveGame slots live in, relative to the project. Exists in a packaged build. */
	static FString GetSaveGamesDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames");
	}
}

bool UTourPersistenceLibrary::SaveTourToSlot(const UTourSequencePreset* Preset, const FString& SlotName, int32 UserIndex)
{
	if (Preset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SaveTourToSlot('%s'): null preset."), *SlotName);
		return false;
	}

	// Only the geometry the tour actually references is written, and only if it is resident:
	// a soft reference that was never loaded has nothing to serialise.
	TArray<FTourPathData> Paths;
	TArray<FName> PathNames;
	Paths.Reserve(Preset->ReferencedPaths.Num());
	PathNames.Reserve(Preset->ReferencedPaths.Num());

	for (const TSoftObjectPtr<UTourPathPreset>& SoftPath : Preset->ReferencedPaths)
	{
		if (const UTourPathPreset* PathPreset = SoftPath.Get())
		{
			Paths.Add(PathPreset->PathData);
			PathNames.Add(PathPreset->GetFName());
		}
	}

	return SaveTourDataToSlot(
		Preset->Steps, Paths, PathNames, Preset->TourTitle,
		Preset->bLoopTour, Preset->GlobalTimeScale, SlotName, UserIndex);
}

bool UTourPersistenceLibrary::SaveTourDataToSlot(
	const TArray<FTourStep>& Steps,
	const TArray<FTourPathData>& Paths,
	const TArray<FName>& PathNames,
	const FText& TourTitle,
	bool bLoopTour,
	float GlobalTimeScale,
	const FString& SlotName,
	int32 UserIndex)
{
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("SaveTourDataToSlot: empty slot name."));
		return false;
	}

	if (Paths.Num() != PathNames.Num())
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("SaveTourDataToSlot('%s'): %d paths but %d names. The arrays are parallel and must match."),
			*SlotName, Paths.Num(), PathNames.Num());
		return false;
	}

	UTourSaveGame* Save = Cast<UTourSaveGame>(UGameplayStatics::CreateSaveGameObject(UTourSaveGame::StaticClass()));
	if (Save == nullptr)
	{
		UE_LOG(LogArchVizTour, Error, TEXT("SaveTourDataToSlot('%s'): could not create the save object."), *SlotName);
		return false;
	}

	Save->SchemaVersion         = CurrentSaveSchemaVersion;
	Save->TourTitle             = TourTitle;
	Save->Paths                 = Paths;
	Save->PathNames             = PathNames;
	Save->Steps                 = Steps;
	Save->bLoopTour             = bLoopTour;
	Save->GlobalTimeScale       = FMath::Max(GlobalTimeScale, UE_KINDA_SMALL_NUMBER);
	Save->SavedAtUtc            = FDateTime::UtcNow();
	Save->SavedByEngineVersion  = FEngineVersion::Current().ToString();

	if (!UGameplayStatics::SaveGameToSlot(Save, SlotName, UserIndex))
	{
		UE_LOG(LogArchVizTour, Error, TEXT("SaveTourDataToSlot('%s'): the write failed."), *SlotName);
		return false;
	}

	UE_LOG(LogArchVizTour, Log, TEXT("Saved tour to slot '%s': %d steps, %d paths."),
		*SlotName, Steps.Num(), Paths.Num());
	return true;
}

bool UTourPersistenceLibrary::LoadTourFromSlot(const FString& SlotName, int32 UserIndex, UTourSaveGame*& OutSave)
{
	OutSave = nullptr;

	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("LoadTourFromSlot('%s'): no such slot."), *SlotName);
		return false;
	}

	UTourSaveGame* Save = Cast<UTourSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	if (Save == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("LoadTourFromSlot('%s'): the slot exists but does not hold a tour save."), *SlotName);
		return false;
	}

	if (Save->SchemaVersion < CurrentSaveSchemaVersion)
	{
		const int32 LoadedVersion = Save->SchemaVersion;
		MigrateSaveGame(*Save, LoadedVersion);
		Save->SchemaVersion = CurrentSaveSchemaVersion;
		UE_LOG(LogArchVizTour, Log, TEXT("Migrated tour slot '%s' from schema %d to %d."),
			*SlotName, LoadedVersion, CurrentSaveSchemaVersion);
	}
	else if (Save->SchemaVersion > CurrentSaveSchemaVersion)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour slot '%s' was written with schema %d by engine version '%s'; this build understands only %d. Loading it as-is."),
			*SlotName, Save->SchemaVersion, *Save->SavedByEngineVersion, CurrentSaveSchemaVersion);
	}

	OutSave = Save;
	return true;
}

UTourSequencePreset* UTourPersistenceLibrary::LoadTourPresetFromSlot(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex)
{
	UTourSaveGame* Save = nullptr;
	if (!LoadTourFromSlot(SlotName, UserIndex, Save) || Save == nullptr)
	{
		return nullptr;
	}

	UTourSubsystem* Subsystem = UTourSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("LoadTourPresetFromSlot('%s'): no tour subsystem for the supplied world context."), *SlotName);
		return nullptr;
	}

	// Paths have to exist as actors before the steps can resolve, because FTourStep references
	// them by name exactly as it would reference a level-placed path.
	Subsystem->ClearRuntimePaths();
	for (int32 Index = 0; Index < Save->Paths.Num(); ++Index)
	{
		const FName PathName = Save->PathNames.IsValidIndex(Index) ? Save->PathNames[Index] : NAME_None;
		if (PathName == NAME_None)
		{
			UE_LOG(LogArchVizTour, Warning,
				TEXT("Tour slot '%s': path %d has no name and cannot be referenced by a step; skipping it."),
				*SlotName, Index);
			continue;
		}

		Subsystem->RegisterRuntimePath(PathName, Save->Paths[Index]);
	}

	// Transient: a packaged build cannot create an asset package, and the preset only has to
	// live as long as the tour that is playing it.
	UTourSequencePreset* Preset = NewObject<UTourSequencePreset>(Subsystem, NAME_None, RF_Transient);
	check(Preset != nullptr);

	Preset->Steps           = Save->Steps;
	Preset->bLoopTour       = Save->bLoopTour;
	Preset->GlobalTimeScale = FMath::Max(Save->GlobalTimeScale, UE_KINDA_SMALL_NUMBER);
	Preset->TourTitle       = Save->TourTitle;
	Preset->PlaybackBackend = ETourPlaybackBackend::Procedural;

	UE_LOG(LogArchVizTour, Log, TEXT("Rebuilt tour '%s' from slot '%s': %d steps, %d runtime paths."),
		*Preset->TourTitle.ToString(), *SlotName, Preset->Steps.Num(), Save->Paths.Num());

	return Preset;
}

TArray<FString> UTourPersistenceLibrary::EnumerateTourSlots()
{
	using namespace ArchVizTour::PersistencePrivate;

	TArray<FString> SlotNames;

	// UGameplayStatics has no enumeration entry point, so the slot directory is listed directly.
	// This is the same directory the engine's own save system writes to on every platform that
	// uses the generic save file system.
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(GetSaveGamesDirectory() / FString::Printf(TEXT("*.%s"), SlotExtension)), /*Files*/ true, /*Directories*/ false);

	SlotNames.Reserve(Files.Num());
	for (const FString& File : Files)
	{
		SlotNames.Add(FPaths::GetBaseFilename(File));
	}

	return SlotNames;
}

bool UTourPersistenceLibrary::DeleteTourSlot(const FString& SlotName, int32 UserIndex)
{
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		return false;
	}

	const bool bDeleted = UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	UE_LOG(LogArchVizTour, Log, TEXT("Delete tour slot '%s': %s."), *SlotName, bDeleted ? TEXT("removed") : TEXT("failed"));
	return bDeleted;
}

bool UTourPersistenceLibrary::DoesTourSlotExist(const FString& SlotName, int32 UserIndex)
{
	return UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex);
}

bool UTourPersistenceLibrary::MigrateSaveGame(UTourSaveGame& InOutSave, int32 FromVersion)
{
	bool bChanged = false;

	if (FromVersion < 1)
	{
		// Schema 0 predates per-preset schema versioning entirely, so its path data is written
		// in the tour path schema of the same era and has to walk the same chain.
		for (FTourPathData& PathData : InOutSave.Paths)
		{
			bChanged |= UTourPathPreset::MigratePathData(PathData, /*FromVersion*/ 1);
		}

		bChanged |= UTourSequencePreset::MigrateSteps(InOutSave.Steps, /*FromVersion*/ 1);

		if (InOutSave.GlobalTimeScale <= 0.0f)
		{
			InOutSave.GlobalTimeScale = 1.0f;
			bChanged = true;
		}
	}

	return bChanged;
}
