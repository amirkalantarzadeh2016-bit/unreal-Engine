// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBPAIBridgePanel.h"

#include "AssetRegistry/AssetData.h"
#include "BPDiffEngine.h"
#include "BPExporter.h"
#include "BPImporter.h"
#include "BPSnapshotStore.h"
#include "BlueprintAIBridgeModule.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BlueprintAIBridge"

namespace SBPAIBridgePanelInternal
{
	const TCHAR* AllGraphsOption = TEXT("All Graphs");

	const FLinearColor StatusNeutral(0.75f, 0.75f, 0.75f);
	const FLinearColor StatusSuccess(0.25f, 0.8f, 0.35f);
	const FLinearColor StatusError(0.9f, 0.3f, 0.25f);

	/** Colour used for a diff row's +/~/- glyph. */
	FLinearColor GlyphColor(EBPDiffType Type)
	{
		switch (Type)
		{
		case EBPDiffType::NodeAdded:
		case EBPDiffType::ConnectionAdded:
		case EBPDiffType::VariableAdded:
			return FLinearColor(0.25f, 0.8f, 0.35f);
		case EBPDiffType::NodeDeleted:
		case EBPDiffType::ConnectionRemoved:
		case EBPDiffType::VariableRemoved:
			return FLinearColor(0.9f, 0.3f, 0.25f);
		default:
			return FLinearColor(0.95f, 0.8f, 0.2f);
		}
	}

	/** Maps a combo entry back to the enum. Order must match the strings pushed in Construct. */
	EExportContext ContextFromString(const FString& Label)
	{
		if (Label == TEXT("Bug Fix"))         { return EExportContext::BugFix; }
		if (Label == TEXT("Refactor"))        { return EExportContext::Refactor; }
		if (Label == TEXT("Feature Request")) { return EExportContext::FeatureRequest; }
		if (Label == TEXT("Code Review"))     { return EExportContext::CodeReview; }
		return EExportContext::General;
	}
}

using namespace SBPAIBridgePanelInternal;

void SBPAIBridgePanel::Construct(const FArguments& InArgs)
{
	ContextOptions.Add(MakeShared<FString>(TEXT("Bug Fix")));
	ContextOptions.Add(MakeShared<FString>(TEXT("Refactor")));
	ContextOptions.Add(MakeShared<FString>(TEXT("Feature Request")));
	ContextOptions.Add(MakeShared<FString>(TEXT("Code Review")));
	ContextOptions.Add(MakeShared<FString>(TEXT("General")));
	SelectedContextOption = ContextOptions.Last();

	RefreshGraphOptions();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SScrollBox)

			// ---- Source ------------------------------------------------------------------
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Text(LOCTEXT("SourceHeading", "Source Blueprint"))
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UBlueprint::StaticClass())
					.ObjectPath(this, &SBPAIBridgePanel::GetBlueprintPath)
					.OnObjectChanged(this, &SBPAIBridgePanel::OnBlueprintChanged)
					.AllowClear(true)
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&ContextOptions)
					.InitiallySelectedItem(SelectedContextOption)
					.OnGenerateWidget(this, &SBPAIBridgePanel::MakeComboItemWidget)
					.OnSelectionChanged(this, &SBPAIBridgePanel::OnContextSelectionChanged)
					.ToolTipText(LOCTEXT("ContextTooltip", "Why you are handing this Blueprint to an AI. Shapes the generated prompt."))
					[
						SNew(STextBlock).Text(this, &SBPAIBridgePanel::GetContextComboText)
					]
				]
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SAssignNew(GraphCombo, SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&GraphOptions)
					.OnGenerateWidget(this, &SBPAIBridgePanel::MakeComboItemWidget)
					.OnSelectionChanged(this, &SBPAIBridgePanel::OnGraphSelectionChanged)
					.ToolTipText(LOCTEXT("GraphScopeTooltip", "Export every graph, or one graph at a time for large Blueprints."))
					[
						SNew(STextBlock).Text(this, &SBPAIBridgePanel::GetGraphComboText)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ExportButton", "Export for AI"))
					.ToolTipText(LOCTEXT("ExportTooltip", "Snapshot the selected graphs as JSON."))
					.IsEnabled(this, &SBPAIBridgePanel::HasBlueprint)
					.OnClicked(this, &SBPAIBridgePanel::OnExportClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyButton", "Copy to Clipboard"))
					.ToolTipText(LOCTEXT("CopyTooltip", "Copy the prompt prefix and the export JSON together."))
					.IsEnabled(this, &SBPAIBridgePanel::HasExport)
					.OnClicked(this, &SBPAIBridgePanel::OnCopyToClipboardClicked)
				]
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			// ---- Task description --------------------------------------------------------
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Text(LOCTEXT("TaskHeading", "Task Description"))
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SAssignNew(TaskDescBox, SMultiLineEditableTextBox)
				.AutoWrapText(true)
				.HintText(LOCTEXT("TaskHint", "Describe what you want the AI to change, e.g. \"the door never closes after the first use\"."))
			]

			// ---- AI response -------------------------------------------------------------
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Text(LOCTEXT("ResponseHeading", "AI Response JSON"))
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SAssignNew(AIResponseBox, SMultiLineEditableTextBox)
				.AutoWrapText(false)
				.HintText(LOCTEXT("ResponseHint", "Paste the AI's JSON reply here."))
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("AnalyzeButton", "Analyze Diff"))
				.IsEnabled(this, &SBPAIBridgePanel::HasExport)
				.OnClicked(this, &SBPAIBridgePanel::OnAnalyzeDiffClicked)
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			// ---- Diff preview ------------------------------------------------------------
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Text(LOCTEXT("DiffHeading", "Diff Preview"))
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBox)
				.HeightOverride(220.0f)
				[
					SAssignNew(DiffListView, SListView<TSharedPtr<FBPDiffItem>>)
					.ListItemsSource(&DiffListSource)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SBPAIBridgePanel::GenerateDiffRow)
				]
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SelectAllButton", "Select All"))
					.IsEnabled(this, &SBPAIBridgePanel::HasDiff)
					.OnClicked(this, &SBPAIBridgePanel::OnSelectAllClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("DeselectAllButton", "Deselect All"))
					.IsEnabled(this, &SBPAIBridgePanel::HasDiff)
					.OnClicked(this, &SBPAIBridgePanel::OnDeselectAllClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyButton", "Apply Selected Changes"))
					.ToolTipText(LOCTEXT("ApplyTooltip", "Apply the ticked changes to the Blueprint. Undoable with Ctrl+Z."))
					.IsEnabled(this, &SBPAIBridgePanel::HasDiff)
					.OnClicked(this, &SBPAIBridgePanel::OnApplyChangesClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("RevertButton", "Revert to Snapshot"))
					.ToolTipText(LOCTEXT("RevertTooltip", "Roll the Blueprint back to the most recent snapshot on disk."))
					.IsEnabled(this, &SBPAIBridgePanel::HasBlueprint)
					.OnClicked(this, &SBPAIBridgePanel::OnRevertSnapshotClicked)
				]
			]

			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			// ---- Status ------------------------------------------------------------------
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Text(LOCTEXT("StatusHeading", "Status / Log"))
			]

			+ SScrollBox::Slot()
			[
				SAssignNew(StatusText, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(StatusNeutral))
				.Text(LOCTEXT("StatusIdle", "Pick a Blueprint and click Export for AI."))
			]
		]
	];
}

// ---------------------------------------------------------------------------------------
// Blueprint picker
// ---------------------------------------------------------------------------------------

FString SBPAIBridgePanel::GetBlueprintPath() const
{
	return SelectedBlueprint.IsValid() ? SelectedBlueprint->GetPathName() : FString();
}

void SBPAIBridgePanel::OnBlueprintChanged(const FAssetData& AssetData)
{
	SelectedBlueprint = Cast<UBlueprint>(AssetData.GetAsset());

	// A different Blueprint invalidates everything downstream of the export.
	LastExportJson.Reset();
	LastSnapshotPath.Reset();
	ClearDiff();
	RefreshGraphOptions();

	if (GraphCombo.IsValid())
	{
		GraphCombo->RefreshOptions();
		GraphCombo->SetSelectedItem(SelectedGraphOption);
	}

	if (SelectedBlueprint.IsValid())
	{
		UpdateStatus(FString::Printf(TEXT("Selected '%s'."), *SelectedBlueprint->GetName()));
	}
	else
	{
		UpdateStatus(TEXT("No Blueprint selected."));
	}
}

void SBPAIBridgePanel::RefreshGraphOptions()
{
	GraphOptions.Reset();
	GraphOptions.Add(MakeShared<FString>(AllGraphsOption));

	if (SelectedBlueprint.IsValid())
	{
		TArray<UEdGraph*> Graphs;
		FBPExporter::CollectGraphs(SelectedBlueprint.Get(), Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph != nullptr)
			{
				GraphOptions.Add(MakeShared<FString>(Graph->GetName()));
			}
		}
	}

	SelectedGraphOption = GraphOptions[0];
}

// ---------------------------------------------------------------------------------------
// Combo boxes
// ---------------------------------------------------------------------------------------

TSharedRef<SWidget> SBPAIBridgePanel::MakeComboItemWidget(TSharedPtr<FString> Item) const
{
	return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
}

void SBPAIBridgePanel::OnContextSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (!NewSelection.IsValid())
	{
		return;
	}

	SelectedContextOption = NewSelection;
	SelectedContext = ContextFromString(*NewSelection);
}

void SBPAIBridgePanel::OnGraphSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		SelectedGraphOption = NewSelection;
	}
}

FText SBPAIBridgePanel::GetContextComboText() const
{
	return FText::FromString(SelectedContextOption.IsValid() ? *SelectedContextOption : TEXT("General"));
}

FText SBPAIBridgePanel::GetGraphComboText() const
{
	return FText::FromString(SelectedGraphOption.IsValid() ? *SelectedGraphOption : FString(AllGraphsOption));
}

// ---------------------------------------------------------------------------------------
// Enablement
// ---------------------------------------------------------------------------------------

bool SBPAIBridgePanel::HasBlueprint() const
{
	return SelectedBlueprint.IsValid();
}

bool SBPAIBridgePanel::HasExport() const
{
	return SelectedBlueprint.IsValid() && !LastExportJson.IsEmpty();
}

bool SBPAIBridgePanel::HasDiff() const
{
	return CurrentDiff.Num() > 0;
}

// ---------------------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------------------

FBPExportOptions SBPAIBridgePanel::MakeExportOptions() const
{
	FBPExportOptions Options;
	Options.Context = SelectedContext;

	// GraphOptions[0] is "All Graphs", so entry N+1 is graph N of the Blueprint's graph list.
	const int32 OptionIndex = SelectedGraphOption.IsValid() ? GraphOptions.IndexOfByKey(SelectedGraphOption) : 0;
	if (OptionIndex > 0)
	{
		// Chunked mode indexes into the unfiltered list, which is what lets the prompt tell the
		// AI "this is graph 3 of 7" rather than "graph 1 of 1".
		Options.bChunkedMode = true;
		Options.ChunkGraphIndex = OptionIndex - 1;
	}

	return Options;
}

FReply SBPAIBridgePanel::OnExportClicked()
{
	UBlueprint* Blueprint = SelectedBlueprint.Get();
	if (Blueprint == nullptr)
	{
		UpdateStatus(TEXT("Select a Blueprint first."), /*bIsError*/ true);
		return FReply::Handled();
	}

	const FBPExportOptions Options = MakeExportOptions();
	LastExportJson = FBPExporter::ExportBlueprint(Blueprint, Options);

	if (LastExportJson.IsEmpty())
	{
		UpdateStatus(TEXT("Export failed. See the Output Log (LogBlueprintAIBridge) for details."), /*bIsError*/ true);
		return FReply::Handled();
	}

	ClearDiff();

	LastSnapshotPath = FBPSnapshotStore::SaveSnapshot(Blueprint->GetPathName(), LastExportJson);

	if (LastSnapshotPath.IsEmpty())
	{
		UpdateStatus(
			FString::Printf(TEXT("Exported %d characters, but the snapshot could not be written. Revert will be unavailable."), LastExportJson.Len()),
			/*bIsError*/ true);
	}
	else
	{
		UpdateStatus(FString::Printf(
			TEXT("Exported %d characters. Snapshot saved to %s"), LastExportJson.Len(), *LastSnapshotPath));
	}

	return FReply::Handled();
}

FString SBPAIBridgePanel::BuildFullExportPayload()
{
	const FString TaskText = TaskDescBox.IsValid() ? TaskDescBox->GetText().ToString() : FString();

	const FString Prefix = FBPExporter::BuildPromptPrefix(SelectedBlueprint.Get(), MakeExportOptions(), TaskText);

	return FString::Printf(TEXT("%s\n---\n```json\n%s\n```\n"), *Prefix, *LastExportJson);
}

FReply SBPAIBridgePanel::OnCopyToClipboardClicked()
{
	if (LastExportJson.IsEmpty())
	{
		UpdateStatus(TEXT("Nothing to copy. Export first."), /*bIsError*/ true);
		return FReply::Handled();
	}

	const FString Payload = BuildFullExportPayload();
	FPlatformApplicationMisc::ClipboardCopy(*Payload);

	UpdateStatus(FString::Printf(TEXT("Copied %d characters (prompt + JSON) to the clipboard."), Payload.Len()));
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------
// Diff
// ---------------------------------------------------------------------------------------

void SBPAIBridgePanel::ClearDiff()
{
	CurrentDiff.Reset();
	DiffItemAccepted.Reset();
	DiffListSource.Reset();

	if (DiffListView.IsValid())
	{
		DiffListView->RequestListRefresh();
	}
}

FReply SBPAIBridgePanel::OnAnalyzeDiffClicked()
{
	if (LastExportJson.IsEmpty())
	{
		UpdateStatus(TEXT("Export the Blueprint before analysing a response."), /*bIsError*/ true);
		return FReply::Handled();
	}

	const FString ResponseText = AIResponseBox.IsValid() ? AIResponseBox->GetText().ToString() : FString();
	if (ResponseText.TrimStartAndEnd().IsEmpty())
	{
		UpdateStatus(TEXT("Paste the AI's JSON response first."), /*bIsError*/ true);
		return FReply::Handled();
	}

	ClearDiff();

	FString Error;
	CurrentDiff = FBPDiffEngine::ComputeDiff(LastExportJson, ResponseText, &Error);

	if (!Error.IsEmpty())
	{
		UpdateStatus(Error, /*bIsError*/ true);
		return FReply::Handled();
	}

	DiffItemAccepted.Init(true, CurrentDiff.Num());
	DiffListSource.Reserve(CurrentDiff.Num());
	for (const FBPDiffItem& Item : CurrentDiff)
	{
		DiffListSource.Add(MakeShared<FBPDiffItem>(Item));
	}

	if (DiffListView.IsValid())
	{
		DiffListView->RequestListRefresh();
	}

	if (CurrentDiff.Num() == 0)
	{
		UpdateStatus(TEXT("The response parsed cleanly but proposes no changes."));
	}
	else
	{
		UpdateStatus(FString::Printf(TEXT("%d proposed change(s). Review them, then apply."), CurrentDiff.Num()));
	}

	return FReply::Handled();
}

TSharedRef<ITableRow> SBPAIBridgePanel::GenerateDiffRow(
	TSharedPtr<FBPDiffItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const EBPDiffType Type = Item.IsValid() ? Item->Type : EBPDiffType::NodeModified;

	return SNew(STableRow<TSharedPtr<FBPDiffItem>>, OwnerTable)
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked(this, &SBPAIBridgePanel::IsDiffItemAccepted, Item)
				.OnCheckStateChanged(this, &SBPAIBridgePanel::OnDiffItemAcceptedChanged, Item)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FBPDiffEngine::DiffTypeToGlyph(Type)))
				.ColorAndOpacity(FSlateColor(GlyphColor(Type)))
				.ToolTipText(FText::FromString(FBPDiffEngine::DiffTypeToString(Type)))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(FText::FromString(Item.IsValid() ? Item->Description : FString()))
			]
		];
}

ECheckBoxState SBPAIBridgePanel::IsDiffItemAccepted(TSharedPtr<FBPDiffItem> Item) const
{
	const int32 Index = DiffListSource.IndexOfByKey(Item);
	const bool bAccepted = DiffItemAccepted.IsValidIndex(Index) && DiffItemAccepted[Index];
	return bAccepted ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SBPAIBridgePanel::OnDiffItemAcceptedChanged(ECheckBoxState NewState, TSharedPtr<FBPDiffItem> Item)
{
	const int32 Index = DiffListSource.IndexOfByKey(Item);
	if (DiffItemAccepted.IsValidIndex(Index))
	{
		DiffItemAccepted[Index] = (NewState == ECheckBoxState::Checked);
	}
}

void SBPAIBridgePanel::SetAllAccepted(bool bAccepted)
{
	for (bool& Accepted : DiffItemAccepted)
	{
		Accepted = bAccepted;
	}

	if (DiffListView.IsValid())
	{
		DiffListView->RequestListRefresh();
	}
}

FReply SBPAIBridgePanel::OnSelectAllClicked()
{
	SetAllAccepted(true);
	return FReply::Handled();
}

FReply SBPAIBridgePanel::OnDeselectAllClicked()
{
	SetAllAccepted(false);
	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------
// Apply / revert
// ---------------------------------------------------------------------------------------

FReply SBPAIBridgePanel::OnApplyChangesClicked()
{
	UBlueprint* Blueprint = SelectedBlueprint.Get();
	if (Blueprint == nullptr)
	{
		UpdateStatus(TEXT("Select a Blueprint first."), /*bIsError*/ true);
		return FReply::Handled();
	}

	TArray<FBPDiffItem> Accepted;
	for (int32 Index = 0; Index < CurrentDiff.Num(); ++Index)
	{
		if (DiffItemAccepted.IsValidIndex(Index) && DiffItemAccepted[Index])
		{
			Accepted.Add(CurrentDiff[Index]);
		}
	}

	if (Accepted.Num() == 0)
	{
		UpdateStatus(TEXT("No changes are ticked."), /*bIsError*/ true);
		return FReply::Handled();
	}

	const FBPImportResult Result = FBPImporter::ApplyDiff(Blueprint, Accepted);
	UpdateStatus(Result.ToString(), !Result.bSuccess);

	// The graph has moved on, so the node ids in the current diff no longer mean anything.
	if (Result.AppliedCount > 0)
	{
		ClearDiff();
		LastExportJson.Reset();
	}

	return FReply::Handled();
}

FReply SBPAIBridgePanel::OnRevertSnapshotClicked()
{
	UBlueprint* Blueprint = SelectedBlueprint.Get();
	if (Blueprint == nullptr)
	{
		UpdateStatus(TEXT("Select a Blueprint first."), /*bIsError*/ true);
		return FReply::Handled();
	}

	FString SnapshotJson;
	if (!FBPSnapshotStore::LoadLatestSnapshot(Blueprint->GetPathName(), SnapshotJson))
	{
		UpdateStatus(TEXT("No snapshot on disk for this Blueprint."), /*bIsError*/ true);
		return FReply::Handled();
	}

	const EAppReturnType::Type Answer = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("RevertConfirm", "Roll this Blueprint back to the most recent snapshot?\n\nAnything added since the snapshot was taken will be removed. This is undoable with Ctrl+Z."));

	if (Answer != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	// Diff the live graph against the snapshot, then apply every difference: the snapshot is
	// the target state, so this is a real revert rather than a replay of the last import.
	FBPExportOptions Options;
	Options.Context = SelectedContext;
	const FString CurrentJson = FBPExporter::ExportBlueprint(Blueprint, Options);

	if (CurrentJson.IsEmpty())
	{
		UpdateStatus(TEXT("Could not read the Blueprint's current state; revert aborted."), /*bIsError*/ true);
		return FReply::Handled();
	}

	FString Error;
	const TArray<FBPDiffItem> RevertDiff = FBPDiffEngine::ComputeDiff(CurrentJson, SnapshotJson, &Error);

	if (!Error.IsEmpty())
	{
		UpdateStatus(FString::Printf(TEXT("Revert aborted: %s"), *Error), /*bIsError*/ true);
		return FReply::Handled();
	}

	if (RevertDiff.Num() == 0)
	{
		UpdateStatus(TEXT("The Blueprint already matches the latest snapshot."));
		return FReply::Handled();
	}

	const FBPImportResult Result = FBPImporter::ApplyDiff(Blueprint, RevertDiff);
	UpdateStatus(FString::Printf(TEXT("Revert: %s"), *Result.ToString()), !Result.bSuccess);

	ClearDiff();
	LastExportJson.Reset();

	return FReply::Handled();
}

// ---------------------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------------------

void SBPAIBridgePanel::UpdateStatus(const FString& Message, bool bIsError)
{
	if (!StatusText.IsValid())
	{
		return;
	}

	StatusText->SetText(FText::FromString(Message));
	StatusText->SetColorAndOpacity(FSlateColor(bIsError ? StatusError : StatusSuccess));

	if (bIsError)
	{
		UE_LOG(LogBlueprintAIBridge, Warning, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogBlueprintAIBridge, Log, TEXT("%s"), *Message);
	}
}

#undef LOCTEXT_NAMESPACE
