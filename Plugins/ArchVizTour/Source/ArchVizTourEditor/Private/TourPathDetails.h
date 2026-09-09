// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "TourTypes.h"

class ATourPath;
class IDetailLayoutBuilder;

/**
 * Details panel customization for ATourPath.
 *
 * Adds a generation panel above the raw properties and a preset save/load row, so the common
 * authoring loop - pick a shape, generate, tweak, save - is one panel rather than a hunt
 * through the CallInEditor buttons scattered across the default layout.
 */
class FTourPathDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	// --- IDetailCustomization --------------------------------------------
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Run the generator on every selected path. */
	FReply OnGenerateClicked();

	/** Resample every selected path to uniform spacing. */
	FReply OnResampleClicked();

	/** Smooth every selected path's tangents. */
	FReply OnSmoothClicked();

	/** Save every selected path into its linked preset. */
	FReply OnSaveToPresetClicked();

	/** Load every selected path from its linked preset. */
	FReply OnLoadFromPresetClicked();

	/** Export the first selected path to a JSON file chosen by the user. */
	FReply OnExportJsonClicked();

	/** Import the first selected path from a JSON file chosen by the user. */
	FReply OnImportJsonClicked();

	/** Total length of the first selected path, for the read-out row. */
	FText GetPathLengthText() const;

	/** Estimated traversal time of the first selected path at its authored speed. */
	FText GetPathDurationText() const;

	/** Paths currently being customised. Weak, because selection changes under the panel. */
	TArray<TWeakObjectPtr<ATourPath>> SelectedPaths;
};
