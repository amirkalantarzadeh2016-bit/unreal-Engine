// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BPDiffEngine.h"
#include "BPExporter.h"
#include "BPGraphLayoutEngine.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SMultiLineEditableTextBox;
class STableViewBase;
class STextBlock;
class UBlueprint;
class UEdGraph;
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

	/**
	 * Points the panel at a Blueprint, as though it had been chosen in the picker.
	 *
	 * Called when the panel is opened from a Blueprint editor's own toolbar, so the developer
	 * does not have to find in a picker the asset they already have open.
	 */
	void SetBlueprint(UBlueprint* Blueprint);

private:
	// ---- State -------------------------------------------------------------------------
	TWeakObjectPtr<UBlueprint> SelectedBlueprint;
	EExportContext SelectedContext = EExportContext::General;
	FString LastExportJson;

	/** Where the file browser opens next time; seeded from the snapshot directory. */
	FString LastResponseDirectory;
	FString LastSnapshotPath;

	/** Where the last "Save JSON File..." went, and the folder to reopen the save dialog in. */
	FString LastSavedExportPath;
	FString LastExportDirectory;
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
	TSharedPtr<SMultiLineEditableTextBox> ExportPreviewBox;
	TSharedPtr<SMultiLineEditableTextBox> AIResponseBox;
	TSharedPtr<SListView<TSharedPtr<FBPDiffItem>>> DiffListView;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> GraphCombo;

	// ---- Blueprint picker --------------------------------------------------------------
	FString GetBlueprintPath() const;
	void OnBlueprintChanged(const FAssetData& AssetData);
	/**
	 * Rebuilds the export scope combo from the Blueprint's current graphs.
	 * @param bPreserveSelection  keep the selected graph if a graph of that name still exists;
	 *                            otherwise the scope resets to "All Graphs".
	 */
	void RefreshGraphOptions(bool bPreserveSelection = false);

	// ---- Combo boxes -------------------------------------------------------------------
	TSharedRef<SWidget> MakeComboItemWidget(TSharedPtr<FString> Item) const;
	void OnContextSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnGraphSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetContextComboText() const;
	FText GetGraphComboText() const;

	// ---- Toolbar -----------------------------------------------------------------------
	TSharedRef<SWidget> BuildToolbar();
	void OnFormatGraphExecute();
	bool CanFormatGraph() const;

	/** The graphs the export scope combo currently selects: all of them, or just the one. */
	TArray<UEdGraph*> GetGraphsInScope() const;

	/**
	 * Formats GetGraphsInScope(). Leaves the status line alone so callers can decide what to
	 * say -- the apply path has an import result to report alongside this one.
	 */
	FBPFormatResult FormatGraphsInScope();

	// ---- Button handlers ---------------------------------------------------------------
	FReply OnExportClicked();
	FReply OnCopyToClipboardClicked();
	FReply OnSaveExportToFileClicked();
	FReply OnShowExportInExplorerClicked();
	FReply OnLoadResponseFromFileClicked();
	FReply OnAnalyzeDiffClicked();
	FReply OnApplyChangesClicked();
	FReply OnRevertSnapshotClicked();
	FReply OnSelectAllClicked();
	FReply OnDeselectAllClicked();

	bool HasBlueprint() const;
	bool HasExport() const;
	bool HasDiff() const;

	/** True once a file has been written this session, which is what "Show in Explorer" needs. */
	bool HasExportFile() const;

	/** The "Export file: ..." line under the export buttons -- selectable, so it can be copied. */
	FText GetExportFileText() const;

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

	/**
	 * Index of the graph the scope combo selects, within FBPExporter::CollectGraphs order, or
	 * INDEX_NONE when the scope is "All Graphs" or the named graph is gone.
	 */
	int32 FindSelectedGraphIndex() const;

	/** Builds the export options implied by the current combo selections. */
	FBPExportOptions MakeExportOptions() const;

	/** Export JSON + prompt prefix combined, ready for the clipboard. */
	FString BuildFullExportPayload();

	/** Puts the current payload in the preview box, or empties it when there is no export. */
	void RefreshExportPreview();

	/** Keeps the preview honest when the task description -- part of the prompt -- changes. */
	void OnTaskTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	/** Default filename for the save dialog: <BlueprintName>_<UTC stamp>.json. */
	FString SuggestExportFilename() const;
};
