// Copyright Epic Games, Inc. All Rights Reserved.

#include "SArchOpeningSetupPanel.h"

#include "ArchOpeningActor.h"
#include "ArchOpeningComponent.h"
#include "ArchOpeningFunctionLibrary.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningPreviewManager.h"
#include "ArchitecturalOpeningsEditorModule.h"

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ArchOpeningSetupPanel"

namespace ArchOpeningPanel
{
	TSharedRef<SWidget> StepHeader(const FText& Number, const FText& Title)
	{
		return SNew(SBorder)
			.Padding(FMargin(6.0f, 4.0f))
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("StepFmt", "{0}  {1}"), Number, Title))
			];
	}

	TSharedRef<SWidget> StepHeader(int32 Step, const FText& Title)
	{
		return StepHeader(FText::Format(LOCTEXT("StepNumberFmt", "{0}."), FText::AsNumber(Step)), Title);
	}
}

void SArchOpeningSetupPanel::Construct(const FArguments& /*InArgs*/)
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowOptions = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditor.CreateDetailView(DetailsArgs);

	if (GEditor != nullptr)
	{
		SelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw(this, &SArchOpeningSetupPanel::HandleSelectionChanged);
	}

	auto Step = [](int32 Number, const FText& Title) { return ArchOpeningPanel::StepHeader(Number, Title); };
	auto StepRange = [](const FText& Range, const FText& Title) { return ArchOpeningPanel::StepHeader(Range, Title); };

	ChildSlot
	[
		SNew(SScrollBox)

		+ SScrollBox::Slot().Padding(4.0f)
		[
			SNew(SVerticalBox)

			// ---- 1. Target -------------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(1, LOCTEXT("StepTarget", "Select or assign source objects"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.Text(this, &SArchOpeningSetupPanel::GetTargetLabel)
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SUniformGridPanel).SlotPadding(2.0f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CreateOpening", "Create Opening Here"))
					.ToolTipText(LOCTEXT("CreateOpeningTip", "Spawns an Architectural Opening actor at the centre of the current selection and makes it the target."))
					.OnClicked(this, &SArchOpeningSetupPanel::OnCreateOpeningClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SSeparator)
			]

			// ---- 2-4. Assignment -------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(2, LOCTEXT("StepStationary", "Identify stationary parts"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SUniformGridPanel).SlotPadding(2.0f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AssignStationary", "Assign Selection"))
					.ToolTipText(LOCTEXT("AssignStationaryTip", "Records the selected meshes as parts of this opening that must never move: outer frame, fixed glazing, fixed profiles. Their mobility is never changed and they are never written to."))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningSetupPanel::OnAssignSelectionClicked, EArchOpeningPartRole::Stationary))
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearStationary", "Clear"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningSetupPanel::OnClearRoleClicked, EArchOpeningPartRole::Stationary))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(3, LOCTEXT("StepLeaf", "Identify the movable leaf"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SUniformGridPanel).SlotPadding(2.0f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AssignLeaf", "Assign Selection"))
					.ToolTipText(LOCTEXT("AssignLeafTip", "Records the selected meshes as one movable leaf: the leaf's PVC profiles, its glass, its trim. They move together as a rigid assembly."))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningSetupPanel::OnAssignSelectionClicked, EArchOpeningPartRole::Leaf))
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearLeaf", "Clear"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningSetupPanel::OnClearRoleClicked, EArchOpeningPartRole::Leaf))
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(4, LOCTEXT("StepHandles", "Assign handle parts (optional)"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.Text(this, &SArchOpeningSetupPanel::GetHandleGroupLabel)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SUniformGridPanel).SlotPadding(2.0f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AddHandleGroup", "Add Group"))
					.ToolTipText(LOCTEXT("AddHandleGroupTip", "Adds a handle group. Meshes in one group rotate together about one pivot; use separate groups for hardware that needs its own pivot or direction."))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnAddHandleGroupClicked)
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("AssignHandle", "Assign Selection"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(FOnClicked::CreateSP(this, &SArchOpeningSetupPanel::OnAssignSelectionClicked, EArchOpeningPartRole::Handle))
				]

				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveHandleGroup", "Remove Group"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnRemoveHandleGroupClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SSeparator)
			]

			// ---- Calibration -----------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(5, LOCTEXT("StepCalibrate", "Capture the closed pose"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("CalibrateHelp", "Place every mesh where it belongs when the opening is shut, then capture. The captured pose is what every animated pose is computed from, so nothing drifts and the leaf always returns exactly here. You do not need to change any pivot in your modelling package."))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SUniformGridPanel).SlotPadding(2.0f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SetClosed", "Set Current Pose As Closed"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnSetClosedPoseClicked)
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ResetClosed", "Reset To Closed Pose"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnResetToClosedClicked)
				]

				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Reanchor", "Re-anchor Parts"))
					.ToolTipText(LOCTEXT("ReanchorTip", "Carries every assigned part rigidly with the opening after the opening has been moved, and re-anchors the calibration to its new place."))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnReanchorClicked)
				]
			]

			// ---- Motion placement ------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(6, LOCTEXT("StepPlacement", "Choose hinged or sliding, and place the hinge or slide path"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SUniformGridPanel).SlotPadding(2.0f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SnapHinge", "Snap Hinge To Leaf Edge"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnSnapHingeClicked)
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SnapSlide", "Fit Slide To Leaf Width"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnSnapSlideClicked)
				]

				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SnapHandlePivot", "Snap Handle Pivot"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnSnapHandlePivotClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SSeparator)
			]

			// ---- 7-10. Settings --------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				StepRange(LOCTEXT("StepRange710", "7-10."), LOCTEXT("StepSettings", "Direction and range, timing, interaction, sounds"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2).MaxHeight(700.0f)
			[
				DetailsView.ToSharedRef()
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SSeparator)
			]

			// ---- 11. Preview -----------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(11, LOCTEXT("StepPreview", "Preview"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("PreviewHelp", "Preview runs without Play In Editor. It only moves the meshes; it never rewrites the captured closed pose and never adds a transaction per frame. Restore Pre-Preview Pose puts everything back exactly."))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SUniformGridPanel).SlotPadding(2.0f)

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("PreviewOpen", "Open"))
					.IsEnabled(this, &SArchOpeningSetupPanel::CanPreview)
					.OnClicked(this, &SArchOpeningSetupPanel::OnPreviewOpenClicked)
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("PreviewClose", "Close"))
					.IsEnabled(this, &SArchOpeningSetupPanel::CanPreview)
					.OnClicked(this, &SArchOpeningSetupPanel::OnPreviewCloseClicked)
				]

				+ SUniformGridPanel::Slot(2, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("PreviewToggle", "Toggle"))
					.IsEnabled(this, &SArchOpeningSetupPanel::CanPreview)
					.OnClicked(this, &SArchOpeningSetupPanel::OnPreviewToggleClicked)
				]

				+ SUniformGridPanel::Slot(0, 1)
				[
					SNew(SButton)
					.Text(LOCTEXT("PreviewStop", "Stop Preview"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnStopPreviewClicked)
				]

				+ SUniformGridPanel::Slot(1, 1)
				[
					SNew(SButton)
					.Text(LOCTEXT("PreviewRestore", "Restore Pre-Preview Pose"))
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
					.OnClicked(this, &SArchOpeningSetupPanel::OnRestorePreviewClicked)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
				[
					SNew(STextBlock).Text(LOCTEXT("Openness", "Openness"))
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SSlider)
					.Value(this, &SArchOpeningSetupPanel::GetPreviewScrub)
					.OnValueChanged(this, &SArchOpeningSetupPanel::OnPreviewScrubChanged)
					.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SSeparator)
			]

			// ---- Extraction ------------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				StepRange(LOCTEXT("StepOptional", "Optional."), LOCTEXT("StepExtract", "One-mesh source? Extract the leaf into its own asset"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenExtraction", "Open Leaf Extraction Tool"))
				.ToolTipText(LOCTEXT("OpenExtractionTip", "Splits a static mesh into its disconnected geometric pieces and writes the selected pieces out as new assets. Only use this when the leaf is a separate piece of geometry inside one mesh; welded frame-and-leaf geometry cannot be separated this way."))
				.IsEnabled(this, &SArchOpeningSetupPanel::HasTarget)
				.OnClicked(this, &SArchOpeningSetupPanel::OnOpenExtractionClicked)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SSeparator)
			]

			// ---- Validation ------------------------------------------------------------------
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
			[
				Step(12, LOCTEXT("StepValidate", "Check, then save the level"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.Text(this, &SArchOpeningSetupPanel::GetStateLabel)
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
			[
				SAssignNew(ValidationBox, SVerticalBox)
			]
		]
	];

	RefreshTargetFromSelection();
}

SArchOpeningSetupPanel::~SArchOpeningSetupPanel()
{
	// The panel outlives no delegates: removing here is what keeps closing the tab from leaving a
	// dangling raw binding on the editor's selection event.
	if (SelectionChangedHandle.IsValid())
	{
		USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);
		SelectionChangedHandle.Reset();
	}
}

// -------------------------------------------------------------------------------------------
// Target resolution
// -------------------------------------------------------------------------------------------

void SArchOpeningSetupPanel::HandleSelectionChanged(UObject* /*NewSelection*/)
{
	RefreshTargetFromSelection();
}

void SArchOpeningSetupPanel::RefreshTargetFromSelection()
{
	if (GEditor == nullptr)
	{
		return;
	}

	// Selecting an opening actor retargets the panel. Selecting anything else keeps the current
	// target, so an artist can select meshes and press Assign without losing their place.
	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (Actor == nullptr)
		{
			continue;
		}

		TArray<UArchOpeningComponent*> Openings;
		Actor->GetComponents(Openings);

		if (Openings.Num() > 0)
		{
			TargetOpening = Openings[0];
			ActiveHandleGroup = FMath::Clamp(ActiveHandleGroup, 0, FMath::Max(0, Openings[0]->HandleGroups.Num() - 1));
			break;
		}
	}

	if (DetailsView.IsValid())
	{
		UArchOpeningComponent* Target = TargetOpening.Get();
		DetailsView->SetObject(Target, /*bForceRefresh*/ true);
	}

	if (UArchOpeningComponent* Target = TargetOpening.Get())
	{
		PreviewScrub = Target->GetOpenness();
	}

	RefreshValidation();
}

FReply SArchOpeningSetupPanel::OnCreateOpeningClicked()
{
	if (GEditor == nullptr)
	{
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		return FReply::Handled();
	}

	// Spawn at the centre of the current selection so the calibration frame starts somewhere useful.
	FVector SpawnLocation = FVector::ZeroVector;
	FRotator SpawnRotation = FRotator::ZeroRotator;

	TArray<USceneComponent*> Selected;
	GatherAssignableSelection(Selected);

	if (Selected.Num() > 0)
	{
		FBox Bounds(ForceInit);
		for (const USceneComponent* Component : Selected)
		{
			Bounds += Component->Bounds.GetBox();
		}
		SpawnLocation = Bounds.GetCenter();

		// Borrow the first selection's yaw so the opening's local frame lines up with the geometry.
		SpawnRotation = FRotator(0.0f, Selected[0]->GetComponentRotation().Yaw, 0.0f);
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateOpeningTransaction", "Create Architectural Opening"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transactional;

	AArchOpeningActor* NewActor = World->SpawnActor<AArchOpeningActor>(SpawnLocation, SpawnRotation, SpawnParams);
	if (NewActor != nullptr)
	{
		NewActor->SetActorLabel(TEXT("ArchOpening"));
		TargetOpening = NewActor->GetOpening();

		GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
		GEditor->SelectActor(NewActor, /*bInSelected*/ true, /*bNotify*/ true);
	}

	RefreshTargetFromSelection();
	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Selection gathering
// -------------------------------------------------------------------------------------------

void SArchOpeningSetupPanel::GatherAssignableSelection(TArray<USceneComponent*>& OutComponents) const
{
	OutComponents.Reset();

	if (GEditor == nullptr)
	{
		return;
	}

	// Component selection first: this is what lets an artist assign individual mesh components that
	// belong to one larger actor, rather than only whole Static Mesh Actors.
	USelection* ComponentSelection = GEditor->GetSelectedComponents();
	if (ComponentSelection != nullptr && ComponentSelection->Num() > 0)
	{
		for (FSelectionIterator It(*ComponentSelection); It; ++It)
		{
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(*It))
			{
				OutComponents.AddUnique(SceneComponent);
			}
		}

		if (OutComponents.Num() > 0)
		{
			return;
		}
	}

	// Otherwise take the root component of every selected actor, which covers the common case of a
	// set of separate Static Mesh Actors.
	USelection* ActorSelection = GEditor->GetSelectedActors();
	if (ActorSelection == nullptr)
	{
		return;
	}

	for (FSelectionIterator It(*ActorSelection); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (Actor == nullptr)
		{
			continue;
		}

		// Never assign the opening's own actor to itself.
		if (TargetOpening.IsValid() && Actor == TargetOpening->GetOwner())
		{
			continue;
		}

		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			OutComponents.AddUnique(Root);
		}
	}
}

// -------------------------------------------------------------------------------------------
// Assignment
// -------------------------------------------------------------------------------------------

FReply SArchOpeningSetupPanel::OnAssignSelectionClicked(EArchOpeningPartRole Role)
{
	UArchOpeningComponent* Target = GetTarget();
	if (Target == nullptr)
	{
		return FReply::Handled();
	}

	TArray<USceneComponent*> Selected;
	GatherAssignableSelection(Selected);

	if (Selected.IsEmpty())
	{
		StatusMessage = LOCTEXT("NothingSelected", "Nothing assignable is selected. Select the meshes in the level first.");
		RefreshValidation();
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("AssignPartsTransaction", "Assign Opening Parts"));

	int32 Assigned = 0;
	FText FirstError;

	for (USceneComponent* Component : Selected)
	{
		FText Error;
		if (Target->AssignPart(Component, Role, ActiveHandleGroup, Error))
		{
			++Assigned;
		}
		else if (FirstError.IsEmpty())
		{
			FirstError = Error;
		}
	}

	StatusMessage = FirstError.IsEmpty()
		? FText::Format(LOCTEXT("AssignedFmt", "Assigned {0} part(s)."), FText::AsNumber(Assigned))
		: FirstError;

	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}

	RefreshValidation();
	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnClearRoleClicked(EArchOpeningPartRole Role)
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		const FScopedTransaction Transaction(LOCTEXT("ClearRoleTransaction", "Clear Opening Parts"));
		Target->ClearRole(Role, Role == EArchOpeningPartRole::Handle ? ActiveHandleGroup : INDEX_NONE);

		if (DetailsView.IsValid())
		{
			DetailsView->ForceRefresh();
		}
		RefreshValidation();
	}

	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnAddHandleGroupClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		const FScopedTransaction Transaction(LOCTEXT("AddHandleGroupTransaction", "Add Handle Group"));
		Target->Modify();

		FArchOpeningHandleGroup Group;
		Group.GroupName = FName(*FString::Printf(TEXT("Handle %d"), Target->HandleGroups.Num() + 1));
		ActiveHandleGroup = Target->HandleGroups.Add(Group);

		if (DetailsView.IsValid())
		{
			DetailsView->ForceRefresh();
		}
		RefreshValidation();
	}

	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnRemoveHandleGroupClicked()
{
	UArchOpeningComponent* Target = GetTarget();
	if (Target == nullptr || !Target->HandleGroups.IsValidIndex(ActiveHandleGroup))
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveHandleGroupTransaction", "Remove Handle Group"));
	Target->Modify();

	Target->HandleGroups.RemoveAt(ActiveHandleGroup);
	ActiveHandleGroup = FMath::Clamp(ActiveHandleGroup, 0, FMath::Max(0, Target->HandleGroups.Num() - 1));

	if (DetailsView.IsValid())
	{
		DetailsView->ForceRefresh();
	}
	RefreshValidation();

	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Calibration
// -------------------------------------------------------------------------------------------

FReply SArchOpeningSetupPanel::OnSetClosedPoseClicked()
{
	UArchOpeningComponent* Target = GetTarget();
	if (Target == nullptr)
	{
		return FReply::Handled();
	}

	if (Target->HasPendingPreviewState())
	{
		// Refuse rather than silently promote a preview pose to the authored closed pose.
		StatusMessage = LOCTEXT("PreviewBlocksCapture", "A preview is active. Press 'Restore Pre-Preview Pose' first, so the preview pose cannot be captured as the closed pose by mistake.");
		RefreshValidation();
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("CaptureClosedTransaction", "Set Current Pose As Closed"));

	// The parts' mobility may change, so they take part in the transaction too.
	Target->ForEachPart([](const FArchOpeningPartRef& Part, EArchOpeningPartRole, int32)
	{
		if (Part.IsValidPart())
		{
			Part.Component->Modify();
			if (AActor* PartOwner = Part.Component->GetOwner())
			{
				PartOwner->Modify();
			}
		}
		return true;
	});

	StatusMessage = Target->SetCurrentPoseAsClosed()
		? LOCTEXT("CaptureOk", "Closed pose captured.")
		: LOCTEXT("CaptureFailed", "Could not capture a closed pose. Assign at least one movable leaf part, and make sure the opening is showing its closed pose.");

	RefreshValidation();
	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnResetToClosedClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		// Transient pose change only: no transaction, and the map is not dirtied by it.
		Target->ResetToClosedPose();
		PreviewScrub = 0.0f;
		RefreshValidation();
	}

	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnReanchorClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		const FScopedTransaction Transaction(LOCTEXT("ReanchorTransaction", "Re-anchor Opening Parts"));
		Target->TransportPartsWithFrameChange();
		RefreshValidation();
	}

	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnSnapHingeClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		const FScopedTransaction Transaction(LOCTEXT("SnapHingeTransaction", "Snap Hinge To Leaf Edge"));
		Target->SnapHingeToLeafEdge();

		if (DetailsView.IsValid())
		{
			DetailsView->ForceRefresh();
		}
		RefreshValidation();
	}

	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnSnapSlideClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		const FScopedTransaction Transaction(LOCTEXT("SnapSlideTransaction", "Fit Slide To Leaf Width"));
		Target->SnapSlideToLeafBounds();

		if (DetailsView.IsValid())
		{
			DetailsView->ForceRefresh();
		}
		RefreshValidation();
	}

	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnSnapHandlePivotClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		const FScopedTransaction Transaction(LOCTEXT("SnapHandleTransaction", "Snap Handle Pivot"));
		Target->SnapHandlePivotToGroupBounds(ActiveHandleGroup);

		if (DetailsView.IsValid())
		{
			DetailsView->ForceRefresh();
		}
		RefreshValidation();
	}

	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Preview
// -------------------------------------------------------------------------------------------

bool SArchOpeningSetupPanel::CanPreview() const
{
	const UArchOpeningComponent* Target = TargetOpening.Get();
	return Target != nullptr && Target->Calibration.bCalibrated;
}

FReply SArchOpeningSetupPanel::OnPreviewOpenClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		if (FArchitecturalOpeningsEditorModule::IsAvailable())
		{
			FArchitecturalOpeningsEditorModule::Get().GetPreviewManager()->Track(Target);
		}
		Target->PreviewOpen();
	}
	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnPreviewCloseClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		if (FArchitecturalOpeningsEditorModule::IsAvailable())
		{
			FArchitecturalOpeningsEditorModule::Get().GetPreviewManager()->Track(Target);
		}
		Target->PreviewClose();
	}
	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnPreviewToggleClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		if (FArchitecturalOpeningsEditorModule::IsAvailable())
		{
			FArchitecturalOpeningsEditorModule::Get().GetPreviewManager()->Track(Target);
		}
		Target->PreviewToggle();
	}
	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnStopPreviewClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		Target->StopPreview();
		PreviewScrub = Target->GetOpenness();
	}
	return FReply::Handled();
}

FReply SArchOpeningSetupPanel::OnRestorePreviewClicked()
{
	if (UArchOpeningComponent* Target = GetTarget())
	{
		if (FArchitecturalOpeningsEditorModule::IsAvailable())
		{
			FArchitecturalOpeningsEditorModule::Get().GetPreviewManager()->RestoreAndStopTracking(Target);
		}
		else
		{
			Target->RestorePrePreviewPose();
		}

		PreviewScrub = Target->GetOpenness();
	}
	return FReply::Handled();
}

void SArchOpeningSetupPanel::OnPreviewScrubChanged(float NewValue)
{
	PreviewScrub = NewValue;

	if (UArchOpeningComponent* Target = GetTarget())
	{
		if (FArchitecturalOpeningsEditorModule::IsAvailable())
		{
			// Tracked so the scrubbed pose is restored on PIE, on save, and when the tool closes.
			FArchitecturalOpeningsEditorModule::Get().GetPreviewManager()->Track(Target);
		}

		Target->SetPreviewOpenness(NewValue);
	}
}

float SArchOpeningSetupPanel::GetPreviewScrub() const
{
	const UArchOpeningComponent* Target = TargetOpening.Get();
	return Target != nullptr ? Target->GetOpenness() : PreviewScrub;
}

// -------------------------------------------------------------------------------------------
// Extraction
// -------------------------------------------------------------------------------------------

FReply SArchOpeningSetupPanel::OnOpenExtractionClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(FArchitecturalOpeningsEditorModule::ExtractionPanelTabId);

	StatusMessage = LOCTEXT("ExtractionHint", "Select the source Static Mesh Actor in the level, then press 'Use Selected Static Mesh' in the extraction tab.");
	RefreshValidation();
	return FReply::Handled();
}

// -------------------------------------------------------------------------------------------
// Labels and validation
// -------------------------------------------------------------------------------------------

EVisibility SArchOpeningSetupPanel::GetNoTargetVisibility() const
{
	return TargetOpening.IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SArchOpeningSetupPanel::GetHasTargetVisibility() const
{
	return TargetOpening.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SArchOpeningSetupPanel::GetTargetLabel() const
{
	const UArchOpeningComponent* Target = TargetOpening.Get();
	if (Target == nullptr)
	{
		return LOCTEXT("NoTarget", "No opening selected. Select an actor with an Architectural Opening component, or press Create Opening Here.");
	}

	const AActor* Owner = Target->GetOwner();
	return FText::Format(
		LOCTEXT("TargetFmt", "Editing: {0}   ({1} stationary, {2} leaf, {3} handle group(s))"),
		FText::FromString(Owner ? Owner->GetActorNameOrLabel() : TEXT("<no actor>")),
		FText::AsNumber(Target->StationaryParts.Num()),
		FText::AsNumber(Target->LeafParts.Num()),
		FText::AsNumber(Target->HandleGroups.Num()));
}

FText SArchOpeningSetupPanel::GetHandleGroupLabel() const
{
	const UArchOpeningComponent* Target = TargetOpening.Get();
	if (Target == nullptr || !Target->HandleGroups.IsValidIndex(ActiveHandleGroup))
	{
		return LOCTEXT("NoHandleGroup", "No handle group yet. Press Add Group to create one.");
	}

	const FArchOpeningHandleGroup& Group = Target->HandleGroups[ActiveHandleGroup];
	return FText::Format(
		LOCTEXT("HandleGroupFmt", "Active group: {0}   ({1} mesh(es))"),
		FText::FromName(Group.GroupName), FText::AsNumber(Group.Parts.Num()));
}

FText SArchOpeningSetupPanel::GetStateLabel() const
{
	if (!StatusMessage.IsEmpty())
	{
		return StatusMessage;
	}

	return UArchOpeningFunctionLibrary::SummarizeValidation(CachedReport);
}

void SArchOpeningSetupPanel::RefreshValidation()
{
	CachedReport = FArchOpeningValidationReport();

	if (const UArchOpeningComponent* Target = TargetOpening.Get())
	{
		CachedReport = Target->Validate();
	}

	if (!ValidationBox.IsValid())
	{
		return;
	}

	ValidationBox->ClearChildren();

	for (const FArchOpeningIssue& Issue : CachedReport.Issues)
	{
		FLinearColor Color = FLinearColor(0.65f, 0.65f, 0.65f);
		FText Prefix = LOCTEXT("PrefixInfo", "Note");

		if (Issue.Severity == EArchOpeningIssueSeverity::Error)
		{
			Color = FLinearColor(1.0f, 0.35f, 0.30f);
			Prefix = LOCTEXT("PrefixError", "Error");
		}
		else if (Issue.Severity == EArchOpeningIssueSeverity::Warning)
		{
			Color = FLinearColor(1.0f, 0.75f, 0.25f);
			Prefix = LOCTEXT("PrefixWarning", "Warning");
		}

		ValidationBox->AddSlot()
			.AutoHeight()
			.Padding(8, 1)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(Color)
				.Text(FText::Format(LOCTEXT("IssueFmt", "{0}: {1}"), Prefix, Issue.Message))
			];
	}

	if (CachedReport.Issues.IsEmpty())
	{
		ValidationBox->AddSlot()
			.AutoHeight()
			.Padding(8, 1)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FLinearColor(0.45f, 0.90f, 0.45f))
				.Text(LOCTEXT("AllGood", "No problems found. Save the level to keep this configuration."))
			];
	}
}

TSharedRef<SWidget> SArchOpeningSetupPanel::BuildValidationList()
{
	return ValidationBox.IsValid() ? StaticCastSharedRef<SWidget>(ValidationBox.ToSharedRef()) : SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE
