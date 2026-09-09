// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchOpeningExtractionSubsystem.h"
#include "Containers/Ticker.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class AActor;
class SVerticalBox;
class UArchOpeningPieceSetComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** One row in the piece list. Rows hold only an index; the component holds the state. */
struct FArchOpeningPieceRow
{
	int32 PieceIndex = INDEX_NONE;
};

/** One row in the group list. */
struct FArchOpeningGroupRow
{
	int32 GroupIndex = INDEX_NONE;
};

/**
 * Leaf extraction panel.
 *
 * A session works against a transient, editor-only actor carrying a UArchOpeningPieceSetComponent.
 * That component is what the viewport draws and hit-tests, which is what makes the workflow direct:
 * pieces are colour-coded by group live, hovering a list row highlights the piece in 3D, hovering
 * the piece in 3D highlights the row, and clicking a piece in the viewport assigns it to the active
 * group. Nothing about the artist's own actors is modified by a session.
 *
 * Classification is into any number of groups - a stationary bucket plus one movable group per leaf
 * - and it persists in an extraction profile asset next to the source mesh, so re-opening the tool
 * on a mesh restores the previous assignments and fixing one piece does not mean starting over.
 */
class SArchOpeningExtractionPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArchOpeningExtractionPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SArchOpeningExtractionPanel() override;

private:
	// ---- Session ---------------------------------------------------------------------------
	FReply OnUseSelectionClicked();
	FReply OnAnalyzeClicked();
	void EndSession();
	UArchOpeningPieceSetComponent* GetPieceSet() const;
	bool HasSession() const;

	// ---- Groups ----------------------------------------------------------------------------
	FReply OnAddGroupClicked();
	FReply OnRemoveGroupClicked(int32 GroupIndex);
	FReply OnSetActiveGroupClicked(int32 GroupIndex);
	FReply OnAssignSelectionClicked(int32 GroupIndex);
	TSharedRef<ITableRow> MakeGroupRow(TSharedPtr<FArchOpeningGroupRow> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// ---- Pieces and batch selection --------------------------------------------------------
	TSharedRef<ITableRow> MakePieceRow(TSharedPtr<FArchOpeningPieceRow> Item, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnSelectAllClicked();
	FReply OnSelectNoneClicked();
	FReply OnInvertSelectionClicked();
	FReply OnSelectSimilarClicked();
	FReply OnFocusSelectionClicked();

	// ---- Profile ---------------------------------------------------------------------------
	FReply OnSaveProfileClicked();
	FReply OnReloadProfileClicked();

	// ---- Output ----------------------------------------------------------------------------
	FReply OnExtractClicked();
	FReply OnSpawnSplitActorsClicked();

	// ---- State / labels --------------------------------------------------------------------
	FText GetSourceLabel() const;
	FText GetStatusText() const;
	FText GetSelectionSummary() const;
	bool CanAnalyze() const;
	bool CanExtract() const;
	bool CanSpawnActors() const;

	void RefreshRows();
	void RefreshGroupRows();

	/** Polls the level viewport's hit proxy so hovering a piece in 3D highlights its list row. */
	bool TickHover(float DeltaSeconds);

	/** Marks the viewport dirty after any change that alters the drawing. */
	void InvalidateViewport() const;

	// ---- Session data ----------------------------------------------------------------------
	TWeakObjectPtr<UStaticMesh> SourceMesh;
	TWeakObjectPtr<UStaticMeshComponent> SourceComponent;

	/** Transient, editor-only actor that carries the piece set for the duration of a session. */
	TWeakObjectPtr<AActor> SessionActor;
	TWeakObjectPtr<UArchOpeningPieceSetComponent> PieceSet;

	/** Full analysis, including the triangle ids extraction needs. */
	FArchOpeningMeshAnalysis Analysis;

	TArray<TSharedPtr<FArchOpeningPieceRow>> PieceRows;
	TArray<TSharedPtr<FArchOpeningGroupRow>> GroupRows;
	TSharedPtr<SListView<TSharedPtr<FArchOpeningPieceRow>>> PieceListView;
	TSharedPtr<SListView<TSharedPtr<FArchOpeningGroupRow>>> GroupListView;

	float WeldTolerance = 0.0f;
	float SimilarSizeTolerance = 1.0f;
	FString OutputPath = TEXT("/Game/ArchitecturalOpenings/Extracted");
	EArchOpeningExtractionCollision CollisionOption = EArchOpeningExtractionCollision::UseComplexAsSimple;
	bool bUpdateExistingAssets = true;
	bool bTrackViewportHover = true;

	FText StatusText;
	TSharedPtr<SVerticalBox> NotesBox;

	FTSTicker::FDelegateHandle HoverTickerHandle;
	int32 LastPolledMouseX = -1;
	int32 LastPolledMouseY = -1;
};
