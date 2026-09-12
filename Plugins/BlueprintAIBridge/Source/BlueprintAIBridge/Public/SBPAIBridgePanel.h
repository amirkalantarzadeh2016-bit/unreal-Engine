// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BPDiffEngine.h"
#include "BPExporter.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SMultiLineEditableTextBox;
class STableViewBase;
class STextBlock;
class UBlueprint;
struct FAssetData;

/**
 * Dockable editor panel driving the whole workflow: pick a Blueprint, pick a context, export,
 * paste the AI reply, review the diff item by item, apply the accepted subset, revert if needed.
 */
class BLUEPRINTAIBRIDGE_API SBPAIBridgePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBPAIBridgePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// ---- State -------------------------------------------------------------------------
	TWeakObjectPtr<UBlueprint> SelectedBlueprint;
	EExportContext SelectedContext = EExportContext::General;
	FString LastExportJson;
	FString LastSnapshotPath;
	TArray<FBPDiffItem> CurrentDiff;
	TArray<bool> DiffItemAccepted; // parallel array to CurrentDiff

	/** List view source; entries are 1:1 and in the same order as CurrentDiff. */
	TArray<TSharedPtr<FBPDiffItem>> DiffListSource;

	/** "All Graphs" followed by one entry per graph, for the export scope combo. */
	TArray<TSharedPtr<FString>> GraphOptions;
	TSharedPtr<FString> SelectedGraphOption;

	TArray<TSharedPtr<FString>> ContextOptions;
	TSharedPtr<FString> SelectedContextOption;

	// ---- Widgets -----------------------------------------------------------------------
	TSharedPtr<SMultiLineEditableTextBox> TaskDescBox;
	TSharedPtr<SMultiLineEditableTextBox> AIResponseBox;
	TSharedPtr<SListView<TSharedPtr<FBPDiffItem>>> DiffListView;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> GraphCombo;

	// ---- Blueprint picker --------------------------------------------------------------
	FString GetBlueprintPath() const;
	void OnBlueprintChanged(const FAssetData& AssetData);
	void RefreshGraphOptions();

	// ---- Combo boxes -------------------------------------------------------------------
	TSharedRef<SWidget> MakeComboItemWidget(TSharedPtr<FString> Item) const;
	void OnContextSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnGraphSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetContextComboText() const;
	FText GetGraphComboText() const;

	// ---- Button handlers ---------------------------------------------------------------
	FReply OnExportClicked();
	FReply OnCopyToClipboardClicked();
	FReply OnAnalyzeDiffClicked();
	FReply OnApplyChangesClicked();
	FReply OnRevertSnapshotClicked();
	FReply OnSelectAllClicked();
	FReply OnDeselectAllClicked();

	bool HasBlueprint() const;
	bool HasExport() const;
	bool HasDiff() const;

	// ---- List view ---------------------------------------------------------------------
	TSharedRef<ITableRow> GenerateDiffRow(
		TSharedPtr<FBPDiffItem> Item,
		const TSharedRef<STableViewBase>& OwnerTable);

	ECheckBoxState IsDiffItemAccepted(TSharedPtr<FBPDiffItem> Item) const;
	void OnDiffItemAcceptedChanged(ECheckBoxState NewState, TSharedPtr<FBPDiffItem> Item);
	void SetAllAccepted(bool bAccepted);

	// ---- Helpers -----------------------------------------------------------------------
	void UpdateStatus(const FString& Message, bool bIsError = false);
	void ClearDiff();

	/** Builds the export options implied by the current combo selections. */
	FBPExportOptions MakeExportOptions() const;

	/** Export JSON + prompt prefix combined, ready for the clipboard. */
	FString BuildFullExportPayload();
};
