// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "TourTypes.h"

#include "TourAuthoringWidgetBase.generated.h"

class ATourPath;
class UTourSequencePreset;
class UTourSubsystem;

/**
 * Base class for an in-game tour authoring widget.
 *
 * Optional: the playback path never touches it. It exists so a tour can be built, adjusted
 * and persisted on site in a packaged client, where no editor is available and no asset can
 * be created - everything here writes into a save slot instead.
 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "Tour Authoring Widget Base"))
class ARCHVIZTOURRUNTIME_API UTourAuthoringWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Path being edited. Set it before calling any point operation.
	 * A null target makes every operation a logged no-op rather than a crash.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "ArchViz Tour|Authoring")
	TObjectPtr<ATourPath> TargetPath;

	/**
	 * Append a point at the player's current view, capturing its lens settings too.
	 *
	 * @param bUseExplicitRotation  Store the view rotation on the point. On by default, because
	 *                              a hand-placed viewpoint is authored precisely to be looked
	 *                              through, and letting it fall back to the tangent discards
	 *                              exactly the thing the user just framed.
	 * @return Index of the new point, or INDEX_NONE on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	int32 AddPointFromCurrentView(bool bUseExplicitRotation = true);

	/**
	 * Insert a point at an explicit world transform.
	 * @param InsertIndex  Where to insert; negative or out of range appends.
	 * @return Index of the new point, or INDEX_NONE on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	int32 InsertPoint(const FTransform& WorldTransform, int32 InsertIndex = -1);

	/** Remove a point. @return true when the index was valid. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool DeletePoint(int32 PointIndex);

	/** Move a point within the path. @return true when both indices were valid. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool ReorderPoint(int32 FromIndex, int32 ToIndex);

	/** Copy a point, inserting the copy directly after it. @return the copy's index, or INDEX_NONE. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	int32 DuplicatePoint(int32 PointIndex);

	/** Overwrite a point's camera settings from the player's current view. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool UpdatePointFromCurrentView(int32 PointIndex);

	/** Read a point back for display in the UI. @return false when the index is out of range. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool GetPoint(int32 PointIndex, FTourPoint& OutPoint) const;

	/** Write a point back after the UI has edited it. @return false when the index is out of range. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool SetPoint(int32 PointIndex, const FTourPoint& InPoint);

	/** Number of points on the target path. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Authoring")
	int32 GetPointCount() const;

	// ---------------------------------------------------------------------
	// Preset and slot management
	// ---------------------------------------------------------------------

	/**
	 * Copy a tour so it can be edited without touching the original.
	 * @param Source  Tour to copy. Null is rejected.
	 * @return A transient copy owned by this widget, or null on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	UTourSequencePreset* DuplicatePreset(const UTourSequencePreset* Source);

	/**
	 * Build a tour from the target path: one SplineMove step covering the whole spline.
	 * The quickest way from "I placed some points" to "I can press play".
	 * @param PathName  Tag published on the path so the step can resolve it.
	 */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	UTourSequencePreset* CreatePresetFromTargetPath(FName PathName, const FText& TourTitle);

	/** Write the currently loaded tour, and the target path's geometry, to a save slot. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool SaveCurrentTourToSlot(const FString& SlotName);

	/** Replace the loaded tour from a save slot, rebuilding its runtime paths. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool LoadTourFromSlot(const FString& SlotName);

	/** Every tour slot on disk. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	TArray<FString> EnumerateSlots() const;

	/** Delete a tour slot. */
	UFUNCTION(BlueprintCallable, Category = "ArchViz Tour|Authoring")
	bool DeleteSlot(const FString& SlotName);

	/** Fired whenever a point operation changed the target path, so the list can refresh. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ArchViz Tour|Authoring")
	void OnPathEdited(int32 PointCount);

protected:
	/** The subsystem for this widget's world. Null outside a world. */
	UFUNCTION(BlueprintPure, Category = "ArchViz Tour|Authoring")
	UTourSubsystem* GetTourSubsystem() const;

	/** Player's current view point and lens, or false when there is no player camera. */
	bool GetCurrentViewState(FTourCameraState& OutState) const;

	/** True with a warning logged when TargetPath is not set. */
	bool WarnIfNoTargetPath(const TCHAR* Operation) const;

	/** Push the edited points into the spline and notify the UI. */
	void CommitPathEdit();
};
