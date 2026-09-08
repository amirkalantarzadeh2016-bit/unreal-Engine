// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"

#include "ArchOpeningExtractionSubsystem.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/** How collision is set up on the assets extraction writes out. */
UENUM()
enum class EArchOpeningExtractionCollision : uint8
{
	/**
	 * Per-triangle collision from the extracted geometry. Traces and clicks work immediately.
	 * Simple-collision queries (overlaps against primitives) do not.
	 */
	UseComplexAsSimple	UMETA(DisplayName = "Complex as simple (recommended)"),
	/** No collision at all. The extracted meshes will not be clickable until you add some. */
	None				UMETA(DisplayName = "None"),
	/** Keep the source mesh's collision trace flag but generate no primitives. */
	CopySourceFlag		UMETA(DisplayName = "Copy source flag only")
};

/** One connected piece of geometry found inside a source static mesh. */
struct FArchOpeningMeshPiece
{
	int32 TriangleCount = 0;
	int32 VertexCount = 0;
	FBox LocalBounds = FBox(ForceInit);

	/** Material slot names this piece uses. Several pieces commonly share one slot. */
	TArray<FName> MaterialSlots;

	/** Raw FTriangleID values belonging to this piece. */
	TArray<int32> TriangleIds;
};

/** Result of analysing a source mesh for disconnected pieces. */
struct FArchOpeningMeshAnalysis
{
	TWeakObjectPtr<UStaticMesh> SourceMesh;
	TArray<FArchOpeningMeshPiece> Pieces;

	int32 TotalTriangles = 0;
	int32 NumUVChannels = 1;
	float WeldTolerance = 0.0f;
	bool bValid = false;

	/** True when the whole mesh came back as a single connected piece. */
	bool IsSinglePiece() const { return bValid && Pieces.Num() <= 1; }
};

/** What ExtractPieces produced. */
struct FArchOpeningExtractionResult
{
	TWeakObjectPtr<UStaticMesh> ExtractedMesh;
	TWeakObjectPtr<UStaticMesh> RemainderMesh;

	int32 ExtractedTriangles = 0;
	int32 RemainderTriangles = 0;

	/** Things the artist must know about the output, e.g. that only LOD0 was carried over. */
	TArray<FText> Notes;
};

/**
 * Editor-only assisted extraction of a movable leaf out of a one-mesh source asset.
 *
 * Method, stated exactly so the results are predictable
 * ----------------------------------------------------
 * Pieces are found by union-find over triangle adjacency in the source mesh description. Two
 * triangles are in the same piece when they share a vertex.
 *
 * A mesh description already shares one vertex between the several *vertex instances* a hard edge
 * or a UV seam produces, so split normals and split UVs do NOT break a piece apart. What does break
 * a piece apart is duplicated *vertices* at the same position, which some exporters emit. The weld
 * tolerance exists for exactly that case: with a non-zero tolerance, vertices within that distance
 * of each other are unioned before adjacency is walked.
 *
 * The tolerance is a trade-off and the plugin will not guess it: too small and one physical part
 * arrives as several pieces; too large and a leaf resting flush against its frame is welded to it
 * and the two can no longer be separated. It defaults to 0 (pure topology) for that reason.
 *
 * Material sections are offered as an additional selection aid, never as the definition of a part:
 * one PVC or glass material routinely spans both the fixed frame and the movable leaf, so a
 * material section is not by itself a physical part.
 *
 * What this cannot do
 * -------------------
 * If the frame and the leaf are welded into one connected piece, no selection of whole pieces can
 * separate them. The tool reports that plainly and stops. It never cuts arbitrary geometry, and it
 * never claims a separation it did not achieve.
 */
UCLASS()
class ARCHITECTURALOPENINGSEDITOR_API UArchOpeningExtractionSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Finds the disconnected pieces of a static mesh's LOD0.
	 * @return false with OutError filled when the source cannot be analysed.
	 */
	bool AnalyzeMesh(UStaticMesh* SourceMesh, float WeldTolerance, FArchOpeningMeshAnalysis& OutAnalysis, FText& OutError) const;

	/**
	 * Writes the selected pieces out as one new static mesh and the remaining pieces as another.
	 *
	 * The source asset is never modified. Both outputs are created under PackagePath with unique
	 * names. Nothing is saved to disk here: the packages are marked dirty and the artist saves them.
	 *
	 * @return false with OutError filled when the selection is empty, covers everything, or the
	 *         output path is not a valid content path.
	 */
	bool ExtractPieces(
		UStaticMesh* SourceMesh,
		const FArchOpeningMeshAnalysis& Analysis,
		const TArray<int32>& SelectedPieceIndices,
		const FString& PackagePath,
		const FString& BaseAssetName,
		EArchOpeningExtractionCollision CollisionOption,
		FArchOpeningExtractionResult& OutResult,
		FText& OutError) const;

	/** Human readable one-liner describing a piece, for the selection list. */
	static FText DescribePiece(const FArchOpeningMeshAnalysis& Analysis, int32 PieceIndex);
};
