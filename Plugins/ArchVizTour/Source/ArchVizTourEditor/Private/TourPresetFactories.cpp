// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPresetFactories.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "TourEditorSettings.h"
#include "TourGeometryLibrary.h"
#include "TourInputConfig.h"
#include "TourPathPreset.h"
#include "TourSequencePreset.h"

#define LOCTEXT_NAMESPACE "ArchVizTourEditor"

namespace ArchVizTourEditor::FactoryPrivate
{
	/** The plugin's Content Browser category, created once and shared by every factory. */
	static uint32 GetTourAssetCategory()
	{
		static EAssetTypeCategories::Type Category = EAssetTypeCategories::None;

		if (Category == EAssetTypeCategories::None)
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			Category = AssetTools.RegisterAdvancedAssetCategory(
				FName(TEXT("ArchVizTour")),
				LOCTEXT("ArchVizTourAssetCategory", "ArchViz Tour"));
		}

		return Category;
	}
}

// ---------------------------------------------------------------------------
// Path preset
// ---------------------------------------------------------------------------

UTourPathPresetFactory::UTourPathPresetFactory()
{
	SupportedClass = UTourPathPreset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FText UTourPathPresetFactory::GetDisplayName() const
{
	return LOCTEXT("TourPathPresetFactoryName", "Tour Path Preset");
}

uint32 UTourPathPresetFactory::GetMenuCategories() const
{
	return ArchVizTourEditor::FactoryPrivate::GetTourAssetCategory();
}

UObject* UTourPathPresetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UTourPathPreset* Preset = NewObject<UTourPathPreset>(InParent, InClass, InName, Flags);
	check(Preset != nullptr);

	const UTourEditorSettings& Settings = UTourEditorSettings::Get();

	// A brand new preset with no points is useless and produces warnings the moment it is used,
	// so it starts as a usable quarter arc at the project's default radius.
	Preset->PathData = UTourGeometryLibrary::GenerateArc(
		FVector::ZeroVector,
		Settings.DefaultArcRadius,
		/*StartAngleDeg*/ 0.0f,
		/*SweepAngleDeg*/ 90.0f,
		Settings.DefaultArcPointCount,
		FVector::UpVector,
		/*HeightDelta*/ 0.0f,
		FTransform::Identity);

	Preset->PathData.DefaultSpeed = Settings.DefaultPathSpeed;
	for (FTourPoint& Point : Preset->PathData.Points)
	{
		Point.FocalLength = Settings.DefaultFocalLength;
		Point.Speed = Settings.DefaultPathSpeed;
	}

	Preset->DisplayName = FText::FromName(InName);
	return Preset;
}

// ---------------------------------------------------------------------------
// Sequence preset
// ---------------------------------------------------------------------------

UTourSequencePresetFactory::UTourSequencePresetFactory()
{
	SupportedClass = UTourSequencePreset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FText UTourSequencePresetFactory::GetDisplayName() const
{
	return LOCTEXT("TourSequencePresetFactoryName", "Tour Sequence Preset");
}

uint32 UTourSequencePresetFactory::GetMenuCategories() const
{
	return ArchVizTourEditor::FactoryPrivate::GetTourAssetCategory();
}

UObject* UTourSequencePresetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UTourSequencePreset* Preset = NewObject<UTourSequencePreset>(InParent, InClass, InName, Flags);
	check(Preset != nullptr);

	Preset->TourTitle = FText::FromName(InName);

	// One placeholder step, so the asset opens with something to edit rather than an empty array
	// and a "tour has no steps" warning.
	FTourStep Step;
	Step.StepType  = ETourStepType::SplineMove;
	Step.Label     = LOCTEXT("DefaultTourStepLabel", "New Step");
	Step.Duration  = 0.0f;
	Step.BlendTime = UTourEditorSettings::Get().DefaultPathSpeed > 0.0f ? 1.0f : 0.0f;
	Preset->Steps.Add(Step);

	return Preset;
}

// ---------------------------------------------------------------------------
// Input config
// ---------------------------------------------------------------------------

UTourInputConfigFactory::UTourInputConfigFactory()
{
	SupportedClass = UTourInputConfig::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

FText UTourInputConfigFactory::GetDisplayName() const
{
	return LOCTEXT("TourInputConfigFactoryName", "Tour Input Config");
}

uint32 UTourInputConfigFactory::GetMenuCategories() const
{
	return ArchVizTourEditor::FactoryPrivate::GetTourAssetCategory();
}

UObject* UTourInputConfigFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UTourInputConfig>(InParent, InClass, InName, Flags);
}

#undef LOCTEXT_NAMESPACE
