// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPathDetails.h"

#include "DesktopPlatformModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "TourGeometryLibrary.h"
#include "TourPath.h"
#include "TourPathPreset.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ArchVizTourEditor"

namespace ArchVizTourEditor::DetailsPrivate
{
	/** Spacing used by the Resample button, in centimetres. */
	static constexpr float ResampleSpacingCm = 100.0f;

	/** Blend weight used by the Smooth Tangents button. */
	static constexpr float SmoothStrength = 1.0f;

	/** Build one action button for the grid. */
	static TSharedRef<SWidget> MakeActionButton(const FText& Label, const FText& Tooltip, FOnClicked OnClicked)
	{
		return SNew(SButton)
			.Text(Label)
			.ToolTipText(Tooltip)
			.HAlign(HAlign_Center)
			.OnClicked(OnClicked);
	}
}

TSharedRef<IDetailCustomization> FTourPathDetails::MakeInstance()
{
	return MakeShared<FTourPathDetails>();
}

void FTourPathDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace ArchVizTourEditor::DetailsPrivate;

	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

	SelectedPaths.Reset();
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		if (ATourPath* Path = Cast<ATourPath>(Object.Get()))
		{
			SelectedPaths.Add(Path);
		}
	}

	if (SelectedPaths.Num() == 0)
	{
		return;
	}

	// --- Generation ----------------------------------------------------------
	IDetailCategoryBuilder& GenerationCategory = DetailBuilder.EditCategory(
		TEXT("Tour Path|Generation"),
		LOCTEXT("GenerationCategory", "Generation"),
		ECategoryPriority::Important);

	GenerationCategory.AddCustomRow(LOCTEXT("GenerateRowFilter", "Generate"))
	.WholeRowContent()
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2.0f))
		+ SUniformGridPanel::Slot(0, 0)
		[
			MakeActionButton(
				LOCTEXT("GenerateButton", "Generate"),
				LOCTEXT("GenerateButtonTooltip", "Rebuild this path from the generation parameters above, replacing its current points."),
				FOnClicked::CreateSP(this, &FTourPathDetails::OnGenerateClicked))
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			MakeActionButton(
				LOCTEXT("ResampleButton", "Resample Uniform"),
				LOCTEXT("ResampleButtonTooltip", "Redistribute the points so they are equally spaced along the curve, without changing its shape."),
				FOnClicked::CreateSP(this, &FTourPathDetails::OnResampleClicked))
		]
		+ SUniformGridPanel::Slot(2, 0)
		[
			MakeActionButton(
				LOCTEXT("SmoothButton", "Smooth Tangents"),
				LOCTEXT("SmoothButtonTooltip", "Replace every tangent with a Catmull-Rom estimate, removing kinks at hand-placed points."),
				FOnClicked::CreateSP(this, &FTourPathDetails::OnSmoothClicked))
		]
	];

	// --- Read-outs -----------------------------------------------------------
	IDetailCategoryBuilder& InfoCategory = DetailBuilder.EditCategory(
		TEXT("Tour Path|Info"),
		LOCTEXT("InfoCategory", "Path Info"),
		ECategoryPriority::Important);

	InfoCategory.AddCustomRow(LOCTEXT("LengthRowFilter", "Length"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("PathLengthLabel", "Length"))
	]
	.ValueContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(this, &FTourPathDetails::GetPathLengthText)
	];

	InfoCategory.AddCustomRow(LOCTEXT("DurationRowFilter", "Duration"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("PathDurationLabel", "Traversal Time"))
	]
	.ValueContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(this, &FTourPathDetails::GetPathDurationText)
		.ToolTipText(LOCTEXT("PathDurationTooltip", "How long the whole path takes at its authored speed, when a step leaves its duration at zero."))
	];

	// --- Preset --------------------------------------------------------------
	IDetailCategoryBuilder& PresetCategory = DetailBuilder.EditCategory(
		TEXT("Tour Path|Preset"),
		LOCTEXT("PresetCategory", "Preset"),
		ECategoryPriority::Important);

	PresetCategory.AddCustomRow(LOCTEXT("PresetRowFilter", "Preset"))
	.WholeRowContent()
	[
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2.0f))
		+ SUniformGridPanel::Slot(0, 0)
		[
			MakeActionButton(
				LOCTEXT("SaveToPresetButton", "Save To Preset"),
				LOCTEXT("SaveToPresetButtonTooltip", "Write this path's geometry into the linked Tour Path Preset asset."),
				FOnClicked::CreateSP(this, &FTourPathDetails::OnSaveToPresetClicked))
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			MakeActionButton(
				LOCTEXT("LoadFromPresetButton", "Load From Preset"),
				LOCTEXT("LoadFromPresetButtonTooltip", "Replace this path's geometry from the linked Tour Path Preset asset. The actor keeps its own placement."),
				FOnClicked::CreateSP(this, &FTourPathDetails::OnLoadFromPresetClicked))
		]
		+ SUniformGridPanel::Slot(2, 0)
		[
			MakeActionButton(
				LOCTEXT("ExportJsonButton", "Export JSON"),
				LOCTEXT("ExportJsonButtonTooltip", "Write this path to a JSON file that another project can import."),
				FOnClicked::CreateSP(this, &FTourPathDetails::OnExportJsonClicked))
		]
		+ SUniformGridPanel::Slot(3, 0)
		[
			MakeActionButton(
				LOCTEXT("ImportJsonButton", "Import JSON"),
				LOCTEXT("ImportJsonButtonTooltip", "Replace this path from a JSON file, applying schema migration if it was written by an older version."),
				FOnClicked::CreateSP(this, &FTourPathDetails::OnImportJsonClicked))
		]
	];
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

FReply FTourPathDetails::OnGenerateClicked()
{
	const FScopedTransaction Transaction(LOCTEXT("GenerateTourPathTransaction", "Generate Tour Path"));

	for (const TWeakObjectPtr<ATourPath>& WeakPath : SelectedPaths)
	{
		if (ATourPath* Path = WeakPath.Get())
		{
			Path->GenerateArcRail();
		}
	}

	return FReply::Handled();
}

FReply FTourPathDetails::OnResampleClicked()
{
	using namespace ArchVizTourEditor::DetailsPrivate;

	const FScopedTransaction Transaction(LOCTEXT("ResampleTourPathTransaction", "Resample Tour Path"));

	for (const TWeakObjectPtr<ATourPath>& WeakPath : SelectedPaths)
	{
		ATourPath* Path = WeakPath.Get();
		if (Path == nullptr)
		{
			continue;
		}

		Path->Modify();
		const FTourPathData Resampled = UTourGeometryLibrary::ResampleUniform(Path->BuildPathData(), ResampleSpacingCm);
		Path->ApplyPathData(Resampled, /*bApplyTransform*/ false);
	}

	return FReply::Handled();
}

FReply FTourPathDetails::OnSmoothClicked()
{
	using namespace ArchVizTourEditor::DetailsPrivate;

	const FScopedTransaction Transaction(LOCTEXT("SmoothTourPathTransaction", "Smooth Tour Path Tangents"));

	for (const TWeakObjectPtr<ATourPath>& WeakPath : SelectedPaths)
	{
		ATourPath* Path = WeakPath.Get();
		if (Path == nullptr)
		{
			continue;
		}

		Path->Modify();
		FTourPathData PathData = Path->BuildPathData();
		UTourGeometryLibrary::SmoothTangents(PathData, SmoothStrength);
		Path->ApplyPathData(PathData, /*bApplyTransform*/ false);
	}

	return FReply::Handled();
}

FReply FTourPathDetails::OnSaveToPresetClicked()
{
	const FScopedTransaction Transaction(LOCTEXT("SaveTourPathPresetTransaction", "Save Tour Path To Preset"));

	for (const TWeakObjectPtr<ATourPath>& WeakPath : SelectedPaths)
	{
		if (ATourPath* Path = WeakPath.Get())
		{
			Path->SaveToPreset();
		}
	}

	return FReply::Handled();
}

FReply FTourPathDetails::OnLoadFromPresetClicked()
{
	const FScopedTransaction Transaction(LOCTEXT("LoadTourPathPresetTransaction", "Load Tour Path From Preset"));

	for (const TWeakObjectPtr<ATourPath>& WeakPath : SelectedPaths)
	{
		if (ATourPath* Path = WeakPath.Get())
		{
			Path->LoadFromPreset();
		}
	}

	return FReply::Handled();
}

FReply FTourPathDetails::OnExportJsonClicked()
{
	ATourPath* Path = SelectedPaths.Num() > 0 ? SelectedPaths[0].Get() : nullptr;
	if (Path == nullptr)
	{
		return FReply::Handled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return FReply::Handled();
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		LOCTEXT("ExportTourPathTitle", "Export Tour Path").ToString(),
		FPaths::ProjectSavedDir(),
		Path->GetActorNameOrLabel() + TEXT(".json"),
		TEXT("Tour Path JSON (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (!bPicked || OutFiles.Num() == 0)
	{
		return FReply::Handled();
	}

	// A transient preset is the serialiser; exporting does not require the path to be linked to
	// an asset, which is the whole point of a JSON export.
	UTourPathPreset* Scratch = NewObject<UTourPathPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	Scratch->PathData = Path->BuildPathData();
	Scratch->DisplayName = FText::FromString(Path->GetActorNameOrLabel());
	Scratch->ExportToJson(OutFiles[0]);

	return FReply::Handled();
}

FReply FTourPathDetails::OnImportJsonClicked()
{
	ATourPath* Path = SelectedPaths.Num() > 0 ? SelectedPaths[0].Get() : nullptr;
	if (Path == nullptr)
	{
		return FReply::Handled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr)
	{
		return FReply::Handled();
	}

	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);

	TArray<FString> OutFiles;
	const bool bPicked = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		LOCTEXT("ImportTourPathTitle", "Import Tour Path").ToString(),
		FPaths::ProjectSavedDir(),
		FString(),
		TEXT("Tour Path JSON (*.json)|*.json"),
		EFileDialogFlags::None,
		OutFiles);

	if (!bPicked || OutFiles.Num() == 0)
	{
		return FReply::Handled();
	}

	UTourPathPreset* Scratch = NewObject<UTourPathPreset>(GetTransientPackage(), NAME_None, RF_Transient);
	if (Scratch->ImportFromJson(OutFiles[0]))
	{
		const FScopedTransaction Transaction(LOCTEXT("ImportTourPathTransaction", "Import Tour Path"));
		Path->Modify();
		// The actor's own placement wins over the exported transform, so an imported path lands
		// where the actor already is rather than teleporting to wherever it was authored.
		Path->ApplyPathData(Scratch->PathData, /*bApplyTransform*/ false);
	}

	return FReply::Handled();
}

// ---------------------------------------------------------------------------
// Read-outs
// ---------------------------------------------------------------------------

FText FTourPathDetails::GetPathLengthText() const
{
	const ATourPath* Path = SelectedPaths.Num() > 0 ? SelectedPaths[0].Get() : nullptr;
	if (Path == nullptr)
	{
		return FText::GetEmpty();
	}

	const float LengthCm = Path->GetPathLength();
	return FText::Format(
		LOCTEXT("PathLengthValue", "{0} cm  ({1} m)"),
		FText::AsNumber(FMath::RoundToInt(LengthCm)),
		FText::AsNumber(LengthCm / 100.0f, &FNumberFormattingOptions::DefaultWithGrouping()));
}

FText FTourPathDetails::GetPathDurationText() const
{
	const ATourPath* Path = SelectedPaths.Num() > 0 ? SelectedPaths[0].Get() : nullptr;
	if (Path == nullptr)
	{
		return FText::GetEmpty();
	}

	const float LengthCm = Path->GetPathLength();
	const float Speed = FMath::Max(Path->GetSpeedAtDistance(LengthCm * 0.5f), UE_KINDA_SMALL_NUMBER);

	return FText::Format(
		LOCTEXT("PathDurationValue", "{0} s at {1} cm/s"),
		FText::AsNumber(LengthCm / Speed),
		FText::AsNumber(FMath::RoundToInt(Speed)));
}

#undef LOCTEXT_NAMESPACE
