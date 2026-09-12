// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningExtractionSubsystem.h"

#include "ArchOpeningExtractionProfile.h"
#include "ArchOpeningLog.h"
#include "ArchOpeningPieceSetComponent.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSourceData.h"
#include "IAssetTools.h"
#include "MeshDescription.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "ArchOpeningExtraction"

namespace ArchOpeningExtraction
{
	/** Classic union-find over vertex indices. */
	class FDisjointSet
	{
	public:
		explicit FDisjointSet(int32 Count)
		{
			Parent.SetNumUninitialized(Count);
			Rank.SetNumZeroed(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];	// Path halving.
				Index = Parent[Index];
			}
			return Index;
		}

		void Union(int32 A, int32 B)
		{
			const int32 RootA = Find(A);
			const int32 RootB = Find(B);
			if (RootA == RootB)
			{
				return;
			}

			if (Rank[RootA] < Rank[RootB])
			{
				Parent[RootA] = RootB;
			}
			else if (Rank[RootA] > Rank[RootB])
			{
				Parent[RootB] = RootA;
			}
			else
			{
				Parent[RootB] = RootA;
				++Rank[RootA];
			}
		}

	private:
		TArray<int32> Parent;
		TArray<int32> Rank;
	};

	/** Spatial hash key for the optional position weld. */
	FIntVector QuantizePosition(const FVector3f& Position, float CellSize)
	{
		return FIntVector(
			FMath::FloorToInt(Position.X / CellSize),
			FMath::FloorToInt(Position.Y / CellSize),
			FMath::FloorToInt(Position.Z / CellSize));
	}
}

bool UArchOpeningExtractionSubsystem::AnalyzeMesh(UStaticMesh* SourceMesh, float WeldTolerance, FArchOpeningMeshAnalysis& OutAnalysis, FText& OutError) const
{
	using namespace ArchOpeningExtraction;

	OutAnalysis = FArchOpeningMeshAnalysis();

	if (!::IsValid(SourceMesh))
	{
		OutError = LOCTEXT("NoSourceMesh", "No source static mesh was supplied.");
		return false;
	}

	if (SourceMesh->GetNumSourceModels() == 0)
	{
		OutError = LOCTEXT("NoSourceModels", "The source mesh has no source models, so its editable geometry is unavailable. Meshes without source data (for example a cooked-only asset) cannot be analysed.");
		return false;
	}

	FMeshDescription* SourceDescription = SourceMesh->GetMeshDescription(0);
	if (SourceDescription == nullptr)
	{
		OutError = LOCTEXT("NoMeshDescription", "The source mesh has no LOD0 mesh description to read. Re-import the asset, or use a mesh that retains its source data.");
		return false;
	}

	if (SourceDescription->Triangles().Num() == 0)
	{
		OutError = LOCTEXT("EmptyMesh", "The source mesh's LOD0 contains no triangles.");
		return false;
	}

	FStaticMeshConstAttributes Attributes(*SourceDescription);
	TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TPolygonGroupAttributesConstRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

	const int32 VertexArraySize = SourceDescription->Vertices().GetArraySize();
	FDisjointSet Sets(VertexArraySize);

	// Optional position weld. This runs BEFORE adjacency so exporter-duplicated vertices at a seam
	// join up, and it is off by default because too large a tolerance silently welds a leaf to the
	// frame it sits flush against.
	if (WeldTolerance > 0.0f)
	{
		const float CellSize = FMath::Max(WeldTolerance, UE_KINDA_SMALL_NUMBER);
		TMap<FIntVector, TArray<int32>> Cells;

		for (const FVertexID VertexID : SourceDescription->Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[VertexID];
			const FIntVector BaseCell = QuantizePosition(Position, CellSize);
			Cells.FindOrAdd(BaseCell).Add(VertexID.GetValue());
		}

		const float ToleranceSquared = WeldTolerance * WeldTolerance;

		for (const FVertexID VertexID : SourceDescription->Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[VertexID];
			const FIntVector BaseCell = QuantizePosition(Position, CellSize);

			// A neighbour within the tolerance can sit in any of the 27 surrounding cells.
			for (int32 dX = -1; dX <= 1; ++dX)
			{
				for (int32 dY = -1; dY <= 1; ++dY)
				{
					for (int32 dZ = -1; dZ <= 1; ++dZ)
					{
						const TArray<int32>* Bucket = Cells.Find(BaseCell + FIntVector(dX, dY, dZ));
						if (Bucket == nullptr)
						{
							continue;
						}

						for (int32 OtherRaw : *Bucket)
						{
							if (OtherRaw == VertexID.GetValue())
							{
								continue;
							}

							const FVector3f Other = Positions[FVertexID(OtherRaw)];
							if (FVector3f::DistSquared(Position, Other) <= ToleranceSquared)
							{
								Sets.Union(VertexID.GetValue(), OtherRaw);
							}
						}
					}
				}
			}
		}
	}

	// Adjacency: two triangles belong to the same piece when they share a vertex. Vertex instances
	// are deliberately not used here - a hard edge or a UV seam splits instances but keeps the
	// vertex shared, so shading seams do not fragment a part.
	for (const FTriangleID TriangleID : SourceDescription->Triangles().GetElementIDs())
	{
		TArrayView<const FVertexInstanceID> Instances = SourceDescription->GetTriangleVertexInstances(TriangleID);
		if (Instances.Num() < 3)
		{
			continue;
		}

		const FVertexID V0 = SourceDescription->GetVertexInstanceVertex(Instances[0]);
		const FVertexID V1 = SourceDescription->GetVertexInstanceVertex(Instances[1]);
		const FVertexID V2 = SourceDescription->GetVertexInstanceVertex(Instances[2]);

		Sets.Union(V0.GetValue(), V1.GetValue());
		Sets.Union(V1.GetValue(), V2.GetValue());
	}

	// Gather triangles by root.
	TMap<int32, int32> RootToPiece;

	for (const FTriangleID TriangleID : SourceDescription->Triangles().GetElementIDs())
	{
		TArrayView<const FVertexInstanceID> Instances = SourceDescription->GetTriangleVertexInstances(TriangleID);
		if (Instances.Num() < 3)
		{
			continue;
		}

		const int32 Root = Sets.Find(SourceDescription->GetVertexInstanceVertex(Instances[0]).GetValue());

		int32* ExistingPiece = RootToPiece.Find(Root);
		if (ExistingPiece == nullptr)
		{
			const int32 NewIndex = OutAnalysis.Pieces.AddDefaulted();
			ExistingPiece = &RootToPiece.Add(Root, NewIndex);
		}

		FArchOpeningMeshPiece& Piece = OutAnalysis.Pieces[*ExistingPiece];
		Piece.TriangleIds.Add(TriangleID.GetValue());
		++Piece.TriangleCount;

		const FPolygonGroupID GroupID = SourceDescription->GetTrianglePolygonGroup(TriangleID);
		if (GroupID != INDEX_NONE)
		{
			Piece.MaterialSlots.AddUnique(SlotNames[GroupID]);
		}

		for (const FVertexInstanceID InstanceID : Instances)
		{
			Piece.LocalBounds += FVector(Positions[SourceDescription->GetVertexInstanceVertex(InstanceID)]);
		}
	}

	// Vertex counts, and the stable key each piece is identified by from here on.
	for (FArchOpeningMeshPiece& Piece : OutAnalysis.Pieces)
	{
		TSet<int32> UniqueVertices;
		int32 LowestTriangleId = TNumericLimits<int32>::Max();

		for (int32 RawTriangleId : Piece.TriangleIds)
		{
			LowestTriangleId = FMath::Min(LowestTriangleId, RawTriangleId);

			for (const FVertexInstanceID InstanceID : SourceDescription->GetTriangleVertexInstances(FTriangleID(RawTriangleId)))
			{
				UniqueVertices.Add(SourceDescription->GetVertexInstanceVertex(InstanceID).GetValue());
			}
		}

		Piece.VertexCount = UniqueVertices.Num();

		// Display order is not stable (pieces are sorted by size, and ties are arbitrary), so saved
		// assignments key off this instead of an index. Triangle ids come from the source asset and
		// do not move, so the lowest one in a connected set names that set.
		Piece.Key = Piece.TriangleIds.IsEmpty() ? INDEX_NONE : LowestTriangleId;
	}

	// Largest first: the frame is usually the biggest piece, and the leaf the next one down.
	OutAnalysis.Pieces.Sort([](const FArchOpeningMeshPiece& A, const FArchOpeningMeshPiece& B)
	{
		return A.TriangleCount > B.TriangleCount;
	});

	OutAnalysis.SourceMesh = SourceMesh;
	OutAnalysis.TotalTriangles = SourceDescription->Triangles().Num();
	OutAnalysis.NumUVChannels = FMath::Max(1, Attributes.GetVertexInstanceUVs().GetNumChannels());
	OutAnalysis.WeldTolerance = WeldTolerance;
	OutAnalysis.bValid = true;

	if (OutAnalysis.Pieces.Num() <= 1)
	{
		OutError = LOCTEXT("SinglePiece",
			"This mesh is one single connected piece: the frame and the leaf are welded together, so no selection of whole pieces can separate them. "
			"This case is not supported and the tool will not cut the geometry for you. Separate the leaf in your modelling package (or re-export the source model with the leaf as a separate object) and re-import.");
		// Still a valid analysis, just not a usable one. The caller shows the message.
	}

	return true;
}

FText UArchOpeningExtractionSubsystem::DescribePiece(const FArchOpeningPieceRecord& Piece, int32 PieceIndex)
{
	const FVector Size = Piece.LocalBounds.IsValid ? Piece.LocalBounds.GetSize() : FVector::ZeroVector;

	FString Slots;
	for (const FName& Slot : Piece.MaterialSlots)
	{
		if (!Slots.IsEmpty())
		{
			Slots += TEXT(", ");
		}
		Slots += Slot.ToString();
	}

	return FText::Format(
		LOCTEXT("PieceFmt", "Piece {0}   {1} tris   {2} x {3} x {4} cm   [{5}]"),
		FText::AsNumber(PieceIndex),
		FText::AsNumber(Piece.TriangleCount),
		FText::AsNumber(FMath::RoundToInt(Size.X)),
		FText::AsNumber(FMath::RoundToInt(Size.Y)),
		FText::AsNumber(FMath::RoundToInt(Size.Z)),
		FText::FromString(Slots.IsEmpty() ? TEXT("no material") : Slots));
}

void UArchOpeningExtractionSubsystem::PopulatePieceSet(
	UArchOpeningPieceSetComponent* PieceSet,
	const FArchOpeningMeshAnalysis& Analysis,
	UStaticMesh* SourceMesh,
	UStaticMeshComponent* SourceComponent) const
{
	if (PieceSet == nullptr)
	{
		return;
	}

	PieceSet->SourceMesh = SourceMesh;
	PieceSet->SourceComponent = SourceComponent;
	PieceSet->WeldTolerance = Analysis.WeldTolerance;

	PieceSet->InitializeDefaultGroups();

	PieceSet->Pieces.Reset(Analysis.Pieces.Num());
	for (const FArchOpeningMeshPiece& Piece : Analysis.Pieces)
	{
		FArchOpeningPieceRecord& Record = PieceSet->Pieces.AddDefaulted_GetRef();
		Record.Key = Piece.Key;
		Record.TriangleCount = Piece.TriangleCount;
		Record.VertexCount = Piece.VertexCount;
		Record.LocalBounds = Piece.LocalBounds;
		Record.MaterialSlots = Piece.MaterialSlots;

		// Everything starts stationary, so the classification is complete from the first frame and
		// the fixed asset is simply whatever the artist never moved out of that bucket.
		Record.GroupIndex = 0;
	}

	PieceSet->ClearSelection();
	PieceSet->HoveredPieceIndex = INDEX_NONE;
}

namespace ArchOpeningExtraction
{
	/**
	 * Copies a subset of triangles into a fresh mesh description, carrying positions, normals,
	 * tangents, binormal signs, colours, every UV channel and the material slot names.
	 */
	void BuildSubsetDescription(
		const FMeshDescription& Source,
		const TSet<int32>& TriangleIdsToCopy,
		int32 NumUVChannels,
		FMeshDescription& OutDescription,
		TArray<FName>& OutUsedSlotNames)
	{
		FStaticMeshAttributes OutAttributes(OutDescription);
		OutAttributes.Register();
		OutAttributes.GetVertexInstanceUVs().SetNumChannels(NumUVChannels);

		FStaticMeshConstAttributes SourceAttributes(Source);

		TVertexAttributesConstRef<FVector3f> SourcePositions = SourceAttributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector3f> SourceNormals = SourceAttributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesConstRef<FVector3f> SourceTangents = SourceAttributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesConstRef<float> SourceBinormalSigns = SourceAttributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesConstRef<FVector4f> SourceColors = SourceAttributes.GetVertexInstanceColors();
		TVertexInstanceAttributesConstRef<FVector2f> SourceUVs = SourceAttributes.GetVertexInstanceUVs();
		TPolygonGroupAttributesConstRef<FName> SourceSlotNames = SourceAttributes.GetPolygonGroupMaterialSlotNames();

		TVertexAttributesRef<FVector3f> OutPositions = OutAttributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> OutNormals = OutAttributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> OutTangents = OutAttributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> OutBinormalSigns = OutAttributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector4f> OutColors = OutAttributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> OutUVs = OutAttributes.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> OutSlotNames = OutAttributes.GetPolygonGroupMaterialSlotNames();

		TMap<int32, FVertexID> VertexRemap;
		TMap<int32, FVertexInstanceID> InstanceRemap;
		TMap<int32, FPolygonGroupID> GroupRemap;

		const int32 SourceUVChannels = SourceUVs.GetNumChannels();

		for (int32 RawTriangleId : TriangleIdsToCopy)
		{
			const FTriangleID TriangleID(RawTriangleId);

			const FPolygonGroupID SourceGroup = Source.GetTrianglePolygonGroup(TriangleID);
			FPolygonGroupID* MappedGroup = GroupRemap.Find(SourceGroup.GetValue());
			if (MappedGroup == nullptr)
			{
				const FPolygonGroupID NewGroup = OutDescription.CreatePolygonGroup();
				const FName SlotName = SourceSlotNames[SourceGroup];
				OutSlotNames[NewGroup] = SlotName;
				OutUsedSlotNames.AddUnique(SlotName);
				MappedGroup = &GroupRemap.Add(SourceGroup.GetValue(), NewGroup);
			}

			TArrayView<const FVertexInstanceID> SourceInstances = Source.GetTriangleVertexInstances(TriangleID);
			if (SourceInstances.Num() < 3)
			{
				continue;
			}

			TArray<FVertexInstanceID, TInlineAllocator<3>> NewInstances;

			for (const FVertexInstanceID SourceInstance : SourceInstances)
			{
				FVertexInstanceID* MappedInstance = InstanceRemap.Find(SourceInstance.GetValue());
				if (MappedInstance != nullptr)
				{
					NewInstances.Add(*MappedInstance);
					continue;
				}

				const FVertexID SourceVertex = Source.GetVertexInstanceVertex(SourceInstance);

				FVertexID* MappedVertex = VertexRemap.Find(SourceVertex.GetValue());
				if (MappedVertex == nullptr)
				{
					const FVertexID NewVertex = OutDescription.CreateVertex();
					OutPositions[NewVertex] = SourcePositions[SourceVertex];
					MappedVertex = &VertexRemap.Add(SourceVertex.GetValue(), NewVertex);
				}

				const FVertexInstanceID NewInstance = OutDescription.CreateVertexInstance(*MappedVertex);

				// Normals and tangents are carried across verbatim, so the build does not have to
				// recompute them and shading matches the source exactly.
				OutNormals[NewInstance] = SourceNormals[SourceInstance];
				OutTangents[NewInstance] = SourceTangents[SourceInstance];
				OutBinormalSigns[NewInstance] = SourceBinormalSigns[SourceInstance];
				OutColors[NewInstance] = SourceColors[SourceInstance];

				for (int32 Channel = 0; Channel < NumUVChannels; ++Channel)
				{
					OutUVs.Set(NewInstance, Channel,
						Channel < SourceUVChannels ? SourceUVs.Get(SourceInstance, Channel) : FVector2f::ZeroVector);
				}

				InstanceRemap.Add(SourceInstance.GetValue(), NewInstance);
				NewInstances.Add(NewInstance);
			}

			if (NewInstances.Num() == 3)
			{
				OutDescription.CreateTriangle(*MappedGroup, NewInstances);
			}
		}
	}

	/** Applies build settings, geometry, materials and collision to a static mesh asset. */
	bool PopulateStaticMeshAsset(
		UStaticMesh* TargetMesh,
		UStaticMesh* SourceMesh,
		FMeshDescription& Description,
		const TArray<FName>& UsedSlotNames,
		EArchOpeningExtractionCollision CollisionOption,
		FText& OutError)
	{
		if (TargetMesh->GetNumSourceModels() == 0)
		{
			TargetMesh->AddSourceModel();
		}

		FStaticMeshSourceModel& TargetSourceModel = TargetMesh->GetSourceModel(0);

		// Start from the source's build settings so lightmap and UV behaviour carries over, then
		// force normals and tangents to be kept rather than rebuilt: they were copied exactly.
		if (SourceMesh->GetNumSourceModels() > 0)
		{
			TargetSourceModel.BuildSettings = SourceMesh->GetSourceModel(0).BuildSettings;
		}
		TargetSourceModel.BuildSettings.bRecomputeNormals = false;
		TargetSourceModel.BuildSettings.bRecomputeTangents = false;

		FMeshDescription* TargetDescription = TargetMesh->CreateMeshDescription(0);
		if (TargetDescription == nullptr)
		{
			OutError = LOCTEXT("DescriptionFailed", "Could not create a mesh description on the asset.");
			return false;
		}

		*TargetDescription = MoveTemp(Description);
		TargetMesh->CommitMeshDescription(0);

		// Material slots, in the order the subset actually uses them.
		TargetMesh->GetStaticMaterials().Empty(UsedSlotNames.Num());

		for (const FName& SlotName : UsedSlotNames)
		{
			UMaterialInterface* Material = nullptr;

			for (const FStaticMaterial& SourceMaterial : SourceMesh->GetStaticMaterials())
			{
				if (SourceMaterial.MaterialSlotName == SlotName)
				{
					Material = SourceMaterial.MaterialInterface;
					break;
				}
			}

			TargetMesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName, SlotName));
		}

		TargetMesh->SetLightMapCoordinateIndex(SourceMesh->GetLightMapCoordinateIndex());
		TargetMesh->SetLightMapResolution(SourceMesh->GetLightMapResolution());
		TargetMesh->SetNaniteSettings(SourceMesh->GetNaniteSettings());

		// Collision. Simple collision primitives are deliberately NOT copied: a convex hull or box
		// authored for the whole source shape would be wrong for a subset of it.
		TargetMesh->CreateBodySetup();
		if (UBodySetup* BodySetup = TargetMesh->GetBodySetup())
		{
			switch (CollisionOption)
			{
			case EArchOpeningExtractionCollision::UseComplexAsSimple:
				BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
				break;

			case EArchOpeningExtractionCollision::None:
				BodySetup->CollisionTraceFlag = CTF_UseDefault;
				BodySetup->DefaultInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);
				break;

			case EArchOpeningExtractionCollision::CopySourceFlag:
				if (const UBodySetup* SourceBodySetup = SourceMesh->GetBodySetup())
				{
					BodySetup->CollisionTraceFlag = SourceBodySetup->CollisionTraceFlag;
				}
				break;
			}
		}

		return true;
	}

	/** Creates one static mesh asset from a mesh description. Returns nullptr on failure. */
	UStaticMesh* CreateStaticMeshAsset(
		UStaticMesh* SourceMesh,
		FMeshDescription& Description,
		const TArray<FName>& UsedSlotNames,
		const FString& PackagePath,
		const FString& DesiredName,
		EArchOpeningExtractionCollision CollisionOption,
		FText& OutError)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		FString PackageName;
		FString AssetName;
		AssetTools.CreateUniqueAssetName(PackagePath / DesiredName, TEXT(""), PackageName, AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)
		{
			OutError = FText::Format(
				LOCTEXT("PackageFailedFmt", "Could not create the package '{0}'."), FText::FromString(PackageName));
			return nullptr;
		}

		UStaticMesh* NewMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
		if (NewMesh == nullptr)
		{
			OutError = LOCTEXT("MeshFailed", "Could not create the static mesh object.");
			return nullptr;
		}

		NewMesh->InitResources();
		NewMesh->SetLightingGuid();

		if (!PopulateStaticMeshAsset(NewMesh, SourceMesh, Description, UsedSlotNames, CollisionOption, OutError))
		{
			return nullptr;
		}

		NewMesh->Build(/*bInSilent*/ true);
		NewMesh->PostEditChange();

		FAssetRegistryModule::AssetCreated(NewMesh);
		Package->MarkPackageDirty();

		return NewMesh;
	}

	/**
	 * Rewrites an asset a previous extraction produced.
	 *
	 * Refuses anything with more than one LOD: the tool only ever writes single-LOD assets, so a
	 * multi-LOD asset is not one of ours and rewriting only its LOD0 would leave the others showing
	 * stale geometry. The caller falls back to creating a new asset and says so.
	 */
	UStaticMesh* UpdateStaticMeshAsset(
		UStaticMesh* TargetMesh,
		UStaticMesh* SourceMesh,
		FMeshDescription& Description,
		const TArray<FName>& UsedSlotNames,
		EArchOpeningExtractionCollision CollisionOption,
		FText& OutError)
	{
		if (TargetMesh == nullptr || TargetMesh == SourceMesh)
		{
			OutError = LOCTEXT("BadUpdateTarget", "The asset to update is missing, or is the source mesh itself.");
			return nullptr;
		}

		if (TargetMesh->GetNumSourceModels() > 1)
		{
			OutError = FText::Format(
				LOCTEXT("MultiLodUpdateFmt", "'{0}' has more than one LOD, so it was not written in place."),
				FText::FromString(TargetMesh->GetName()));
			return nullptr;
		}

		TargetMesh->Modify();
		TargetMesh->PreEditChange(nullptr);

		if (!PopulateStaticMeshAsset(TargetMesh, SourceMesh, Description, UsedSlotNames, CollisionOption, OutError))
		{
			return nullptr;
		}

		TargetMesh->Build(/*bInSilent*/ true);
		TargetMesh->PostEditChange();
		TargetMesh->MarkPackageDirty();

		return TargetMesh;
	}
}

bool UArchOpeningExtractionSubsystem::ExtractGroups(
	UStaticMesh* SourceMesh,
	const FArchOpeningMeshAnalysis& Analysis,
	const TArray<FArchOpeningGroupExtractionRequest>& Requests,
	const FString& PackagePath,
	const FString& BaseAssetName,
	EArchOpeningExtractionCollision CollisionOption,
	bool bUpdateExistingAssets,
	FArchOpeningExtractionResult& OutResult,
	FText& OutError) const
{
	using namespace ArchOpeningExtraction;

	OutResult = FArchOpeningExtractionResult();

	if (!::IsValid(SourceMesh) || !Analysis.bValid || Analysis.SourceMesh.Get() != SourceMesh)
	{
		OutError = LOCTEXT("StaleAnalysis", "The analysis does not match the supplied source mesh. Re-run Analyse.");
		return false;
	}

	if (!FPackageName::IsValidLongPackageName(PackagePath / TEXT("Probe")))
	{
		OutError = FText::Format(
			LOCTEXT("BadPathFmt", "'{0}' is not a valid content path. Use something like /Game/Openings."),
			FText::FromString(PackagePath));
		return false;
	}

	// Groups with no pieces produce no asset: an empty "Movable Leaf 2" left over from an
	// experiment must not write an empty mesh.
	TArray<const FArchOpeningGroupExtractionRequest*> PopulatedRequests;
	TArray<FName> EmptiedGroupsWithAssets;

	for (const FArchOpeningGroupExtractionRequest& Request : Requests)
	{
		if (!Request.PieceIndices.IsEmpty())
		{
			PopulatedRequests.Add(&Request);
		}
		else if (!Request.ExistingAsset.IsNull())
		{
			// A group that produced an asset before and has since been emptied: its asset is now
			// stale and nothing here will update or delete it. Say so rather than leave it lurking.
			EmptiedGroupsWithAssets.Add(Request.GroupName);
		}
	}

	if (PopulatedRequests.IsEmpty())
	{
		OutError = LOCTEXT("NoGroups", "No group has any pieces assigned to it.");
		return false;
	}

	if (PopulatedRequests.Num() == 1)
	{
		OutError = LOCTEXT("OneGroupOnly", "Every piece is in one group, so there is nothing to separate. Assign the leaf's pieces to a movable group.");
		return false;
	}

	FMeshDescription* SourceDescription = SourceMesh->GetMeshDescription(0);
	if (SourceDescription == nullptr)
	{
		OutError = LOCTEXT("NoMeshDescriptionExtract", "The source mesh no longer has a readable LOD0 mesh description.");
		return false;
	}

	const FString CleanBaseName = BaseAssetName.IsEmpty() ? SourceMesh->GetName() : BaseAssetName;

	bool bAnyFallbackToNewAsset = false;

	for (const FArchOpeningGroupExtractionRequest* Request : PopulatedRequests)
	{
		// Triangle sets are built per group from the piece decomposition, so no triangle can land
		// in two outputs: the results are exactly complementary and the retained geometry cannot
		// visually double up with an extracted leaf.
		TSet<int32> Triangles;
		for (int32 PieceIndex : Request->PieceIndices)
		{
			if (!Analysis.Pieces.IsValidIndex(PieceIndex))
			{
				continue;
			}

			for (int32 RawTriangleId : Analysis.Pieces[PieceIndex].TriangleIds)
			{
				Triangles.Add(RawTriangleId);
			}
		}

		if (Triangles.IsEmpty())
		{
			continue;
		}

		FArchOpeningGroupExtractionOutput Output;
		Output.SourceGroupIndex = Request->SourceGroupIndex;
		Output.GroupName = Request->GroupName;
		Output.Role = Request->Role;
		Output.TriangleCount = Triangles.Num();

		UStaticMesh* ResultMesh = nullptr;

		// Rewriting the asset this group produced last time is what keeps a correction cheap: every
		// actor already placed in the level keeps pointing at the same mesh and simply updates.
		if (bUpdateExistingAssets && !Request->ExistingAsset.IsNull())
		{
			if (UStaticMesh* Existing = Request->ExistingAsset.LoadSynchronous())
			{
				FMeshDescription Description;
				TArray<FName> UsedSlots;
				BuildSubsetDescription(*SourceDescription, Triangles, Analysis.NumUVChannels, Description, UsedSlots);

				FText UpdateError;
				ResultMesh = UpdateStaticMeshAsset(Existing, SourceMesh, Description, UsedSlots, CollisionOption, UpdateError);

				if (ResultMesh != nullptr)
				{
					Output.bUpdatedInPlace = true;
				}
				else
				{
					// Say why, then fall through and create a fresh asset rather than losing the work.
					OutResult.Notes.Add(UpdateError);
					bAnyFallbackToNewAsset = true;
				}
			}
		}

		if (ResultMesh == nullptr)
		{
			// A separate description: the one above was moved from if the in-place path ran.
			FMeshDescription Description;
			TArray<FName> UsedSlots;
			BuildSubsetDescription(*SourceDescription, Triangles, Analysis.NumUVChannels, Description, UsedSlots);

			const FString Suffix = (Request->Role == EArchOpeningGroupRole::Stationary)
				? TEXT("_Fixed")
				: FString::Printf(TEXT("_%s"), *Request->GroupName.ToString().Replace(TEXT(" "), TEXT("")));

			ResultMesh = CreateStaticMeshAsset(
				SourceMesh, Description, UsedSlots, PackagePath,
				CleanBaseName + Suffix, CollisionOption, OutError);
		}

		if (ResultMesh == nullptr)
		{
			OutError = FText::Format(
				LOCTEXT("GroupFailedFmt", "Group '{0}' could not be written: {1}"),
				FText::FromName(Request->GroupName), OutError);
			return false;
		}

		Output.Mesh = ResultMesh;
		OutResult.Groups.Add(Output);

		UE_LOG(LogArchOpenings, Log,
			TEXT("Extraction: group '%s' -> '%s' (%d triangles, %s)."),
			*Request->GroupName.ToString(), *ResultMesh->GetPathName(), Output.TriangleCount,
			Output.bUpdatedInPlace ? TEXT("updated in place") : TEXT("new asset"));
	}

	// Everything the artist has to know about what the output does and does not carry.
	OutResult.Notes.Add(LOCTEXT("NoteSourceUntouched",
		"The source asset was not modified in any way."));
	OutResult.Notes.Add(LOCTEXT("NoteLod0",
		"Only LOD0 was extracted. The new assets have a single LOD; any LODs the source had were not carried over. Generate LODs on the new assets if you need them."));
	OutResult.Notes.Add(LOCTEXT("NoteNormals",
		"Positions, normals, tangents, binormal signs, vertex colours and every UV channel were copied verbatim, and the build was told not to recompute normals or tangents."));
	OutResult.Notes.Add(LOCTEXT("NoteLightmap",
		"Lightmap UV index, lightmap resolution, Nanite settings and the source's build settings were copied. Generated lightmap UVs are rebuilt by the build for each asset, so they will not match the source's packing."));
	OutResult.Notes.Add(LOCTEXT("NoteCollision",
		"Simple collision primitives were NOT copied: a hull authored for the whole source shape would be wrong for a subset. Collision follows the option you chose."));
	OutResult.Notes.Add(LOCTEXT("NoteSockets",
		"Sockets and any custom asset metadata on the source were not carried over."));
	OutResult.Notes.Add(LOCTEXT("NoteUndo",
		"Undo does not delete created assets. Undo can revert level changes, but newly created assets must be deleted from the Content Browser by hand. Packages are marked dirty and are not saved until you save them."));

	for (const FName& EmptiedGroup : EmptiedGroupsWithAssets)
	{
		OutResult.Notes.Add(FText::Format(
			LOCTEXT("NoteEmptiedGroupFmt",
				"Group '{0}' has no pieces any more, but it previously generated an asset. That asset was left untouched and is now stale - delete it, or re-assign pieces to the group."),
			FText::FromName(EmptiedGroup)));
	}

	if (bAnyFallbackToNewAsset)
	{
		OutResult.Notes.Add(LOCTEXT("NoteFallback",
			"One or more groups could not be written in place and were created as new assets instead. Actors still pointing at the old assets will need re-assigning."));
	}

	return true;
}

// -------------------------------------------------------------------------------------------
// Profiles
// -------------------------------------------------------------------------------------------

FString UArchOpeningExtractionSubsystem::MakeProfilePackageName(const UStaticMesh* SourceMesh)
{
	if (!::IsValid(SourceMesh))
	{
		return FString();
	}

	const FString SourcePackage = SourceMesh->GetOutermost()->GetName();
	const FString Directory = FPackageName::GetLongPackagePath(SourcePackage);

	return Directory / (SourceMesh->GetName() + UArchOpeningExtractionProfile::GetProfileSuffix());
}

UArchOpeningExtractionProfile* UArchOpeningExtractionSubsystem::FindProfileFor(const UStaticMesh* SourceMesh) const
{
	const FString PackageName = MakeProfilePackageName(SourceMesh);
	if (PackageName.IsEmpty())
	{
		return nullptr;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;

	// LoadObject rather than a registry query: the profile may have been created this session and
	// not yet saved, in which case it is in memory but not in the asset registry's on-disk view.
	return LoadObject<UArchOpeningExtractionProfile>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UArchOpeningExtractionProfile* UArchOpeningExtractionSubsystem::SaveProfile(const UArchOpeningPieceSetComponent* PieceSet, FText& OutError) const
{
	if (PieceSet == nullptr || !::IsValid(PieceSet->SourceMesh))
	{
		OutError = LOCTEXT("NoProfileSource", "There is no source mesh to save a profile for.");
		return nullptr;
	}

	const FString PackageName = MakeProfilePackageName(PieceSet->SourceMesh);
	if (PackageName.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName))
	{
		OutError = LOCTEXT("BadProfilePath", "The source mesh is not in a valid content path, so no profile can be written next to it.");
		return nullptr;
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);

	UArchOpeningExtractionProfile* Profile = FindProfileFor(PieceSet->SourceMesh);

	if (Profile == nullptr)
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)
		{
			OutError = FText::Format(
				LOCTEXT("ProfilePackageFailedFmt", "Could not create the package '{0}'."), FText::FromString(PackageName));
			return nullptr;
		}

		Profile = NewObject<UArchOpeningExtractionProfile>(Package, *AssetName, RF_Public | RF_Standalone);
		if (Profile == nullptr)
		{
			OutError = LOCTEXT("ProfileFailed", "Could not create the extraction profile asset.");
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(Profile);
	}

	Profile->Modify();
	Profile->SourceMesh = PieceSet->SourceMesh;
	Profile->WeldTolerance = PieceSet->WeldTolerance;
	Profile->Groups = PieceSet->Groups;
	Profile->PieceCountAtSave = PieceSet->Pieces.Num();

	Profile->PieceKeyToGroup.Reset();
	for (const FArchOpeningPieceRecord& Piece : PieceSet->Pieces)
	{
		if (Piece.Key != INDEX_NONE)
		{
			Profile->PieceKeyToGroup.Add(Piece.Key, Piece.GroupIndex);
		}
	}

	Profile->MarkPackageDirty();

	return Profile;
}

bool UArchOpeningExtractionSubsystem::ApplyProfileTo(
	UArchOpeningPieceSetComponent* PieceSet,
	const UArchOpeningExtractionProfile* Profile,
	int32& OutMatched,
	int32& OutUnmatched) const
{
	OutMatched = 0;
	OutUnmatched = 0;

	if (PieceSet == nullptr || Profile == nullptr || Profile->Groups.IsEmpty())
	{
		return false;
	}

	PieceSet->Groups = Profile->Groups;
	PieceSet->ActiveGroupIndex = FMath::Clamp(PieceSet->ActiveGroupIndex, 0, PieceSet->Groups.Num() - 1);

	for (FArchOpeningPieceRecord& Piece : PieceSet->Pieces)
	{
		// Matched by key, not by index, so a re-analysis that orders the pieces differently still
		// restores the right assignments.
		const int32* SavedGroup = Profile->PieceKeyToGroup.Find(Piece.Key);

		if (SavedGroup != nullptr && PieceSet->Groups.IsValidIndex(*SavedGroup))
		{
			Piece.GroupIndex = *SavedGroup;
			++OutMatched;
		}
		else
		{
			// A piece the profile has never seen: leave it stationary rather than guessing.
			Piece.GroupIndex = 0;
			++OutUnmatched;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
