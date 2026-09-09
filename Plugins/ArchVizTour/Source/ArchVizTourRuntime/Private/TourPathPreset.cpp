// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPathPreset.h"

#include "ArchVizTourLog.h"
#include "TourJsonUtils.h"

UTourPathPreset::UTourPathPreset()
{
	SchemaVersion = CurrentSchemaVersion;
}

void UTourPathPreset::PostInitProperties()
{
	Super::PostInitProperties();

	// Class-default and archetype objects must not carry an identity: the CDO's GUID would be
	// copied into every asset created from it, defeating the point of a stable unique id.
	if (!PresetId.IsValid() && !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		PresetId = FGuid::NewGuid();
	}
}

void UTourPathPreset::PostLoad()
{
	Super::PostLoad();

	if (SchemaVersion < CurrentSchemaVersion)
	{
		const int32 LoadedVersion = SchemaVersion;
		if (MigratePathData(PathData, LoadedVersion))
		{
			UE_LOG(LogArchVizTour, Log,
				TEXT("Migrated tour path preset '%s' from schema %d to %d."),
				*GetName(), LoadedVersion, CurrentSchemaVersion);
		}

		SchemaVersion = CurrentSchemaVersion;

		// PostLoad runs during load, so the package must not be dirtied here: the asset is
		// rewritten the next time the user saves it, which is the standard migration contract.
	}
	else if (SchemaVersion > CurrentSchemaVersion)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path preset '%s' was saved with schema %d but this build understands only %d. Loading it as-is; newer fields are ignored."),
			*GetName(), SchemaVersion, CurrentSchemaVersion);
	}

	if (!PresetId.IsValid() && !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		PresetId = FGuid::NewGuid();
	}
}

FPrimaryAssetId UTourPathPreset::GetPrimaryAssetId() const
{
	// A fixed asset type (rather than the class name) keeps the Asset Manager rules stable if
	// the class is ever renamed or subclassed.
	return FPrimaryAssetId(TEXT("TourPathPreset"), GetFName());
}

bool UTourPathPreset::MigratePathData(FTourPathData& InOutPathData, int32 FromVersion)
{
	bool bChanged = false;

	// Each block upgrades exactly one version step and falls through, so a version-1 asset walks
	// the whole chain. Never collapse these into a single "if old" branch: that breaks the
	// moment a third version is added.

	if (FromVersion < 2)
	{
		// Schema 1 stored speed as centimetres per frame at an assumed 30 fps. Rescaling here
		// keeps an old path moving at the same real-world speed instead of crawling 30x slower.
		constexpr float LegacyAssumedFrameRate = 30.0f;

		InOutPathData.DefaultSpeed *= LegacyAssumedFrameRate;
		for (FTourPoint& Point : InOutPathData.Points)
		{
			Point.Speed = (Point.Speed > 0.0f) ? Point.Speed * LegacyAssumedFrameRate : InOutPathData.DefaultSpeed;
		}
		bChanged = true;
	}

	if (FromVersion < 3)
	{
		// Schema 2 had no bUseExplicitRotation: a non-identity Rotation *was* the opt-in.
		// Reproducing that rule preserves the authored look of existing paths.
		for (FTourPoint& Point : InOutPathData.Points)
		{
			Point.bUseExplicitRotation = !Point.bUseLookAtTarget && !Point.Rotation.IsNearlyZero();
		}
		bChanged = true;
	}

	return bChanged;
}

bool UTourPathPreset::ToJsonString(FString& OutJson) const
{
	return ArchVizTour::Json::ObjectToJsonString(this, OutJson);
}

bool UTourPathPreset::FromJsonString(const FString& InJson)
{
	const int32 IncomingVersion = ArchVizTour::Json::PeekSchemaVersion(InJson);

	if (!ArchVizTour::Json::JsonStringToObject(InJson, this))
	{
		return false;
	}

	if (IncomingVersion != INDEX_NONE && IncomingVersion < CurrentSchemaVersion)
	{
		MigratePathData(PathData, IncomingVersion);
		SchemaVersion = CurrentSchemaVersion;
	}
	else if (IncomingVersion == INDEX_NONE)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path JSON carried no schema version; assuming it matches the current schema (%d)."),
			CurrentSchemaVersion);
		SchemaVersion = CurrentSchemaVersion;
	}

	if (!PresetId.IsValid())
	{
		PresetId = FGuid::NewGuid();
	}

	return true;
}

bool UTourPathPreset::ExportToJson(const FString& AbsolutePath) const
{
	FString Json;
	if (!ToJsonString(Json))
	{
		return false;
	}

	return ArchVizTour::Json::SaveStringToFile(AbsolutePath, Json);
}

bool UTourPathPreset::ImportFromJson(const FString& AbsolutePath)
{
	FString Json;
	if (!ArchVizTour::Json::LoadStringFromFile(AbsolutePath, Json))
	{
		return false;
	}

	// Parse into a scratch object first: a malformed payload must not leave the real asset
	// half-overwritten, which is exactly what a direct in-place parse would do.
	UTourPathPreset* Scratch = NewObject<UTourPathPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	check(Scratch != nullptr);

	if (!Scratch->FromJsonString(Json))
	{
		return false;
	}

#if WITH_EDITOR
	Modify();
#endif

	PathData      = Scratch->PathData;
	DisplayName   = Scratch->DisplayName;
	ThumbnailPath = Scratch->ThumbnailPath;
	SchemaVersion = Scratch->SchemaVersion;
	// PresetId is identity, not content: importing content into an existing asset must not
	// change which asset it is.
	if (!PresetId.IsValid())
	{
		PresetId = Scratch->PresetId.IsValid() ? Scratch->PresetId : FGuid::NewGuid();
	}

#if WITH_EDITOR
	MarkPackageDirty();
#endif

	UE_LOG(LogArchVizTour, Log, TEXT("Imported tour path preset '%s' from '%s' (%d points)."),
		*GetName(), *AbsolutePath, PathData.Points.Num());
	return true;
}
