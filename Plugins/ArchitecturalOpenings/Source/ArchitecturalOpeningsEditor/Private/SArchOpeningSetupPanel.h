// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchOpeningTypes.h"
#include "Widgets/SCompoundWidget.h"

class UArchOpeningComponent;
class SVerticalBox;
class IDetailsView;

/**
 * The Architectural Openings setup panel.
 *
 * Presents the setup as an ordered sequence, so an artist can work top to bottom without knowing
 * anything about the underlying component:
 *
 *   1  Pick the opening to configure (or create one at the current selection).
 *   2  Assign stationary parts from the level selection.
 *   3  Assign movable leaf parts.
 *   4  Assign handle parts to a handle group.
 *   5  Choose hinged or sliding.
 *   6  Place the hinge or the slide path.
 *   7  Set direction and range.
 *   8  Set timing.
 *   9  Set interaction.
 *  10  Set sounds.
 *  11  Preview.
 *  12  Save the level.
 *
 * Steps 5 to 10 are edited through the embedded details view, which is the same set of properties
 * the Details panel shows, with the same categories and tooltips. The panel adds the commands that
 * a property grid cannot express: selection-driven assignment, calibration, snapping and preview.
 */
class SArchOpeningSetupPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArchOpeningSetupPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SArchOpeningSetupPanel() override;

private:
	// Target resolution
	UArchOpeningComponent* GetTarget() const { return TargetOpening.Get(); }
	void RefreshTargetFromSelection();
	FReply OnCreateOpeningClicked();

	// Assignment
	FReply OnAssignSelectionClicked(EArchOpeningPartRole Role);
	FReply OnClearRoleClicked(EArchOpeningPartRole Role);
	FReply OnAddHandleGroupClicked();
	FReply OnRemoveHandleGroupClicked();

	// Calibration and snapping
	FReply OnSetClosedPoseClicked();
	FReply OnResetToClosedClicked();
	FReply OnReanchorClicked();
	FReply OnSnapHingeClicked();
	FReply OnSnapSlideClicked();
	FReply OnSnapHandlePivotClicked();

	// Preview
	FReply OnPreviewOpenClicked();
	FReply OnPreviewCloseClicked();
	FReply OnPreviewToggleClicked();
	FReply OnStopPreviewClicked();
	FReply OnRestorePreviewClicked();
	void OnPreviewScrubChanged(float NewValue);
	float GetPreviewScrub() const;

	// Extraction
	FReply OnOpenExtractionClicked();

	// State
	EVisibility GetNoTargetVisibility() const;
	EVisibility GetHasTargetVisibility() const;
	FText GetTargetLabel() const;
	FText GetStateLabel() const;
	FText GetHandleGroupLabel() const;
	bool HasTarget() const { return TargetOpening.IsValid(); }
	bool CanPreview() const;

	// Validation summary
	void RefreshValidation();
	TSharedRef<class SWidget> BuildValidationList();

	/** Level-selected scene components that can be assigned, honouring the documented restrictions. */
	void GatherAssignableSelection(TArray<USceneComponent*>& OutComponents) const;

	void HandleSelectionChanged(UObject* NewSelection);

	TWeakObjectPtr<UArchOpeningComponent> TargetOpening;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SVerticalBox> ValidationBox;

	FArchOpeningValidationReport CachedReport;
	int32 ActiveHandleGroup = 0;
	float PreviewScrub = 0.0f;

	FDelegateHandle SelectionChangedHandle;
	FText StatusMessage;
};
