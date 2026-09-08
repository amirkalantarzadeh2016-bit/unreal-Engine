// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchOpeningExtractionSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UStaticMesh;
class UStaticMeshComponent;
class SVerticalBox;

/** One row in the piece list. */
struct FArchOpeningPieceRow
{
	int32 PieceIndex = INDEX_NONE;
	bool bSelected = false;
	FText Label;
};

/**
 * Leaf extraction panel: analyse a one-mesh source, pick the pieces that make up the movable leaf,
 * preview the split, and write out two new assets.
 *
 * The panel is careful about two things in particular. It never touches the source asset, and it
 * never claims a separation it did not achieve: a source whose frame and leaf are welded into one
 * connected piece produces a clear unsupported-case message and no output.
 */
class SArchOpeningExtractionPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArchOpeningExtractionPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SArchOpeningExtractionPanel() override;

private:
	FReply OnUseSelectionClicked();
	FReply OnAnalyzeClicked();
	FReply OnPreviewClicked();
	FReply OnClearPreviewClicked();
	FReply OnExtractClicked();

	TSharedRef<ITableRow> MakePieceRow(TSharedPtr<FArchOpeningPieceRow> Item, const TSharedRef<STableViewBase>& OwnerTable);

	FText GetSourceLabel() const;
	FText GetStatusText() const;
	bool CanAnalyze() const;
	bool CanExtract() const;

	void RefreshRows();
	void DrawPreview();

	TWeakObjectPtr<UStaticMesh> SourceMesh;
	TWeakObjectPtr<UStaticMeshComponent> SourceComponent;

	FArchOpeningMeshAnalysis Analysis;
	TArray<TSharedPtr<FArchOpeningPieceRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FArchOpeningPieceRow>>> ListView;

	float WeldTolerance = 0.0f;
	FString OutputPath = TEXT("/Game/ArchitecturalOpenings/Extracted");
	EArchOpeningExtractionCollision CollisionOption = EArchOpeningExtractionCollision::UseComplexAsSimple;

	FText StatusText;
	TSharedPtr<SVerticalBox> NotesBox;
};
