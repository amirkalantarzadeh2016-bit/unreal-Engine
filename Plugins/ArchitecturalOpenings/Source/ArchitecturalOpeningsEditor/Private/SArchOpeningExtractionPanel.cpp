// Copyright Epic Games, Inc. All Rights Reserved.

#include "SArchOpeningExtractionPanel.h"

#include "ArchOpeningLog.h"

#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "ArchOpeningExtractionPanel"

namespace ArchOpeningExtractionPreview
{
	/** Upper bound on debug lines drawn for the per-triangle preview, to keep the viewport usable. */
	static constexpr int32 MaxPreviewLines = 30000;

	static const FColor SelectedColor(60, 230, 100);
	static const FColor RetainedColor(150, 150, 155);

	static constexpr float PreviewLifetime = 30.0f;
}

void SArchOpeningExtractionPanel::Construct(const FArguments& /*InArgs*/)
{
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
					"Use this when the leaf you want to animate is a separate piece of geometry inside one static mesh. "
					"Pieces are found by walking triangle adjacency: two triangles belong to the same piece when they share a vertex. "
					"Hard edges and UV seams do not split a piece; duplicated vertices from an exporter can, which is what the weld tolerance is for. "
					"If the frame and the leaf are welded into a single piece, they cannot be separated here."))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[ SNew(SSeparator) ]

			// Source
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				SNew(STextBlock).Text(LOCTEXT("Step1", "1.  Source mesh"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock).AutoWrapText(true).Text(this, &SArchOpeningExtractionPanel::GetSourceLabel)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("UseSelection", "Use Selected Static Mesh"))
				.OnClicked(this, &SArchOpeningExtractionPanel::OnUseSelectionClicked)
			]

			// Analyse
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(STextBlock).Text(LOCTEXT("Step2", "2.  Analyse"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("WeldTolerance", "Weld tolerance (cm)"))
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f)
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
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Analyze", "Analyse Pieces"))
				.IsEnabled(this, &SArchOpeningExtractionPanel::CanAnalyze)
				.OnClicked(this, &SArchOpeningExtractionPanel::OnAnalyzeClicked)
			]

			// Selection
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(STextBlock).Text(LOCTEXT("Step3", "3.  Tick the pieces that make up the movable leaf"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2).MaxHeight(320.0f)
			[
				SAssignNew(ListView, SListView<TSharedPtr<FArchOpeningPieceRow>>)
				.ListItemsSource(&Rows)
				.OnGenerateRow(this, &SArchOpeningExtractionPanel::MakePieceRow)
				.SelectionMode(ESelectionMode::None)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Preview", "Preview Selection In Viewport"))
					.ToolTipText(LOCTEXT("PreviewTip", "Draws the selected pieces in green and the retained pieces in grey, in the level viewport."))
					.OnClicked(this, &SArchOpeningExtractionPanel::OnPreviewClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearPreview", "Clear Preview"))
					.OnClicked(this, &SArchOpeningExtractionPanel::OnClearPreviewClicked)
				]
			]

			// Output
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(STextBlock).Text(LOCTEXT("Step4", "4.  Output"))
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
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("CollisionLabel", "Collision on new assets"))
				]

				+ SHorizontalBox::Slot().AutoWidth()
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
						SNew(STextBlock).Text(LOCTEXT("ComplexAsSimple", "Use complex as simple (needed for clicking)"))
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SButton)
				.Text(LOCTEXT("Extract", "Create Leaf And Fixed Assets"))
				.IsEnabled(this, &SArchOpeningExtractionPanel::CanExtract)
				.OnClicked(this, &SArchOpeningExtractionPanel::OnExtractClicked)
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
	// Leaving debug lines behind after the tool closes would be confusing; clear them.
	if (GEditor != nullptr)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			FlushPersistentDebugLines(World);
		}
	}
}

FText SArchOpeningExtractionPanel::GetSourceLabel() const
{
	const UStaticMesh* Mesh = SourceMesh.Get();
	if (Mesh == nullptr)
	{
		return LOCTEXT("NoSource", "No source selected. Select a Static Mesh Actor (or a static mesh component) in the level and press the button below.");
	}

	return FText::Format(LOCTEXT("SourceFmt", "Source: {0}"), FText::FromString(Mesh->GetPathName()));
}

FText SArchOpeningExtractionPanel::GetStatusText() const
{
	return StatusText;
}

bool SArchOpeningExtractionPanel::CanAnalyze() const
{
	return SourceMesh.IsValid();
}

bool SArchOpeningExtractionPanel::CanExtract() const
{
	if (!Analysis.bValid || Analysis.Pieces.Num() <= 1)
	{
		return false;
	}

	int32 SelectedCount = 0;
	for (const TSharedPtr<FArchOpeningPieceRow>& Row : Rows)
	{
		if (Row.IsValid() && Row->bSelected)
		{
			++SelectedCount;
		}
	}

	return SelectedCount > 0 && SelectedCount < Analysis.Pieces.Num();
}

FReply SArchOpeningExtractionPanel::OnUseSelectionClicked()
{
	SourceMesh = nullptr;
	SourceComponent = nullptr;
	Analysis = FArchOpeningMeshAnalysis();
	RefreshRows();

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

	StatusText = SourceMesh.IsValid()
		? LOCTEXT("SourceSet", "Source set. Press Analyse Pieces.")
		: LOCTEXT("SourceNotFound", "No static mesh found in the selection.");

	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnAnalyzeClicked()
{
	Analysis = FArchOpeningMeshAnalysis();
	RefreshRows();

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

	RefreshRows();

	if (Analysis.IsSinglePiece())
	{
		// The unsupported case, reported plainly rather than half-attempted.
		StatusText = Error;
		return FReply::Handled();
	}

	StatusText = FText::Format(
		LOCTEXT("AnalyzedFmt", "Found {0} disconnected piece(s) across {1} triangles. Tick the pieces that move as the leaf."),
		FText::AsNumber(Analysis.Pieces.Num()), FText::AsNumber(Analysis.TotalTriangles));

	return FReply::Handled();
}

void SArchOpeningExtractionPanel::RefreshRows()
{
	Rows.Reset();

	for (int32 PieceIndex = 0; PieceIndex < Analysis.Pieces.Num(); ++PieceIndex)
	{
		TSharedPtr<FArchOpeningPieceRow> Row = MakeShared<FArchOpeningPieceRow>();
		Row->PieceIndex = PieceIndex;
		Row->bSelected = false;
		Row->Label = UArchOpeningExtractionSubsystem::DescribePiece(Analysis, PieceIndex);
		Rows.Add(Row);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SArchOpeningExtractionPanel::MakePieceRow(TSharedPtr<FArchOpeningPieceRow> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FArchOpeningPieceRow>>, OwnerTable)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().Padding(2, 1)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]()
				{
					return Item.IsValid() && Item->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Item](ECheckBoxState NewState)
				{
					if (Item.IsValid())
					{
						Item->bSelected = (NewState == ECheckBoxState::Checked);
					}
				})
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4, 1)
			[
				SNew(STextBlock).Text(Item.IsValid() ? Item->Label : FText::GetEmpty())
			]
		];
}

FReply SArchOpeningExtractionPanel::OnPreviewClicked()
{
	DrawPreview();
	return FReply::Handled();
}

FReply SArchOpeningExtractionPanel::OnClearPreviewClicked()
{
	if (GEditor != nullptr)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			FlushPersistentDebugLines(World);
		}
	}
	return FReply::Handled();
}

void SArchOpeningExtractionPanel::DrawPreview()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const UStaticMeshComponent* Component = SourceComponent.Get();

	if (World == nullptr || Component == nullptr || !Analysis.bValid)
	{
		StatusText = LOCTEXT("PreviewNeedsComponent", "Preview needs the source mesh to be placed in the level: select the Static Mesh Actor, press 'Use Selected Static Mesh', then Analyse.");
		return;
	}

	FlushPersistentDebugLines(World);

	const FTransform ToWorld = Component->GetComponentTransform();

	// Bounding boxes first: always affordable, and enough to tell the pieces apart at a glance.
	for (int32 PieceIndex = 0; PieceIndex < Analysis.Pieces.Num(); ++PieceIndex)
	{
		const FArchOpeningMeshPiece& Piece = Analysis.Pieces[PieceIndex];
		if (!Piece.LocalBounds.IsValid)
		{
			continue;
		}

		const bool bSelected = Rows.IsValidIndex(PieceIndex) && Rows[PieceIndex].IsValid() && Rows[PieceIndex]->bSelected;
		const FColor Color = bSelected
			? ArchOpeningExtractionPreview::SelectedColor
			: ArchOpeningExtractionPreview::RetainedColor;

		DrawDebugBox(
			World,
			ToWorld.TransformPosition(Piece.LocalBounds.GetCenter()),
			Piece.LocalBounds.GetExtent() * ToWorld.GetScale3D().GetAbsMax(),
			ToWorld.GetRotation(),
			Color,
			/*bPersistentLines*/ true,
			ArchOpeningExtractionPreview::PreviewLifetime,
			/*DepthPriority*/ 0,
			bSelected ? 2.0f : 1.0f);
	}

	StatusText = FText::Format(
		LOCTEXT("PreviewDrawnFmt", "Preview drawn: selected pieces in green, retained pieces in grey. Lines clear after {0} seconds, or press Clear Preview."),
		FText::AsNumber(FMath::RoundToInt(ArchOpeningExtractionPreview::PreviewLifetime)));
}

FReply SArchOpeningExtractionPanel::OnExtractClicked()
{
	UArchOpeningExtractionSubsystem* Subsystem =
		GEditor ? GEditor->GetEditorSubsystem<UArchOpeningExtractionSubsystem>() : nullptr;

	if (Subsystem == nullptr)
	{
		StatusText = LOCTEXT("NoSubsystemExtract", "The extraction subsystem is unavailable.");
		return FReply::Handled();
	}

	TArray<int32> Selected;
	for (const TSharedPtr<FArchOpeningPieceRow>& Row : Rows)
	{
		if (Row.IsValid() && Row->bSelected)
		{
			Selected.Add(Row->PieceIndex);
		}
	}

	FArchOpeningExtractionResult Result;
	FText Error;

	const bool bSucceeded = Subsystem->ExtractPieces(
		SourceMesh.Get(), Analysis, Selected, OutputPath,
		SourceMesh.IsValid() ? SourceMesh->GetName() : FString(),
		CollisionOption, Result, Error);

	if (NotesBox.IsValid())
	{
		NotesBox->ClearChildren();
	}

	if (!bSucceeded)
	{
		StatusText = Error;
		return FReply::Handled();
	}

	StatusText = FText::Format(
		LOCTEXT("ExtractedFmt", "Created '{0}' ({1} triangles) and '{2}' ({3} triangles). Save them from the Content Browser."),
		FText::FromString(Result.ExtractedMesh.IsValid() ? Result.ExtractedMesh->GetName() : TEXT("?")),
		FText::AsNumber(Result.ExtractedTriangles),
		FText::FromString(Result.RemainderMesh.IsValid() ? Result.RemainderMesh->GetName() : TEXT("?")),
		FText::AsNumber(Result.RemainderTriangles));

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

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
