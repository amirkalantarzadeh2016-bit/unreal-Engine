// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourSequencePreset.h"

#include "ArchVizTourLog.h"
#include "TourJsonUtils.h"
#include "TourPathPreset.h"

UTourSequencePreset::UTourSequencePreset()
{
	SchemaVersion = CurrentSchemaVersion;
}

void UTourSequencePreset::PostInitProperties()
{
	Super::PostInitProperties();

	if (!PresetId.IsValid() && !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		PresetId = FGuid::NewGuid();
	}
}

void UTourSequencePreset::PostLoad()
{
	Super::PostLoad();

	if (SchemaVersion < CurrentSchemaVersion)
	{
		const int32 LoadedVersion = SchemaVersion;
		if (MigrateSteps(Steps, LoadedVersion))
		{
			UE_LOG(LogArchVizTour, Log,
				TEXT("Migrated tour sequence preset '%s' from schema %d to %d."),
				*GetName(), LoadedVersion, CurrentSchemaVersion);
		}

		SchemaVersion = CurrentSchemaVersion;
	}
	else if (SchemaVersion > CurrentSchemaVersion)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour sequence preset '%s' was saved with schema %d but this build understands only %d. Loading it as-is; newer fields are ignored."),
			*GetName(), SchemaVersion, CurrentSchemaVersion);
	}

	if (!PresetId.IsValid() && !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		PresetId = FGuid::NewGuid();
	}
}

FPrimaryAssetId UTourSequencePreset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("TourSequencePreset"), GetFName());
}

bool UTourSequencePreset::MigrateSteps(TArray<FTourStep>& InOutSteps, int32 FromVersion)
{
	bool bChanged = false;

	if (FromVersion < 2)
	{
		// Schema 1 had no blend fields, so every transition was a hard cut. The struct defaults
		// are a one-second cubic blend, which would silently retime every existing tour; force
		// the historical behaviour instead and let the author opt back in.
		for (FTourStep& Step : InOutSteps)
		{
			Step.BlendTime     = 0.0f;
			Step.BlendFunction = VTBlend_Linear;
			Step.BlendExp      = 0.0f;
		}
		bChanged = InOutSteps.Num() > 0;
	}

	return bChanged;
}

float UTourSequencePreset::GetTotalDuration() const
{
	const float TimeScale = FMath::Max(GlobalTimeScale, UE_KINDA_SMALL_NUMBER);

	float Total = 0.0f;
	for (const FTourStep& Step : Steps)
	{
		Total += FMath::Max(Step.Duration, 0.0f);
	}

	return Total / TimeScale;
}

bool UTourSequencePreset::ToJsonString(FString& OutJson) const
{
	return ArchVizTour::Json::ObjectToJsonString(this, OutJson);
}

bool UTourSequencePreset::FromJsonString(const FString& InJson)
{
	const int32 IncomingVersion = ArchVizTour::Json::PeekSchemaVersion(InJson);

	if (!ArchVizTour::Json::JsonStringToObject(InJson, this))
	{
		return false;
	}

	if (IncomingVersion != INDEX_NONE && IncomingVersion < CurrentSchemaVersion)
	{
		MigrateSteps(Steps, IncomingVersion);
		SchemaVersion = CurrentSchemaVersion;
	}
	else if (IncomingVersion == INDEX_NONE)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour sequence JSON carried no schema version; assuming it matches the current schema (%d)."),
			CurrentSchemaVersion);
		SchemaVersion = CurrentSchemaVersion;
	}

	if (!PresetId.IsValid())
	{
		PresetId = FGuid::NewGuid();
	}

	return true;
}

bool UTourSequencePreset::ExportToJson(const FString& AbsolutePath) const
{
	FString Json;
	if (!ToJsonString(Json))
	{
		return false;
	}

	return ArchVizTour::Json::SaveStringToFile(AbsolutePath, Json);
}

bool UTourSequencePreset::ImportFromJson(const FString& AbsolutePath)
{
	FString Json;
	if (!ArchVizTour::Json::LoadStringFromFile(AbsolutePath, Json))
	{
		return false;
	}

	UTourSequencePreset* Scratch = NewObject<UTourSequencePreset>(GetTransientPackage(), NAME_None, RF_Transient);
	check(Scratch != nullptr);

	if (!Scratch->FromJsonString(Json))
	{
		return false;
	}

#if WITH_EDITOR
	Modify();
#endif

	Steps           = Scratch->Steps;
	ReferencedPaths = Scratch->ReferencedPaths;
	bLoopTour       = Scratch->bLoopTour;
	GlobalTimeScale = Scratch->GlobalTimeScale;
	TourTitle       = Scratch->TourTitle;
	BakedSequence   = Scratch->BakedSequence;
	PlaybackBackend = Scratch->PlaybackBackend;
	SchemaVersion   = Scratch->SchemaVersion;

	if (!PresetId.IsValid())
	{
		PresetId = Scratch->PresetId.IsValid() ? Scratch->PresetId : FGuid::NewGuid();
	}

#if WITH_EDITOR
	MarkPackageDirty();
#endif

	UE_LOG(LogArchVizTour, Log, TEXT("Imported tour sequence preset '%s' from '%s' (%d steps)."),
		*GetName(), *AbsolutePath, Steps.Num());
	return true;
}
