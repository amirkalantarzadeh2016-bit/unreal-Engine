// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchOpeningPieceSetComponent.h"
#include "EditorSubsystem.h"

#include "ArchOpeningExtractionSubsystem.generated.h"

class UArchOpeningExtractionProfile;
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
	/** Lowest triangle id in the piece. Stable across re-analysis; see FArchOpeningPieceRecord. */
	int32 Key = INDEX_NONE;

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

/** One group's share of an extraction. */
struct FArchOpeningGroupExtractionRequest
{
	/** Index of the group in the piece set, echoed back on the output so results map back exactly. */
	int32 SourceGroupIndex = INDEX_NONE;

	FName GroupName;
	EArchOpeningGroupRole Role = EArchOpeningGroupRole::Movable;

	/** Indices into FArchOpeningMeshAnalysis::Pieces. */
	TArray<int32> PieceIndices;

	/** When set and still loadable, this asset is rewritten instead of a new one being created. */
	TSoftObjectPtr<UStaticMesh> ExistingAsset;
};

/** What one group produced. */
struct FArchOpeningGroupExtractionOutput
{
	/** The requesting group's index. Matching results back by name would break on a rename. */
	int32 SourceGroupIndex = INDEX_NONE;

	FName GroupName;
	EArchOpeningGroupRole Role = EArchOpeningGroupRole::Movable;
	TWeakObjectPtr<UStaticMesh> Mesh;
	int32 TriangleCount = 0;
	bool bUpdatedInPlace = false;
};

/** What an extraction produced overall. */
struct FArchOpeningExtractionResult
{
	TArray<FArchOpeningGroupExtractionOutput> Groups;

	/** Things the artist must know about the output, e.g. that only LOD0 was carried over. */
	TArray<FText> Notes;
};

/**
 * Editor-only assisted extraction of movable leaves out of a one-mesh source asset.
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
 * Grouping
 * --------
 * Pieces are classified into any number of groups, not a single leaf/fixed pair. A double door is
 * a stationary group plus two movable groups; a folding door is as many movable groups as it has
 * panels. Every non-empty group becomes exactly one asset.
 *
 * Re-editability
 * --------------
 * A group remembers the asset it generated. Extracting again rewrites those same assets rather
 * than creating a second set, so correcting one misassigned piece does not mean redoing the setup
 * or re-pointing the actors already placed in the level. The classification itself persists in an
 * extraction profile asset next to the source mesh.
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
	 * Fills a piece-set component from an analysis, creating the default groups and putting every
	 * piece in the stationary bucket. Existing assignments are not preserved; apply a profile after.
	 */
	void PopulatePieceSet(UArchOpeningPieceSetComponent* PieceSet, const FArchOpeningMeshAnalysis& Analysis,
		UStaticMesh* SourceMesh, UStaticMeshComponent* SourceComponent) const;

	/**
	 * Writes one new static mesh per non-empty group.
	 *
	 * The source asset is never modified. Groups whose ExistingAsset still loads are rewritten in
	 * place, so actors already using them update without re-assignment; everything else is created
	 * under PackagePath with a unique name. Nothing is saved to disk here: packages are marked
	 * dirty and the artist saves them.
	 *
	 * @return false with OutError filled when no group has any pieces, or the path is not valid.
	 */
	bool ExtractGroups(
		UStaticMesh* SourceMesh,
		const FArchOpeningMeshAnalysis& Analysis,
		const TArray<FArchOpeningGroupExtractionRequest>& Requests,
		const FString& PackagePath,
		const FString& BaseAssetName,
		EArchOpeningExtractionCollision CollisionOption,
		bool bUpdateExistingAssets,
		FArchOpeningExtractionResult& OutResult,
		FText& OutError) const;

	// ----------------------------------------------------------------------------------------
	// Profiles
	// ----------------------------------------------------------------------------------------

	/** "/Game/Foo/SM_Door" -> "/Game/Foo/SM_Door_OpeningProfile". Empty on a bad input. */
	static FString MakeProfilePackageName(const UStaticMesh* SourceMesh);

	/** Loads the profile that sits next to a source mesh, or null if there is none. */
	UArchOpeningExtractionProfile* FindProfileFor(const UStaticMesh* SourceMesh) const;

	/**
	 * Copies a piece-set's groups and assignments into its profile, creating the asset if needed.
	 * The package is marked dirty, not saved.
	 */
	UArchOpeningExtractionProfile* SaveProfile(const UArchOpeningPieceSetComponent* PieceSet, FText& OutError) const;

	/**
	 * Applies a saved profile's groups and assignments onto a freshly analysed piece set.
	 *
	 * Matching is by piece key, so a re-analysis that produces the pieces in a different order
	 * still restores the right assignments. Pieces the profile does not know about stay stationary
	 * and are counted in OutUnmatched, which is how a changed source mesh announces itself.
	 */
	bool ApplyProfileTo(UArchOpeningPieceSetComponent* PieceSet, const UArchOpeningExtractionProfile* Profile,
		int32& OutMatched, int32& OutUnmatched) const;

	/** Human readable one-liner describing a piece, for the selection list. */
	static FText DescribePiece(const FArchOpeningPieceRecord& Piece, int32 PieceIndex);
};
