// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "ArchOpeningPieceSetComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EArchOpeningGroupRole : uint8
{
	/** Never animates. The frame, fixed glazing, fixed profiles. */
	Stationary	UMETA(DisplayName = "Stationary"),
	/** Becomes one movable leaf. Each movable group extracts to its own asset. */
	Movable		UMETA(DisplayName = "Movable leaf")
};

/**
 * One classification bucket for the pieces of a source mesh.
 *
 * A double door is three groups: the stationary frame plus two movable leaves. A folding door is
 * as many movable groups as there are panels. Every non-empty group extracts to exactly one asset.
 */
USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningExtractionGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	FName GroupName = TEXT("Group");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	EArchOpeningGroupRole Role = EArchOpeningGroupRole::Movable;

	/** Colour this group's pieces are drawn in, in the viewport and in the piece list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	FLinearColor DisplayColor = FLinearColor(0.15f, 0.85f, 0.35f);

	/**
	 * Asset previously generated for this group.
	 *
	 * This is what makes re-extraction non-destructive: when it points at an existing mesh, the
	 * next extraction rewrites that same asset instead of creating a second one, so every actor
	 * already placed in the level picks up the correction without being re-assigned.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	TSoftObjectPtr<UStaticMesh> GeneratedMesh;
};

/**
 * One connected piece of the source mesh, with the group it has been assigned to.
 */
USTRUCT(BlueprintType)
struct ARCHITECTURALOPENINGS_API FArchOpeningPieceRecord
{
	GENERATED_BODY()

	/**
	 * Stable identity of the piece: the lowest triangle id it contains.
	 *
	 * Piece ORDER is not stable (pieces are sorted for display, and ties are arbitrary), so an
	 * index would silently re-point assignments at the wrong geometry after a re-analysis. Triangle
	 * ids come from the source mesh description and do not move, so the lowest one in a connected
	 * set identifies that set for as long as the source asset is unchanged.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	int32 Key = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	int32 TriangleCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	int32 VertexCount = 0;

	/** Bounds in the source mesh's local space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	FBox LocalBounds = FBox(ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	TArray<FName> MaterialSlots;

	/** Index into UArchOpeningPieceSetComponent::Groups. Never negative once analysis has run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Piece")
	int32 GroupIndex = 0;
};

/**
 * Live state of a leaf-extraction session, and the thing the viewport draws and hit-tests against.
 *
 * It is created on a transient, editor-only actor for the duration of a session, so the artist's
 * own actors are never modified and nothing about the session can be saved into a level by
 * accident. Persistence across sessions is the extraction profile asset's job, not this
 * component's.
 *
 * The component exists at all because a component visualizer is what gives the tool a live,
 * per-frame viewport drawing and, through hit proxies, click-to-assign directly in the 3D view.
 */
UCLASS(ClassGroup = (ArchitecturalOpenings), NotBlueprintable,
	meta = (DisplayName = "Architectural Opening Piece Set"))
class ARCHITECTURALOPENINGS_API UArchOpeningPieceSetComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UArchOpeningPieceSetComponent();

	/** The static mesh whose pieces these are. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Extraction")
	TObjectPtr<UStaticMesh> SourceMesh = nullptr;

	/** The placed component the session was started from, used for the world transform. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Extraction")
	TObjectPtr<UStaticMeshComponent> SourceComponent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Extraction")
	float WeldTolerance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extraction")
	TArray<FArchOpeningExtractionGroup> Groups;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Extraction")
	TArray<FArchOpeningPieceRecord> Pieces;

	/** Group that a viewport click assigns to. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Extraction")
	int32 ActiveGroupIndex = 0;

	/** Piece under the mouse, or INDEX_NONE. Purely a drawing hint. */
	UPROPERTY(Transient)
	int32 HoveredPieceIndex = INDEX_NONE;

	/** Piece indices ticked in the list, which the batch utilities operate on. */
	UPROPERTY(Transient)
	TSet<int32> SelectedPieces;

	/** Draw each piece's actual triangle edges as well as its box, up to the line budget. */
	UPROPERTY(EditAnywhere, Category = "Extraction")
	bool bDrawPieceWireframe = false;

	// ----------------------------------------------------------------------------------------
	// Groups
	// ----------------------------------------------------------------------------------------

	/** Creates the default Stationary + Movable Leaf 1 pair. */
	void InitializeDefaultGroups();

	/** Appends a movable group with the next colour from the palette. Returns its index. */
	int32 AddMovableGroup();

	/**
	 * Removes a group and moves its pieces to group 0. Group 0 itself cannot be removed: it is the
	 * stationary bucket every unassigned piece falls back to.
	 */
	bool RemoveGroup(int32 GroupIndex);

	int32 CountPiecesInGroup(int32 GroupIndex) const;

	FLinearColor GetGroupColor(int32 GroupIndex) const;

	bool IsValidGroup(int32 GroupIndex) const { return Groups.IsValidIndex(GroupIndex); }

	/** Colour used for a piece: its group's colour, brightened while hovered or selected. */
	FLinearColor GetPieceDrawColor(int32 PieceIndex) const;

	// ----------------------------------------------------------------------------------------
	// Assignment and selection
	// ----------------------------------------------------------------------------------------

	void AssignPieceToGroup(int32 PieceIndex, int32 GroupIndex);
	void AssignSelectionToGroup(int32 GroupIndex);

	void SetSelection(int32 PieceIndex);
	void ToggleSelection(int32 PieceIndex);
	void SelectAll();
	void ClearSelection();
	void InvertSelection();

	/**
	 * Selects every piece that looks like the reference one: same triangle count, and bounds whose
	 * extents match within SizeTolerance centimetres on all three axes.
	 *
	 * This is the "twelve identical mouldings" case. Triangle count alone is a weak match (two
	 * unrelated boxes both have twelve), so size has to agree too.
	 */
	int32 SelectSimilarTo(int32 ReferencePieceIndex, float SizeTolerance);

	int32 FindPieceByKey(int32 Key) const;

	/** World transform the pieces' local bounds are drawn through. */
	FTransform GetSourceWorldTransform() const;

	//~ Begin UActorComponent Interface
	virtual bool IsEditorOnly() const override { return true; }
	//~ End UActorComponent Interface
};
