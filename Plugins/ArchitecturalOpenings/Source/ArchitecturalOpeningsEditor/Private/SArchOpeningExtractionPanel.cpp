// Copyright Epic Games, Inc. All Rights Reserved.

#include "SArchOpeningExtractionPanel.h"

#include "ArchOpeningActor.h"
#include "ArchOpeningComponent.h"
#include "ArchOpeningExtractionProfile.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningPieceSetComponent.h"
#include "ArchOpeningPieceSetVisualizer.h"

#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "UnrealClient.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ArchOpeningExtractionPanel"

namespace ArchOpeningExtractionPanelLocal
{
	/** How often the viewport hover poll runs. Fast enough to feel live, slow enough to be cheap. */
	static constexpr float HoverPollInterval = 0.04f;

	TSharedRef<SWidget> SectionHeader(const FText& Title)
	{
		return SNew(SBorder)
			.Padding(FMargin(6.0f, 4.0f))
			[
				SNew(STextBlock).Text(Title)
			];
	}
}

void SArchOpeningExtractionPanel::Construct(const FArguments& /*InArgs*/)
{
	using namespace ArchOpeningExtractionPanelLocal;

	HoverTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		TEXT("ArchOpeningExtractionHover"), HoverPollInterval,
		[this](float DeltaSeconds) { return TickHover(DeltaSeconds); });

	ChildSlot
	[
		SNew(SScrollBox)

		+ SScrollBox::Slot().Padding(6.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("Intro",
					"Splits one static mesh into a stationary frame plus one movable leaf per group. "
					"Pieces are found by triangle adjacency: two triangles belong to the same piece when they share a vertex. "
					"Hard edges and UV seams do not split a piece; duplicated vertices from an exporter can, which is what the weld tolerance is for. "
					"If the frame and the leaf are welded into a single piece, they cannot be separated here."))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[ SNew(SSeparator) ]

			// ---- 1. Source -------------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SectionHeader(LOCTEXT("Step1", "1.  Source mesh"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock).AutoWrapText(true).Text(this, &SArchOpeningExtractionPanel::GetSourceLabel)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("UseSelection", "Use Selected Static Mesh"))
					.OnClicked(this, &SArchOpeningExtractionPanel::OnUseSelectionClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("WeldTolerance", "Weld tolerance (cm)"))
				]

				+ SHorizontalBox::Slot().AutoWidth().MinWidth(90.0f)
				[
					SNew(SSpinBox<float>)
					.MinValue(0.0f)
					.MaxValue(10.0f)
					.Delta(0.01f)
					.ToolTipText(LOCTEXT("WeldToleranceTip",
						"0 uses pure topology. Raise it only if one physical part comes back split across several pieces because the exporter duplicated vertices at seams. "
						"Too large a value welds a leaf to the frame it sits flush against and they can no longer be separated."))
					.Value_Lambda([this]() { return WeldTolerance; })
					.OnValueChanged_Lambda([this](float NewValue) { WeldTolerance = NewValue; })
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Analyze", "Analyse Pieces"))
					.IsEnabled(this, &SArchOpeningExtractionPanel::CanAnalyze)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnAnalyzeClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[ SNew(SSeparator) ]

			// ---- 2. Groups -------------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SectionHeader(LOCTEXT("Step2", "2.  Groups"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("GroupsHelp",
					"One stationary bucket plus one movable group per leaf. A double door needs two movable groups; a folding door needs one per panel. "
					"Every group with pieces in it becomes exactly one asset."))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2).MaxHeight(180.0f)
			[
				SAssignNew(GroupListView, SListView<TSharedPtr<FArchOpeningGroupRow>>)
				.ListItemsSource(&GroupRows)
				.OnGenerateRow(this, &SArchOpeningExtractionPanel::MakeGroupRow)
				.SelectionMode(ESelectionMode::None)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddGroup", "Add Group..."))
				.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
				.OnClicked(this, &SArchOpeningExtractionPanel::OnAddGroupClicked)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[ SNew(SSeparator) ]

			// ---- 3. Assign -------------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SectionHeader(LOCTEXT("Step3", "3.  Assign pieces"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("ViewportHelp",
					"In the viewport: click a piece to put it in the active group. Ctrl-click to tick it without assigning. Shift-click to add it to the ticked set and assign the whole set. "
					"Hovering a piece highlights its row, and hovering a row highlights the piece. Keep the temporary 'Opening Piece Editing' actor selected, or the boxes stop drawing."))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock).Text(this, &SArchOpeningExtractionPanel::GetSelectionSummary)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton).Text(LOCTEXT("SelectAll", "All"))
					.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnSelectAllClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton).Text(LOCTEXT("SelectNone", "None"))
					.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnSelectNoneClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton).Text(LOCTEXT("InvertSelection", "Invert"))
					.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnInvertSelectionClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SelectSimilar", "Select Similar"))
					.ToolTipText(LOCTEXT("SelectSimilarTip",
						"Ticks every piece with the same triangle count and the same bounding size as the ticked ones, ignoring orientation. "
						"This is the way to catch twelve identical mouldings in one go."))
					.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnSelectSimilarClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 4, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("SimilarTolerance", "+/- cm"))
				]

				+ SHorizontalBox::Slot().AutoWidth().MinWidth(70.0f)
				[
					SNew(SSpinBox<float>)
					.MinValue(0.0f).MaxValue(50.0f).Delta(0.1f)
					.Value_Lambda([this]() { return SimilarSizeTolerance; })
					.OnValueChanged_Lambda([this](float NewValue) { SimilarSizeTolerance = NewValue; })
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("FocusSelection", "Focus In Viewport"))
					.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnFocusSelectionClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2).MaxHeight(340.0f)
			[
				SAssignNew(PieceListView, SListView<TSharedPtr<FArchOpeningPieceRow>>)
				.ListItemsSource(&PieceRows)
				.OnGenerateRow(this, &SArchOpeningExtractionPanel::MakePieceRow)
				.SelectionMode(ESelectionMode::None)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					return (Set != nullptr && Set->bDrawPieceWireframe) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
					{
						Set->bDrawPieceWireframe = (NewState == ECheckBoxState::Checked);
						InvalidateViewport();
					}
				})
				[
					SNew(STextBlock).Text(LOCTEXT("ShowWireframe", "Also draw the triangles inside each box (slower)"))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bTrackViewportHover ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bTrackViewportHover = (NewState == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock).Text(LOCTEXT("TrackHover", "Highlight the piece under the mouse in the viewport"))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[ SNew(SSeparator) ]

			// ---- 4. Output -------------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SectionHeader(LOCTEXT("Step4", "4.  Output"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("OutputFolder", "Output folder"))
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this]() { return FText::FromString(OutputPath); })
					.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
					{
						OutputPath = NewText.ToString();
					})
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					return CollisionOption == EArchOpeningExtractionCollision::UseComplexAsSimple
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					CollisionOption = (NewState == ECheckBoxState::Checked)
						? EArchOpeningExtractionCollision::UseComplexAsSimple
						: EArchOpeningExtractionCollision::CopySourceFlag;
				})
				[
					SNew(STextBlock).Text(LOCTEXT("ComplexAsSimple", "Use complex as simple collision (needed for clicking)"))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bUpdateExistingAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					bUpdateExistingAssets = (NewState == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock)
					.ToolTipText(LOCTEXT("UpdateExistingTip",
						"Rewrites the assets a previous extraction produced instead of creating a second set. "
						"Actors already placed in the level keep pointing at the same meshes and simply update, so fixing one misassigned piece costs one click."))
					.Text(LOCTEXT("UpdateExisting", "Update the assets this profile created before, instead of making new ones"))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Extract", "Create / Update Group Assets"))
					.IsEnabled(this, &SArchOpeningExtractionPanel::CanExtract)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnExtractClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SpawnActors", "Spawn Split Actors In Level"))
					.ToolTipText(LOCTEXT("SpawnActorsTip",
						"Places one Static Mesh Actor per group at the source's exact transform and hides the source actor, ready to assign to an opening."))
					.IsEnabled(this, &SArchOpeningExtractionPanel::CanSpawnActors)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnSpawnSplitActorsClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("BuildOpenings", "Build Openings"))
					.ToolTipText(LOCTEXT("BuildOpeningsTip",
						"Creates one Architectural Opening per movable group, assigns the fixed parts and that group's leaf to it, captures the closed pose and places the hinge. "
						"A double door becomes two openings over one shared frame."))
					.IsEnabled(this, &SArchOpeningExtractionPanel::CanCreateOpenings)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnCreateOpeningsClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveProfile", "Save Assignments"))
					.ToolTipText(LOCTEXT("SaveProfileTip",
						"Writes the group assignments to an extraction profile asset next to the source mesh, so re-opening this tool restores them."))
					.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnSaveProfileClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ReloadProfile", "Reload Assignments"))
					.IsEnabled(this, &SArchOpeningExtractionPanel::HasSession)
					.OnClicked(this, &SArchOpeningExtractionPanel::OnReloadProfileClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[ SNew(SSeparator) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock).AutoWrapText(true).Text(this, &SArchOpeningExtractionPanel::GetStatusText)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SAssignNew(NotesBox, SVerticalBox)
			]
		]
	];
}

SArchOpeningExtractionPanel::~SArchOpeningExtractionPanel()
{
	if (HoverTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(HoverTickerHandle);
		HoverTickerHandle.Reset();
	}

	// The session actor is transient, but leaving it behind when the tab closes would still be
	// clutter the artist has to clean up by hand.
	EndSession();
}

// -------------------------------------------------------------------------------------------
// Session
// -------------------------------------------------------------------------------------------

UArchOpeningPieceSetComponent* SArchOpeningExtractionPanel::GetPieceSet() const
{
	return PieceSet.Get();
}

bool SArchOpeningExtractionPanel::HasSession() const
{
	const UArchOpeningPieceSetComponent* Set = PieceSet.Get();
	return Set != nullptr && !Set->Pieces.IsEmpty();
}

void SArchOpeningExtractionPanel::EndSession()
{
	// Guarded for editor shutdown, where the world may already be tearing down when the tab closes.
	if (AActor* Actor = SessionActor.Get())
	{
		UWorld* World = Actor->GetWorld();
		if (::IsValid(World) && !World->bIsTearingDown)
		{
			World->DestroyActor(Actor);
		}
	}

	SessionActor = nullptr;
	PieceSet = nullptr;
	Analysis = FArchOpeningMeshAnalysis();
	SpawnedActorsByGroup.Reset();

	PieceRows.Reset();
	GroupRows.Reset();

	if (PieceListView.IsValid())
	{
		PieceListView->RequestListRefresh();
	}
	if (GroupListView.IsValid())
	{
		GroupListView->RequestListRefresh();
	}
}

void SArchOpeningExtractionPanel::InvalidateViewport() const
{
	if (GEditor != nullptr)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void SArchOpeningExtractionPanel::HandleRowMouseEnter(const FGeometry& /*Geometry*/, const FPointerEvent& /*Event*/, int32 PieceIndex)
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		if (Set->HoveredPieceIndex != PieceIndex)
		{
			Set->HoveredPieceIndex = PieceIndex;
			InvalidateViewport();
		}
	}
}

void SArchOpeningExtractionPanel::HandleRowMouseLeave(const FPointerEvent& /*Event*/, int32 PieceIndex)
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		// Only clear our own hover: the pointer may already have entered the next row.
		if (Set->HoveredPieceIndex == PieceIndex)
		{
			Set->HoveredPieceIndex = INDEX_NONE;
			InvalidateViewport();
		}
	}
}

FReply SArchOpeningExtractionPanel::OnUseSelectionClicked()
{
	EndSession();

	SourceMesh = nullptr;
	SourceComponent = nullptr;

	if (GEditor == nullptr)
	{
		return FReply::Handled();
	}

	// Component selection first, so a mesh component inside a larger actor can be the source.
	if (USelection* ComponentSelection = GEditor->GetSelectedComponents())
	{
		for (FSelectionIterator It(*ComponentSelection); It; ++It)
		{
			if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(*It))
			{
				SourceComponent = MeshComponent;
				SourceMesh = MeshComponent->GetStaticMesh();
				break;
			}
		}
	}

	if (!SourceMesh.IsValid())
	{
		if (USelection* ActorSelection = GEditor->GetSelectedActors())
		{
			for (FSelectionIterator It(*ActorSelection); It; ++It)
			{
				AActor* Actor = Cast<AActor>(*It);
				if (Actor == nullptr)
				{
					continue;
				}

				TArray<UStaticMeshComponent*> MeshComponents;
				Actor->GetComponents(MeshComponents);

				for (UStaticMeshComponent* MeshComponent : MeshComponents)
				{
					if (MeshComponent->GetStaticMesh() != nullptr)
					{
						SourceComponent = MeshComponent;
						SourceMesh = MeshComponent->GetStaticMesh();
						break;
					}
				}

				if (SourceMesh.IsValid())
				{
					break;
				}
			}
		}
	}

	if (!SourceMesh.IsValid())
	{
		StatusText = LOCTEXT("SourceNotFound", "No static mesh found in the selection.");
		return FReply::Handled();
	}

	// Pick up the tolerance a previous session settled on, so re-analysis reproduces the same
	// pieces the saved assignments were keyed against.
	if (const UArchOpeningExtractionSubsystem* Subsystem = GEditor->GetEditorSubsystem<UArchOpeningExtractionSubsystem>())
	{
		if (const UArchOpeningExtractionProfile* Profile = Subsystem->FindProfileFor(SourceMesh.Get()))
		{
			WeldTolerance = Profile->WeldTolerance;
			StatusText = LOCTEXT("SourceSetWithProfile",
				"Source set, and an existing profile was found. Press Analyse Pieces to load its assignments.");
			return FReply::Handled();
		}
	}

	StatusText = LOCTEXT("SourceSet", "Source set. Press Analyse Pieces.");
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnAnalyzeClicked()
{
	EndSession();

	UArchOpeningExtractionSubsystem* Subsystem =
		GEditor ? GEditor->GetEditorSubsystem<UArchOpeningExtractionSubsystem>() : nullptr;

	if (Subsystem == nullptr)
	{
		StatusText = LOCTEXT("NoSubsystem", "The extraction subsystem is unavailable.");
		return FReply::Handled();
	}

	FText Error;
	if (!Subsystem->AnalyzeMesh(SourceMesh.Get(), WeldTolerance, Analysis, Error))
	{
		StatusText = Error;
		return FReply::Handled();
	}

	if (Analysis.IsSinglePiece())
	{
		// The unsupported case, reported plainly rather than half-attempted.
		StatusText = Error;
		Analysis = FArchOpeningMeshAnalysis();
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UStaticMeshComponent* Component = SourceComponent.Get();

	if (World == nullptr || Component == nullptr)
	{
		StatusText = LOCTEXT("NeedPlacedSource",
			"The source mesh has to be placed in the level: select the Static Mesh Actor, press 'Use Selected Static Mesh', then Analyse.");
		return FReply::Handled();
	}

	// A transient, editor-only holder actor. The artist's own actors are never modified by a
	// session, and nothing about it can be saved into the level.
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;

	AActor* Holder = World->SpawnActor<AActor>(
		Component->GetComponentLocation(), Component->GetComponentRotation(), SpawnParams);

	if (Holder == nullptr)
	{
		StatusText = LOCTEXT("SpawnFailed", "Could not create the temporary editing actor.");
		return FReply::Handled();
	}

	Holder->SetActorLabel(TEXT("Opening Piece Editing (temporary)"));

	UArchOpeningPieceSetComponent* NewPieceSet =
		NewObject<UArchOpeningPieceSetComponent>(Holder, TEXT("PieceSet"), RF_Transient);

	Holder->SetRootComponent(NewPieceSet);
	NewPieceSet->RegisterComponent();
	Holder->AddInstanceComponent(NewPieceSet);

	// A plain AActor has no root at spawn time, so the spawn transform was dropped. Put the holder
	// on the source now that it has one, purely so its gizmo and outliner entry sit on the door.
	NewPieceSet->SetWorldTransform(Component->GetComponentTransform());

	SessionActor = Holder;
	PieceSet = NewPieceSet;

	Subsystem->PopulatePieceSet(NewPieceSet, Analysis, SourceMesh.Get(), Component);

	// Restore a previous classification if there is one.
	int32 Matched = 0;
	int32 Unmatched = 0;
	const UArchOpeningExtractionProfile* Profile = Subsystem->FindProfileFor(SourceMesh.Get());
	const bool bAppliedProfile = Profile != nullptr && Subsystem->ApplyProfileTo(NewPieceSet, Profile, Matched, Unmatched);

	RefreshGroupRows();
	RefreshRows();

	// Selecting the holder is what makes the visualizer draw: component visualizers only run for
	// components of a selected actor.
	GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
	GEditor->SelectActor(Holder, /*bInSelected*/ true, /*bNotify*/ true);
	InvalidateViewport();

	if (bAppliedProfile)
	{
		StatusText = FText::Format(
			LOCTEXT("AnalyzedWithProfileFmt",
				"{0} pieces across {1} triangles. Restored {2} saved assignment(s); {3} piece(s) were not in the profile and start stationary."),
			FText::AsNumber(Analysis.Pieces.Num()), FText::AsNumber(Analysis.TotalTriangles),
			FText::AsNumber(Matched), FText::AsNumber(Unmatched));
	}
	else
	{
		StatusText = FText::Format(
			LOCTEXT("AnalyzedFmt",
				"{0} pieces across {1} triangles. Pick the active group, then click pieces in the viewport to assign them."),
			FText::AsNumber(Analysis.Pieces.Num()), FText::AsNumber(Analysis.TotalTriangles));
	}

	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Viewport hover
// -------------------------------------------------------------------------------------------

bool SArchOpeningExtractionPanel::TickHover(float /*DeltaSeconds*/)
{
	UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Set == nullptr)
	{
		return true;
	}

	if (!bTrackViewportHover)
	{
		if (Set->HoveredPieceIndex != INDEX_NONE)
		{
			Set->HoveredPieceIndex = INDEX_NONE;
			InvalidateViewport();
		}
		return true;
	}

	FViewport* Viewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
	if (Viewport == nullptr)
	{
		return true;
	}

	const int32 MouseX = Viewport->GetMouseX();
	const int32 MouseY = Viewport->GetMouseY();

	// Only ask for a hit proxy when the cursor actually moved. GetHitProxy can force a hit-proxy
	// render, so polling a stationary cursor every tick would be paying for nothing.
	if (MouseX == LastPolledMouseX && MouseY == LastPolledMouseY)
	{
		return true;
	}

	LastPolledMouseX = MouseX;
	LastPolledMouseY = MouseY;

	if (MouseX < 0 || MouseY < 0)
	{
		return true;
	}

	int32 NewHovered = INDEX_NONE;

	if (HHitProxy* Proxy = Viewport->GetHitProxy(MouseX, MouseY))
	{
		if (Proxy->IsA(HArchOpeningPieceProxy::StaticGetType()))
		{
			NewHovered = static_cast<HArchOpeningPieceProxy*>(Proxy)->PieceIndex;
		}
	}

	if (NewHovered != Set->HoveredPieceIndex)
	{
		Set->HoveredPieceIndex = NewHovered;
		InvalidateViewport();

		// Bring the hovered piece's row into view so the two halves of the tool stay in step.
		if (PieceListView.IsValid() && PieceRows.IsValidIndex(NewHovered))
		{
			PieceListView->RequestScrollIntoView(PieceRows[NewHovered]);
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------------
// Groups
// -------------------------------------------------------------------------------------------

void SArchOpeningExtractionPanel::RefreshGroupRows()
{
	GroupRows.Reset();

	if (const UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		for (int32 GroupIndex = 0; GroupIndex < Set->Groups.Num(); ++GroupIndex)
		{
			TSharedPtr<FArchOpeningGroupRow> Row = MakeShared<FArchOpeningGroupRow>();
			Row->GroupIndex = GroupIndex;
			GroupRows.Add(Row);
		}
	}

	if (GroupListView.IsValid())
	{
		GroupListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SArchOpeningExtractionPanel::MakeGroupRow(TSharedPtr<FArchOpeningGroupRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 GroupIndex = Item.IsValid() ? Item->GroupIndex : INDEX_NONE;

	return SNew(STableRow<TSharedPtr<FArchOpeningGroupRow>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 2)
			[
				SNew(SColorBlock)
				.Color_Lambda([this, GroupIndex]()
				{
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					return Set ? Set->GetGroupColor(GroupIndex) : FLinearColor::White;
				})
				.Size(FVector2D(18.0f, 18.0f))
			]

			// Editable, because the group name ends up on the generated asset's name.
			+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center).Padding(6, 2)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this, GroupIndex]()
				{
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					return (Set != nullptr && Set->Groups.IsValidIndex(GroupIndex))
						? FText::FromName(Set->Groups[GroupIndex].GroupName)
						: FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([this, GroupIndex](const FText& NewText, ETextCommit::Type)
				{
					if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
					{
						Set->RenameGroup(GroupIndex, FName(*NewText.ToString()));
					}
				})
			]

			+ SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center).Padding(6, 2)
			[
				SNew(STextBlock)
				.Text_Lambda([this, GroupIndex]()
				{
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					if (Set == nullptr || !Set->Groups.IsValidIndex(GroupIndex))
					{
						return FText::GetEmpty();
					}

					const FArchOpeningExtractionGroup& Group = Set->Groups[GroupIndex];
					const bool bActive = (Set->ActiveGroupIndex == GroupIndex);

					return FText::Format(
						LOCTEXT("GroupRowFmt", "{0}{1} piece(s)   [{2}]"),
						bActive ? LOCTEXT("ActiveMarker", "ACTIVE   -   ") : FText::GetEmpty(),
						FText::AsNumber(Set->CountPiecesInGroup(GroupIndex)),
						Group.Role == EArchOpeningGroupRole::Stationary
							? LOCTEXT("RoleFixed", "fixed")
							: LOCTEXT("RoleMovable", "movable"));
				})
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("SetActive", "Make Active"))
				.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningExtractionPanel::OnSetActiveGroupClicked, GroupIndex))
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("AssignTicked", "Assign Ticked"))
				.ToolTipText(LOCTEXT("AssignTickedTip", "Moves every ticked piece into this group."))
				.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningExtractionPanel::OnAssignSelectionClicked, GroupIndex))
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveGroup", "X"))
				.ToolTipText(LOCTEXT("RemoveGroupTip", "Removes this group. Its pieces go back to the stationary bucket."))
				.IsEnabled(GroupIndex > 0)
				.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningExtractionPanel::OnRemoveGroupClicked, GroupIndex))
			]
		];
}

FReply SArchOpeningExtractionPanel::OnAddGroupClicked()
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		Set->ActiveGroupIndex = Set->AddMovableGroup();
		RefreshGroupRows();
		InvalidateViewport();
	}
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnRemoveGroupClicked(int32 GroupIndex)
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		Set->RemoveGroup(GroupIndex);
		RefreshGroupRows();
		RefreshRows();
		InvalidateViewport();
	}
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnSetActiveGroupClicked(int32 GroupIndex)
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		if (Set->IsValidGroup(GroupIndex))
		{
			Set->ActiveGroupIndex = GroupIndex;
			RefreshGroupRows();
		}
	}
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnAssignSelectionClicked(int32 GroupIndex)
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		Set->AssignSelectionToGroup(GroupIndex);
		RefreshGroupRows();
		InvalidateViewport();
	}
	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Pieces and batch selection
// -------------------------------------------------------------------------------------------

void SArchOpeningExtractionPanel::RefreshRows()
{
	PieceRows.Reset();

	if (const UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		for (int32 PieceIndex = 0; PieceIndex < Set->Pieces.Num(); ++PieceIndex)
		{
			TSharedPtr<FArchOpeningPieceRow> Row = MakeShared<FArchOpeningPieceRow>();
			Row->PieceIndex = PieceIndex;
			PieceRows.Add(Row);
		}
	}

	if (PieceListView.IsValid())
	{
		PieceListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SArchOpeningExtractionPanel::MakePieceRow(TSharedPtr<FArchOpeningPieceRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 PieceIndex = Item.IsValid() ? Item->PieceIndex : INDEX_NONE;

	TSharedRef<STableRow<TSharedPtr<FArchOpeningPieceRow>>> Row =
		SNew(STableRow<TSharedPtr<FArchOpeningPieceRow>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			// Ticked set, which the batch utilities operate on.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 1)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, PieceIndex]()
				{
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					return (Set != nullptr && Set->SelectedPieces.Contains(PieceIndex))
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, PieceIndex](ECheckBoxState)
				{
					if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
					{
						Set->ToggleSelection(PieceIndex);
						InvalidateViewport();
					}
				})
			]

			// The group's colour, so the list reads the same way the viewport does.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(SColorBlock)
				.Color_Lambda([this, PieceIndex]()
				{
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					if (Set == nullptr || !Set->Pieces.IsValidIndex(PieceIndex))
					{
						return FLinearColor::White;
					}
					return Set->GetGroupColor(Set->Pieces[PieceIndex].GroupIndex);
				})
				.Size(FVector2D(14.0f, 14.0f))
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock)
				.Text_Lambda([this, PieceIndex]()
				{
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					if (Set == nullptr || !Set->Pieces.IsValidIndex(PieceIndex))
					{
						return FText::GetEmpty();
					}
					return UArchOpeningExtractionSubsystem::DescribePiece(Set->Pieces[PieceIndex], PieceIndex);
				})
				.ColorAndOpacity_Lambda([this, PieceIndex]()
				{
					// The hovered row is brightened to match the highlighted box in the viewport.
					const UArchOpeningPieceSetComponent* Set = GetPieceSet();
					const bool bHovered = (Set != nullptr && Set->HoveredPieceIndex == PieceIndex);
					return FSlateColor(bHovered ? FLinearColor::White : FLinearColor(0.75f, 0.75f, 0.75f));
				})
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 1)
			[
				SNew(SButton)
				.Text(LOCTEXT("AssignToActive", "-> Active"))
				.ToolTipText(LOCTEXT("AssignToActiveTip", "Puts this piece in the active group."))
				.OnClicked_Lambda([this, PieceIndex]()
				{
					if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
					{
						Set->AssignPieceToGroup(PieceIndex, Set->ActiveGroupIndex);
						InvalidateViewport();
					}
					return FReply::Handled();
				})
			]
		];

	// Hover on the whole row rather than only the button: with thirty pieces, having to find a
	// small button before the viewport highlights anything defeats the point.
	Row->SetOnMouseEnter(FNoReplyPointerEventHandler::CreateSP(
		this, &SArchOpeningExtractionPanel::HandleRowMouseEnter, PieceIndex));
	Row->SetOnMouseLeave(FSimpleNoReplyPointerEventHandler::CreateSP(
		this, &SArchOpeningExtractionPanel::HandleRowMouseLeave, PieceIndex));

	return Row;
}

FReply SArchOpeningExtractionPanel::OnSelectAllClicked()
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		Set->SelectAll();
		InvalidateViewport();
	}
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnSelectNoneClicked()
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		Set->ClearSelection();
		InvalidateViewport();
	}
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnInvertSelectionClicked()
{
	if (UArchOpeningPieceSetComponent* Set = GetPieceSet())
	{
		Set->InvertSelection();
		InvalidateViewport();
	}
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnSelectSimilarClicked()
{
	UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Set == nullptr)
	{
		return FReply::Handled();
	}

	if (Set->SelectedPieces.IsEmpty())
	{
		StatusText = LOCTEXT("SimilarNeedsReference",
			"Tick at least one piece first, or click one in the viewport. Select Similar matches everything that looks like the ticked pieces.");
		return FReply::Handled();
	}

	// Expanding from every ticked piece, not just one, means a mixed reference set still finds all
	// of the families it contains.
	const TArray<int32> References = Set->SelectedPieces.Array();

	int32 TotalMatched = 0;
	for (int32 Reference : References)
	{
		TotalMatched += Set->SelectSimilarTo(Reference, SimilarSizeTolerance);
	}

	StatusText = FText::Format(
		LOCTEXT("SimilarMatchedFmt", "{0} piece(s) ticked after matching against {1} reference piece(s)."),
		FText::AsNumber(Set->SelectedPieces.Num()), FText::AsNumber(References.Num()));

	InvalidateViewport();
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnFocusSelectionClicked()
{
	const UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Set == nullptr || Set->SelectedPieces.IsEmpty() || GEditor == nullptr)
	{
		return FReply::Handled();
	}

	const FTransform ToWorld = Set->GetSourceWorldTransform();

	FBox Focus(ForceInit);
	for (int32 PieceIndex : Set->SelectedPieces)
	{
		if (Set->Pieces.IsValidIndex(PieceIndex) && Set->Pieces[PieceIndex].LocalBounds.IsValid)
		{
			Focus += Set->Pieces[PieceIndex].LocalBounds.TransformBy(ToWorld);
		}
	}

	if (Focus.IsValid)
	{
		GEditor->MoveViewportCamerasToBox(Focus, /*bActiveViewportOnly*/ true, /*DrawDebugBoxTimeInSeconds*/ 0.0f);
	}

	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Profile
// -------------------------------------------------------------------------------------------

FReply SArchOpeningExtractionPanel::OnSaveProfileClicked()
{
	UArchOpeningExtractionSubsystem* Subsystem =
		GEditor ? GEditor->GetEditorSubsystem<UArchOpeningExtractionSubsystem>() : nullptr;

	const UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Subsystem == nullptr || Set == nullptr)
	{
		return FReply::Handled();
	}

	FText Error;
	const UArchOpeningExtractionProfile* Profile = Subsystem->SaveProfile(Set, Error);

	StatusText = (Profile != nullptr)
		? FText::Format(
			LOCTEXT("ProfileSavedFmt", "Assignments saved to '{0}'. Save that asset from the Content Browser to keep them."),
			FText::FromString(Profile->GetName()))
		: Error;

	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnReloadProfileClicked()
{
	UArchOpeningExtractionSubsystem* Subsystem =
		GEditor ? GEditor->GetEditorSubsystem<UArchOpeningExtractionSubsystem>() : nullptr;

	UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Subsystem == nullptr || Set == nullptr)
	{
		return FReply::Handled();
	}

	const UArchOpeningExtractionProfile* Profile = Subsystem->FindProfileFor(Set->SourceMesh);
	if (Profile == nullptr)
	{
		StatusText = LOCTEXT("NoProfile", "There is no saved profile for this mesh yet.");
		return FReply::Handled();
	}

	int32 Matched = 0;
	int32 Unmatched = 0;
	Subsystem->ApplyProfileTo(Set, Profile, Matched, Unmatched);

	RefreshGroupRows();
	RefreshRows();
	InvalidateViewport();

	StatusText = FText::Format(
		LOCTEXT("ProfileReloadedFmt", "Restored {0} assignment(s); {1} piece(s) were not in the profile."),
		FText::AsNumber(Matched), FText::AsNumber(Unmatched));

	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Output
// -------------------------------------------------------------------------------------------

FReply SArchOpeningExtractionPanel::OnExtractClicked()
{
	UArchOpeningExtractionSubsystem* Subsystem =
		GEditor ? GEditor->GetEditorSubsystem<UArchOpeningExtractionSubsystem>() : nullptr;

	UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Subsystem == nullptr || Set == nullptr)
	{
		StatusText = LOCTEXT("NoSubsystemExtract", "The extraction subsystem is unavailable.");
		return FReply::Handled();
	}

	// One request per group, carrying the asset that group produced last time so it can be
	// rewritten rather than duplicated.
	TArray<FArchOpeningGroupExtractionRequest> Requests;
	Requests.Reserve(Set->Groups.Num());

	for (int32 GroupIndex = 0; GroupIndex < Set->Groups.Num(); ++GroupIndex)
	{
		FArchOpeningGroupExtractionRequest& Request = Requests.AddDefaulted_GetRef();
		Request.SourceGroupIndex = GroupIndex;
		Request.GroupName = Set->Groups[GroupIndex].GroupName;
		Request.Role = Set->Groups[GroupIndex].Role;
		Request.ExistingAsset = Set->Groups[GroupIndex].GeneratedMesh;

		for (int32 PieceIndex = 0; PieceIndex < Set->Pieces.Num(); ++PieceIndex)
		{
			if (Set->Pieces[PieceIndex].GroupIndex == GroupIndex)
			{
				Request.PieceIndices.Add(PieceIndex);
			}
		}
	}

	FArchOpeningExtractionResult Result;
	FText Error;

	const bool bSucceeded = Subsystem->ExtractGroups(
		SourceMesh.Get(), Analysis, Requests, OutputPath,
		SourceMesh.IsValid() ? SourceMesh->GetName() : FString(),
		CollisionOption, bUpdateExistingAssets, Result, Error);

	if (NotesBox.IsValid())
	{
		NotesBox->ClearChildren();
	}

	if (!bSucceeded)
	{
		StatusText = Error;
		return FReply::Handled();
	}

	// Record what each group produced, so the next extraction updates these same assets. Matched by
	// index, not by name: names are editable and a rename between runs must not orphan an asset.
	for (const FArchOpeningGroupExtractionOutput& Output : Result.Groups)
	{
		if (Set->Groups.IsValidIndex(Output.SourceGroupIndex))
		{
			Set->Groups[Output.SourceGroupIndex].GeneratedMesh = Output.Mesh.Get();
		}
	}

	// Persist the classification straight away: an extraction the artist cannot come back and
	// correct is exactly the problem this replaces.
	FText ProfileError;
	Subsystem->SaveProfile(Set, ProfileError);

	FString Summary;
	for (const FArchOpeningGroupExtractionOutput& Output : Result.Groups)
	{
		Summary += FString::Printf(TEXT("\n  %s -> %s (%d tris, %s)"),
			*Output.GroupName.ToString(),
			Output.Mesh.IsValid() ? *Output.Mesh->GetName() : TEXT("?"),
			Output.TriangleCount,
			Output.bUpdatedInPlace ? TEXT("updated in place") : TEXT("new asset"));
	}

	StatusText = FText::Format(
		LOCTEXT("ExtractedFmt", "Wrote {0} asset(s). Save them, and the profile, from the Content Browser.{1}"),
		FText::AsNumber(Result.Groups.Num()), FText::FromString(Summary));

	if (NotesBox.IsValid())
	{
		for (const FText& Note : Result.Notes)
		{
			NotesBox->AddSlot().AutoHeight().Padding(0, 1)
				[
					SNew(STextBlock).AutoWrapText(true).Text(Note)
				];
		}
	}

	RefreshGroupRows();
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnSpawnSplitActorsClicked()
{
	UArchOpeningPieceSetComponent* Set = GetPieceSet();
	UStaticMeshComponent* Source = SourceComponent.Get();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	if (Set == nullptr || Source == nullptr || World == nullptr)
	{
		return FReply::Handled();
	}

	AActor* SourceActor = Source->GetOwner();
	if (SourceActor == nullptr)
	{
		StatusText = LOCTEXT("NoSourceActor", "The source component has no owning actor to place copies against.");
		return FReply::Handled();
	}

	const FTransform SourceTransform = Source->GetComponentTransform();

	const FScopedTransaction Transaction(LOCTEXT("SpawnSplitTransaction", "Spawn Split Opening Actors"));

	TArray<AActor*> Spawned;
	SpawnedActorsByGroup.Reset();

	for (int32 GroupIndex = 0; GroupIndex < Set->Groups.Num(); ++GroupIndex)
	{
		const FArchOpeningExtractionGroup& Group = Set->Groups[GroupIndex];

		UStaticMesh* Mesh = Group.GeneratedMesh.LoadSynchronous();
		if (Mesh == nullptr)
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags = RF_Transactional;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
			SourceTransform.GetLocation(), SourceTransform.Rotator(), SpawnParams);

		if (Actor == nullptr)
		{
			continue;
		}

		// Placement is preserved exactly: the extracted geometry keeps the source mesh's local
		// coordinates, so putting each actor on the source transform puts every piece back where it
		// already was. That is what lets the opening be calibrated straight away.
		Actor->SetActorScale3D(SourceTransform.GetScale3D());
		Actor->SetActorLabel(FString::Printf(TEXT("%s_%s"), *SourceActor->GetActorLabel(), *Group.GroupName.ToString()));

		if (UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent())
		{
			// Movable groups will be animated, so they need Movable mobility from the start.
			// Stationary groups keep whatever the source had, so baked lighting is not disturbed.
			const EComponentMobility::Type TargetMobility = (Group.Role == EArchOpeningGroupRole::Movable)
				? EComponentMobility::Movable
				: Source->Mobility.GetValue();

			MeshComponent->SetMobility(TargetMobility);
			MeshComponent->SetStaticMesh(Mesh);
		}

		Spawned.Add(Actor);
		SpawnedActorsByGroup.Add(GroupIndex, Actor);
	}

	if (Spawned.IsEmpty())
	{
		StatusText = LOCTEXT("NothingToSpawn", "No group has a generated asset yet. Run Create / Update Group Assets first.");
		return FReply::Handled();
	}

	// The source actor is hidden rather than deleted: hiding is reversible and keeps the original
	// available if the split turns out to be wrong.
	SourceActor->Modify();
	SourceActor->SetIsTemporarilyHiddenInEditor(true);

	StatusText = FText::Format(
		LOCTEXT("SpawnedFmt", "Spawned {0} actor(s) on the source transform and hid the source actor. 'Build Openings' will wire them up."),
		FText::AsNumber(Spawned.Num()));

	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnCreateOpeningsClicked()
{
	UArchOpeningPieceSetComponent* Set = GetPieceSet();
	UStaticMeshComponent* Source = SourceComponent.Get();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	if (Set == nullptr || Source == nullptr || World == nullptr)
	{
		return FReply::Handled();
	}

	// Sort the spawned actors into the fixed parts every opening shares, and one leaf per movable
	// group. A double door is two openings over one shared frame, which is exactly the runtime's
	// model: one opening component drives one leaf.
	TArray<AActor*> StationaryActors;
	TArray<TPair<int32, AActor*>> MovableActors;

	for (const TPair<int32, TWeakObjectPtr<AActor>>& Pair : SpawnedActorsByGroup)
	{
		AActor* Actor = Pair.Value.Get();
		if (!::IsValid(Actor) || !Set->Groups.IsValidIndex(Pair.Key))
		{
			continue;
		}

		if (Set->Groups[Pair.Key].Role == EArchOpeningGroupRole::Stationary)
		{
			StationaryActors.Add(Actor);
		}
		else
		{
			MovableActors.Emplace(Pair.Key, Actor);
		}
	}

	if (MovableActors.IsEmpty())
	{
		StatusText = LOCTEXT("NoMovableActors",
			"No movable group has a spawned actor yet. Run Create / Update Group Assets, then Spawn Split Actors.");
		return FReply::Handled();
	}

	// Deterministic ordering, so leaf 1 and leaf 2 keep their handing between runs.
	MovableActors.Sort([](const TPair<int32, AActor*>& A, const TPair<int32, AActor*>& B)
	{
		return A.Key < B.Key;
	});

	const FTransform SourceTransform = Source->GetComponentTransform();

	const FScopedTransaction Transaction(LOCTEXT("BuildOpeningsTransaction", "Build Openings From Split Actors"));

	TArray<AActor*> CreatedOpenings;
	int32 LeafOrdinal = 0;

	for (const TPair<int32, AActor*>& Movable : MovableActors)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags = RF_Transactional;

		AArchOpeningActor* OpeningActor = World->SpawnActor<AArchOpeningActor>(
			SourceTransform.GetLocation(), SourceTransform.Rotator(), SpawnParams);

		if (OpeningActor == nullptr)
		{
			continue;
		}

		UArchOpeningComponent* Opening = OpeningActor->GetOpening();
		if (Opening == nullptr)
		{
			continue;
		}

		OpeningActor->SetActorLabel(FString::Printf(TEXT("Opening_%s"),
			*Set->Groups[Movable.Key].GroupName.ToString()));

		FText AssignError;

		// Every opening shares the fixed parts. Stationary parts are only recorded, never driven,
		// so two openings referencing the same frame do not fight over it.
		for (AActor* StationaryActor : StationaryActors)
		{
			if (USceneComponent* Root = StationaryActor->GetRootComponent())
			{
				Opening->AssignPart(Root, EArchOpeningPartRole::Stationary, INDEX_NONE, AssignError);
			}
		}

		if (USceneComponent* LeafRoot = Movable.Value->GetRootComponent())
		{
			Opening->AssignPart(LeafRoot, EArchOpeningPartRole::Leaf, INDEX_NONE, AssignError);
		}

		// Alternate the handing, which is the usual arrangement for a pair of leaves. It is only a
		// starting point; the artist sets the real side, swing and angle in the setup panel.
		Opening->Hinged.HingeSide = (LeafOrdinal % 2 == 0)
			? EArchOpeningHingeSide::Left
			: EArchOpeningHingeSide::Right;

		Opening->SetCurrentPoseAsClosed();
		Opening->SnapHingeToLeafEdge();

		CreatedOpenings.Add(OpeningActor);
		++LeafOrdinal;
	}

	if (CreatedOpenings.IsEmpty())
	{
		StatusText = LOCTEXT("OpeningsFailed", "No opening could be created.");
		return FReply::Handled();
	}

	GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
	for (AActor* Created : CreatedOpenings)
	{
		GEditor->SelectActor(Created, /*bInSelected*/ true, /*bNotify*/ false);
	}
	GEditor->NoteSelectionChange();

	StatusText = FText::Format(
		LOCTEXT("OpeningsBuiltFmt",
			"Built {0} opening(s), each with the fixed parts assigned, its own leaf, and the closed pose captured. "
			"Open Window > Architectural Openings to set the outside direction, swing and timing."),
		FText::AsNumber(CreatedOpenings.Num()));

	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Labels
// -------------------------------------------------------------------------------------------

FText SArchOpeningExtractionPanel::GetSourceLabel() const
{
	const UStaticMesh* Mesh = SourceMesh.Get();
	if (Mesh == nullptr)
	{
		return LOCTEXT("NoSource", "No source selected. Select the Static Mesh Actor in the level, then press the button below.");
	}

	return FText::Format(LOCTEXT("SourceFmt", "Source: {0}"), FText::FromString(Mesh->GetPathName()));
}

FText SArchOpeningExtractionPanel::GetStatusText() const
{
	return StatusText;
}

FText SArchOpeningExtractionPanel::GetSelectionSummary() const
{
	const UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Set == nullptr)
	{
		return FText::GetEmpty();
	}

	const FName ActiveName = Set->Groups.IsValidIndex(Set->ActiveGroupIndex)
		? Set->Groups[Set->ActiveGroupIndex].GroupName
		: NAME_None;

	return FText::Format(
		LOCTEXT("SelectionSummaryFmt", "Active group: {0}     Ticked: {1} of {2} piece(s)"),
		FText::FromName(ActiveName),
		FText::AsNumber(Set->SelectedPieces.Num()),
		FText::AsNumber(Set->Pieces.Num()));
}

bool SArchOpeningExtractionPanel::CanAnalyze() const
{
	return SourceMesh.IsValid();
}

bool SArchOpeningExtractionPanel::CanExtract() const
{
	const UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Set == nullptr || Set->Pieces.IsEmpty())
	{
		return false;
	}

	// At least two groups have to be populated, or there is nothing to separate.
	int32 PopulatedGroups = 0;
	for (int32 GroupIndex = 0; GroupIndex < Set->Groups.Num(); ++GroupIndex)
	{
		if (Set->CountPiecesInGroup(GroupIndex) > 0)
		{
			++PopulatedGroups;
		}
	}

	return PopulatedGroups >= 2;
}

bool SArchOpeningExtractionPanel::CanCreateOpenings() const
{
	const UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Set == nullptr)
	{
		return false;
	}

	for (const TPair<int32, TWeakObjectPtr<AActor>>& Pair : SpawnedActorsByGroup)
	{
		if (Pair.Value.IsValid() && Set->Groups.IsValidIndex(Pair.Key) &&
			Set->Groups[Pair.Key].Role == EArchOpeningGroupRole::Movable)
		{
			return true;
		}
	}

	return false;
}

bool SArchOpeningExtractionPanel::CanSpawnActors() const
{
	const UArchOpeningPieceSetComponent* Set = GetPieceSet();
	if (Set == nullptr || !SourceComponent.IsValid())
	{
		return false;
	}

	for (const FArchOpeningExtractionGroup& Group : Set->Groups)
	{
		if (!Group.GeneratedMesh.IsNull())
		{
			return true;
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
