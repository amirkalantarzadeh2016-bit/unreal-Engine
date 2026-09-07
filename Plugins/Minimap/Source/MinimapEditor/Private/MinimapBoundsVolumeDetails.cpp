#include "MinimapBoundsVolumeDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MinimapBoundsVolume.h"
#include "MinimapCaptureComponent.h"
#include "MinimapCaptureTypes.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MinimapBoundsVolumeDetails"

TSharedRef<IDetailCustomization> FMinimapBoundsVolumeDetails::MakeInstance()
{
	return MakeShared<FMinimapBoundsVolumeDetails>();
}

const FSlateBrush* FMinimapBoundsVolumeDetails::ResolveBrush(
	TSharedPtr<FDeferredCleanupSlateBrush>& BrushSlot,
	TWeakObjectPtr<UTexture>& CachedTexture,
	UTexture* CurrentTexture,
	float PreviewSize)
{
	if (!CurrentTexture)
	{
		BrushSlot.Reset();
		CachedTexture.Reset();
		return nullptr;
	}

	// Only rebuild when the texture OBJECT changes. A render target keeps the same object
	// across captures and updates its contents on the GPU, so the existing brush keeps
	// showing fresh pixels for free - rebuilding every tick would be pure waste.
	if (!BrushSlot.IsValid() || CachedTexture.Get() != CurrentTexture)
	{
		// Cast explicitly: CreateBrush is overloaded on (UTexture*, FLinearColor, ...) and
		// (UObject*, FVector2D, ...), and we want the size-taking one unambiguously.
		BrushSlot = FDeferredCleanupSlateBrush::CreateBrush(
			static_cast<UObject*>(CurrentTexture), FVector2D(PreviewSize, PreviewSize));
		CachedTexture = CurrentTexture;
	}

	return BrushSlot.IsValid() ? BrushSlot->GetSlateBrush() : nullptr;
}

void FMinimapBoundsVolumeDetails::AddPreviewRow(
	IDetailLayoutBuilder& DetailBuilder,
	IDetailCategoryBuilder& Category,
	const FText& Title,
	bool bIsStaticSlot,
	TFunction<UTexture*()> TextureGetter,
	TFunction<FText()> StatusGetter,
	TFunction<FText()> ActiveLabelGetter)
{
	// Capture `this`, not a TSharedRef: the customization is owned by the details view and
	// outlives these rows, whereas capturing SharedThis would create a reference cycle.
	// Which brush slot to use is passed explicitly rather than inferred from the title.

	Category.AddCustomRow(Title)
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		// --- Title + which source is live ---------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([ActiveLabelGetter]() { return ActiveLabelGetter(); })
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.8f, 0.35f)))
			]
		]

		// --- The image itself ---------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SBox)
			.WidthOverride(PreviewSize)
			.HeightOverride(PreviewSize)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					// A null brush renders nothing, so the status text below is what tells
					// the user whether the source is missing, uncaptured or stale.
					SNew(SImage)
					.Image_Lambda([this, TextureGetter, bIsStaticSlot]() -> const FSlateBrush*
					{
						return ResolveBrush(
							bIsStaticSlot ? StaticPreviewBrush  : CapturePreviewBrush,
							bIsStaticSlot ? CachedStaticTexture : CachedCaptureTexture,
							TextureGetter(),
							PreviewSize);
					})
				]
			]
		]

		// --- Status ---------------------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([StatusGetter]() { return StatusGetter(); })
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.AutoWrapText(true)
		]
	];
}

void FMinimapBoundsVolumeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	// Multi-select would make "the" preview ambiguous, so only customize a single volume.
	if (Objects.Num() != 1)
	{
		return;
	}

	CustomizedVolume = Cast<AMinimapBoundsVolume>(Objects[0].Get());
	if (!CustomizedVolume.IsValid())
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Minimap Preview"),
		LOCTEXT("PreviewCategory", "Minimap Preview"),
		ECategoryPriority::Important);

	TWeakObjectPtr<AMinimapBoundsVolume> WeakVolume = CustomizedVolume;

	// --- Buttons -----------------------------------------------------------
	Category.AddCustomRow(LOCTEXT("PreviewActions", "Preview Actions"))
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 4.0f, 6.0f, 4.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CaptureNow", "Capture / Refresh Now"))
			.ToolTipText(LOCTEXT("CaptureNowTip",
				"Render the scene capture and update the preview below.\n"
				"Works in the editor without entering PIE, and does NOT change Background Source "
				"or overwrite the configured static texture."))
			.OnClicked(this, &FMinimapBoundsVolumeDetails::OnCaptureClicked)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 4.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ValidateNow", "Validate Setup"))
			.ToolTipText(LOCTEXT("ValidateNowTip",
				"Run the full setup validation and write the pipeline report to LogMinimap."))
			.OnClicked(this, &FMinimapBoundsVolumeDetails::OnValidateClicked)
		]
	];

	// --- Static preview ----------------------------------------------------
	AddPreviewRow(
		DetailBuilder, Category,
		LOCTEXT("StaticTitle", "Static Map Texture"),
		/*bIsStaticSlot=*/true,
		[WeakVolume]() -> UTexture*
		{
			AMinimapBoundsVolume* Volume = WeakVolume.Get();
			return Volume ? Cast<UTexture>(Volume->StaticMapTexture) : nullptr;
		},
		[WeakVolume]() -> FText
		{
			AMinimapBoundsVolume* Volume = WeakVolume.Get();
			if (!Volume)
			{
				return LOCTEXT("NoVolume", "No bounds volume.");
			}
			if (!Volume->StaticMapTexture)
			{
				return LOCTEXT("NoStatic",
					"No Static Map Texture assigned. The widget will keep whatever image "
					"M_Minimap already samples - assign one here to preview and manage it.");
			}
			return FText::FromString(FString::Printf(TEXT("%s"), *Volume->StaticMapTexture->GetName()));
		},
		[WeakVolume]() -> FText
		{
			AMinimapBoundsVolume* Volume = WeakVolume.Get();
			const bool bActive = Volume &&
				Volume->GetActiveBackgroundSource() == EMinimapBackgroundSource::StaticTexture;
			return bActive ? LOCTEXT("ActiveNow", "[ACTIVE]") : FText::GetEmpty();
		});

	// --- Capture preview ---------------------------------------------------
	AddPreviewRow(
		DetailBuilder, Category,
		LOCTEXT("CaptureTitle", "Scene Capture Render Target"),
		/*bIsStaticSlot=*/false,
		[WeakVolume]() -> UTexture*
		{
			AMinimapBoundsVolume* Volume = WeakVolume.Get();
			return Volume ? Cast<UTexture>(Volume->GetPreviewRenderTarget()) : nullptr;
		},
		[WeakVolume]() -> FText
		{
			AMinimapBoundsVolume* Volume = WeakVolume.Get();
			return Volume ? FText::FromString(Volume->GetPreviewStatusText())
			              : LOCTEXT("NoVolume2", "No bounds volume.");
		},
		[WeakVolume]() -> FText
		{
			AMinimapBoundsVolume* Volume = WeakVolume.Get();
			const bool bActive = Volume &&
				Volume->GetActiveBackgroundSource() == EMinimapBackgroundSource::SceneCapture;
			return bActive ? LOCTEXT("ActiveNow2", "[ACTIVE]") : FText::GetEmpty();
		});
}

FReply FMinimapBoundsVolumeDetails::OnCaptureClicked()
{
	if (AMinimapBoundsVolume* Volume = GetVolume())
	{
		// CapturePreviewNow is deliberately non-destructive: it renders even in Static
		// Texture mode without changing the configured source.
		Volume->CapturePreviewNow();

		// The render target object identity does not change between captures, so the
		// existing brush already points at the refreshed pixels; nothing to invalidate.
	}
	return FReply::Handled();
}

FReply FMinimapBoundsVolumeDetails::OnValidateClicked()
{
	if (AMinimapBoundsVolume* Volume = GetVolume())
	{
		Volume->ValidateMinimapSetup();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
