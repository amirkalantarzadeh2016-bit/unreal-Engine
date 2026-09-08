#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Widgets/SBoxPanel.h"
#include "UObject/WeakObjectPtr.h"

class AMinimapBoundsVolume;
class FDeferredCleanupSlateBrush;
class IDetailLayoutBuilder;
class UTexture;

/**
 * Adds a "Minimap Preview" category to the bounds volume showing BOTH background sources
 * side by side - the authored static texture and the generated render target - regardless
 * of which one is currently active, so they can be compared without switching modes.
 *
 * Previewing never mutates configuration: the capture button routes through
 * AMinimapBoundsVolume::CapturePreviewNow, which renders without changing Background
 * Source and without touching StaticMapTexture.
 */
class FMinimapBoundsVolumeDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Build a preview row for one texture, with a live status line underneath. */
	void AddPreviewRow(
		IDetailLayoutBuilder& DetailBuilder,
		class IDetailCategoryBuilder& Category,
		const FText& Title,
		bool bIsStaticSlot,
		TFunction<UTexture*()> TextureGetter,
		TFunction<FText()> StatusGetter,
		TFunction<FText()> ActiveLabelGetter);

	AMinimapBoundsVolume* GetVolume() const { return CustomizedVolume.Get(); }

	/** Single definition of a plugin action button, so every one looks identical. */
	static TSharedRef<SWidget> MakeActionButton(
		const FText& Label, const FText& Tooltip, FOnClicked OnClicked);

	/** Labelled divider that groups the action bars into sections. */
	static void AddSectionHeading(class IDetailCategoryBuilder& Category, const FText& Heading);

	FReply OnCaptureClicked();
	FReply OnBakeStaticClicked();
	FReply OnFitGeometryClicked();
	FReply OnFitActorsClicked();
	FReply OnValidateClicked();

	/** Rebuild a brush when the underlying texture changes identity. */
	static const FSlateBrush* ResolveBrush(
		TSharedPtr<FDeferredCleanupSlateBrush>& BrushSlot,
		TWeakObjectPtr<UTexture>& CachedTexture,
		UTexture* CurrentTexture,
		float PreviewSize);

	TWeakObjectPtr<AMinimapBoundsVolume> CustomizedVolume;

	/** Brushes are held alive here; FDeferredCleanupSlateBrush handles render-thread safety. */
	TSharedPtr<FDeferredCleanupSlateBrush> StaticPreviewBrush;
	TSharedPtr<FDeferredCleanupSlateBrush> CapturePreviewBrush;

	TWeakObjectPtr<UTexture> CachedStaticTexture;
	TWeakObjectPtr<UTexture> CachedCaptureTexture;

	/** Preview edge length in Slate units. */
	float PreviewSize = 256.0f;
};
