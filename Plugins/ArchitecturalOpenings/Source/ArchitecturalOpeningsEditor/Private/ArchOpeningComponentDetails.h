// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UArchOpeningComponent;

/**
 * Adds the commands a property grid cannot express to the opening's Details panel: calibration,
 * snapping, preview and a live validation summary. Everything persistent goes through a
 * transaction so Undo/Redo works; preview does not, because it only writes transient poses.
 */
class FArchOpeningComponentDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	//~ Begin IDetailCustomization Interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface

private:
	UArchOpeningComponent* GetOpening() const;

	FReply OnSetClosedPose();
	FReply OnResetToClosed();
	FReply OnRestorePrePreview();
	FReply OnPreviewToggle();
	FReply OnStopPreview();
	FReply OnSnapHinge();
	FReply OnSnapSlide();
	FReply OnReanchor();

	FText GetValidationSummary() const;

	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
};
