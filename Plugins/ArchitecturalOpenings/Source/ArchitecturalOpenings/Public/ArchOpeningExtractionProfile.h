// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ArchOpeningPieceSetComponent.h"

#include "ArchOpeningExtractionProfile.generated.h"

class UStaticMesh;

/**
 * Persisted classification of one source mesh: which connected piece belongs to which group.
 *
 * This is what makes extraction re-editable rather than one-way. Nothing about the decomposition
 * itself is stored - re-running the analysis with the same mesh and the same weld tolerance always
 * produces the same pieces - only the artist's decisions, keyed by a piece identity that survives
 * a re-analysis (see FArchOpeningPieceRecord::Key).
 *
 * Each group also remembers the asset it generated, so a second extraction rewrites those same
 * assets and every actor already placed in the level picks up the correction with no re-assignment.
 *
 * Profiles live next to the source mesh as "<SourceMeshName>_OpeningProfile" and are found again
 * automatically when the tool is pointed at that mesh.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Architectural Opening Extraction Profile"))
class ARCHITECTURALOPENINGS_API UArchOpeningExtractionProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The mesh this classification describes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Profile")
	TSoftObjectPtr<UStaticMesh> SourceMesh;

	/** Tolerance the pieces were found with. Re-analysing with a different one invalidates keys. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Profile")
	float WeldTolerance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Profile")
	TArray<FArchOpeningExtractionGroup> Groups;

	/** Piece key (lowest triangle id) to group index. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Profile")
	TMap<int32, int32> PieceKeyToGroup;

	/** Number of pieces present when this profile was written, used to detect a changed source. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Profile")
	int32 PieceCountAtSave = 0;

	/** The standard suffix appended to a source mesh's name to find or create its profile. */
	static const TCHAR* GetProfileSuffix() { return TEXT("_OpeningProfile"); }
};
