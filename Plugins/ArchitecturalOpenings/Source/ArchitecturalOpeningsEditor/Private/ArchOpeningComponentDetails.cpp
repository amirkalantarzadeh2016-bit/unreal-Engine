// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningComponentDetails.h"

#include "ArchOpeningComponent.h"
#include "ArchOpeningFunctionLibrary.h"
#include "ArchOpeningPreviewManager.h"
#include "ArchitecturalOpeningsEditorModule.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ArchOpeningDetails"

TSharedRef<IDetailCustomization> FArchOpeningComponentDetails::MakeInstance()
{
	return MakeShared<FArchOpeningComponentDetails>();
}

UArchOpeningComponent* FArchOpeningComponentDetails::GetOpening() const
{
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		if (UArchOpeningComponent* Opening = Cast<UArchOpeningComponent>(Object.Get()))
		{
			return Opening;
		}
	}

	return nullptr;
}

void FArchOpeningComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

	// Commands sit at the very top so they are the first thing an artist sees.
	IDetailCategoryBuilder& Commands = DetailBuilder.EditCategory(
		TEXT("Opening Commands"), LOCTEXT("CommandsCategory", "Opening Commands"), ECategoryPriority::Important);

	Commands.AddCustomRow(LOCTEXT("CalibrationRowFilter", "Calibration"))
		.WholeRowContent()
		[
			SNew(SUniformGridPanel).SlotPadding(2.0f)

			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("SetClosed", "Set Current Pose As Closed"))
				.ToolTipText(LOCTEXT("SetClosedTip", "Captures where every assigned mesh is right now as the closed configuration. Refuses while a preview is showing a non-closed pose."))
				.OnClicked(this, &FArchOpeningComponentDetails::OnSetClosedPose)
			]

			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("ResetClosed", "Reset To Closed Pose"))
				.OnClicked(this, &FArchOpeningComponentDetails::OnResetToClosed)
			]

			+ SUniformGridPanel::Slot(2, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("Reanchor", "Re-anchor Parts To Opening"))
				.OnClicked(this, &FArchOpeningComponentDetails::OnReanchor)
			]

			+ SUniformGridPanel::Slot(0, 1)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("SnapHinge", "Snap Hinge To Leaf Edge"))
				.OnClicked(this, &FArchOpeningComponentDetails::OnSnapHinge)
			]

			+ SUniformGridPanel::Slot(1, 1)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("SnapSlide", "Fit Slide To Leaf Width"))
				.OnClicked(this, &FArchOpeningComponentDetails::OnSnapSlide)
			]
		];

	Commands.AddCustomRow(LOCTEXT("PreviewRowFilter", "Preview"))
		.WholeRowContent()
		[
			SNew(SUniformGridPanel).SlotPadding(2.0f)

			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("PreviewToggle", "Preview Toggle"))
				.ToolTipText(LOCTEXT("PreviewToggleTip", "Animates the opening in the editor without entering Play In Editor. Preview never rewrites the captured closed pose."))
				.OnClicked(this, &FArchOpeningComponentDetails::OnPreviewToggle)
			]

			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("StopPreview", "Stop Preview"))
				.OnClicked(this, &FArchOpeningComponentDetails::OnStopPreview)
			]

			+ SUniformGridPanel::Slot(2, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("RestorePreview", "Restore Pre-Preview Pose"))
				.OnClicked(this, &FArchOpeningComponentDetails::OnRestorePrePreview)
			]
		];

	Commands.AddCustomRow(LOCTEXT("ValidationRowFilter", "Validation"))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(this, &FArchOpeningComponentDetails::GetValidationSummary)
		];
}

FText FArchOpeningComponentDetails::GetValidationSummary() const
{
	const UArchOpeningComponent* Opening = GetOpening();
	if (Opening == nullptr)
	{
		return FText::GetEmpty();
	}

	const FArchOpeningValidationReport Report = Opening->Validate();

	FString Detail;
	for (const FArchOpeningIssue& Issue : Report.Issues)
	{
		if (Issue.Severity == EArchOpeningIssueSeverity::Info)
		{
			continue;
		}

		Detail += TEXT("\n  - ");
		Detail += Issue.Message.ToString();
	}

	return FText::Format(
		LOCTEXT("SummaryFmt", "{0}{1}"),
		UArchOpeningFunctionLibrary::SummarizeValidation(Report),
		FText::FromString(Detail));
}

FReply FArchOpeningComponentDetails::OnSetClosedPose()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetClosedTransaction", "Set Current Pose As Closed"));

		Opening->ForEachPart([](const FArchOpeningPartRef& Part, EArchOpeningPartRole, int32)
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

		Opening->SetCurrentPoseAsClosed();
	}

	return FReply::Handled();
}

FReply FArchOpeningComponentDetails::OnResetToClosed()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		// Transient pose only, so no transaction and no map dirtying.
		Opening->ResetToClosedPose();
	}
	return FReply::Handled();
}

FReply FArchOpeningComponentDetails::OnRestorePrePreview()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		if (FArchitecturalOpeningsEditorModule::IsAvailable())
		{
			FArchitecturalOpeningsEditorModule::Get().GetPreviewManager()->RestoreAndStopTracking(Opening);
		}
		else
		{
			Opening->RestorePrePreviewPose();
		}
	}
	return FReply::Handled();
}

FReply FArchOpeningComponentDetails::OnPreviewToggle()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		if (FArchitecturalOpeningsEditorModule::IsAvailable())
		{
			FArchitecturalOpeningsEditorModule::Get().GetPreviewManager()->Track(Opening);
		}
		Opening->PreviewToggle();
	}
	return FReply::Handled();
}

FReply FArchOpeningComponentDetails::OnStopPreview()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		Opening->StopPreview();
	}
	return FReply::Handled();
}

FReply FArchOpeningComponentDetails::OnSnapHinge()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		const FScopedTransaction Transaction(LOCTEXT("SnapHingeTransaction", "Snap Hinge To Leaf Edge"));
		Opening->SnapHingeToLeafEdge();
	}
	return FReply::Handled();
}

FReply FArchOpeningComponentDetails::OnSnapSlide()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		const FScopedTransaction Transaction(LOCTEXT("SnapSlideTransaction", "Fit Slide To Leaf Width"));
		Opening->SnapSlideToLeafBounds();
	}
	return FReply::Handled();
}

FReply FArchOpeningComponentDetails::OnReanchor()
{
	if (UArchOpeningComponent* Opening = GetOpening())
	{
		const FScopedTransaction Transaction(LOCTEXT("ReanchorTransaction", "Re-anchor Opening Parts"));
		Opening->TransportPartsWithFrameChange();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
