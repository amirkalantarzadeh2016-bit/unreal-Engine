// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningExtractionSubsystem.h"

#include "ArchOpeningLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
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
		if (GroupID != FPolygonGroupID::Invalid)
		{
			Piece.MaterialSlots.AddUnique(SlotNames[GroupID]);
		}

		for (const FVertexInstanceID InstanceID : Instances)
		{
			Piece.LocalBounds += FVector(Positions[SourceDescription->GetVertexInstanceVertex(InstanceID)]);
		}
	}

	// Vertex counts per piece, for the selection list.
	for (FArchOpeningMeshPiece& Piece : OutAnalysis.Pieces)
	{
		TSet<int32> UniqueVertices;
		for (int32 RawTriangleId : Piece.TriangleIds)
		{
			for (const FVertexInstanceID InstanceID : SourceDescription->GetTriangleVertexInstances(FTriangleID(RawTriangleId)))
			{
				UniqueVertices.Add(SourceDescription->GetVertexInstanceVertex(InstanceID).GetValue());
			}
		}
		Piece.VertexCount = UniqueVertices.Num();
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

FText UArchOpeningExtractionSubsystem::DescribePiece(const FArchOpeningMeshAnalysis& Analysis, int32 PieceIndex)
{
	if (!Analysis.Pieces.IsValidIndex(PieceIndex))
	{
		return FText::GetEmpty();
	}

	const FArchOpeningMeshPiece& Piece = Analysis.Pieces[PieceIndex];
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
		LOCTEXT("PieceFmt", "Piece {0}   {1} tris, {2} verts   size {3} x {4} x {5} cm   materials: {6}"),
		FText::AsNumber(PieceIndex),
		FText::AsNumber(Piece.TriangleCount),
		FText::AsNumber(Piece.VertexCount),
		FText::AsNumber(FMath::RoundToInt(Size.X)),
		FText::AsNumber(FMath::RoundToInt(Size.Y)),
		FText::AsNumber(FMath::RoundToInt(Size.Z)),
		FText::FromString(Slots.IsEmpty() ? TEXT("none") : Slots));
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

		FStaticMeshSourceModel& NewSourceModel = NewMesh->AddSourceModel();

		// Start from the source's build settings so lightmap and UV behaviour carries over, then
		// force normals and tangents to be kept rather than rebuilt: they were copied exactly.
		if (SourceMesh->GetNumSourceModels() > 0)
		{
			NewSourceModel.BuildSettings = SourceMesh->GetSourceModel(0).BuildSettings;
		}
		NewSourceModel.BuildSettings.bRecomputeNormals = false;
		NewSourceModel.BuildSettings.bRecomputeTangents = false;

		FMeshDescription* TargetDescription = NewMesh->CreateMeshDescription(0);
		if (TargetDescription == nullptr)
		{
			OutError = LOCTEXT("DescriptionFailed", "Could not create a mesh description on the new asset.");
			return nullptr;
		}

		*TargetDescription = MoveTemp(Description);
		NewMesh->CommitMeshDescription(0);

		// Material slots, in the order the subset actually uses them.
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

			FStaticMaterial NewMaterial(Material, SlotName, SlotName);
			NewMesh->GetStaticMaterials().Add(NewMaterial);
		}

		NewMesh->SetLightMapCoordinateIndex(SourceMesh->GetLightMapCoordinateIndex());
		NewMesh->SetLightMapResolution(SourceMesh->GetLightMapResolution());
		NewMesh->NaniteSettings = SourceMesh->NaniteSettings;

		// Collision. Simple collision primitives are deliberately NOT copied: a convex hull or box
		// authored for the whole source shape would be wrong for a subset of it.
		NewMesh->CreateBodySetup();
		if (UBodySetup* BodySetup = NewMesh->GetBodySetup())
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

		NewMesh->Build(/*bInSilent*/ true);
		NewMesh->PostEditChange();

		FAssetRegistryModule::AssetCreated(NewMesh);
		Package->MarkPackageDirty();

		return NewMesh;
	}
}

bool UArchOpeningExtractionSubsystem::ExtractPieces(
	UStaticMesh* SourceMesh,
	const FArchOpeningMeshAnalysis& Analysis,
	const TArray<int32>& SelectedPieceIndices,
	const FString& PackagePath,
	const FString& BaseAssetName,
	EArchOpeningExtractionCollision CollisionOption,
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

	if (SelectedPieceIndices.IsEmpty())
	{
		OutError = LOCTEXT("NoSelection", "No pieces are selected. Tick the pieces that make up the movable leaf.");
		return false;
	}

	if (SelectedPieceIndices.Num() >= Analysis.Pieces.Num())
	{
		OutError = LOCTEXT("SelectedEverything", "Every piece is selected, which would leave nothing behind as the fixed part. Deselect the frame and the fixed glazing.");
		return false;
	}

	if (!FPackageName::IsValidLongPackageName(PackagePath / TEXT("Probe")))
	{
		OutError = FText::Format(
			LOCTEXT("BadPathFmt", "'{0}' is not a valid content path. Use something like /Game/Openings."),
			FText::FromString(PackagePath));
		return false;
	}

	FMeshDescription* SourceDescription = SourceMesh->GetMeshDescription(0);
	if (SourceDescription == nullptr)
	{
		OutError = LOCTEXT("NoMeshDescriptionExtract", "The source mesh no longer has a readable LOD0 mesh description.");
		return false;
	}

	// Split the triangle set in two. Doing it as sets rather than by re-walking adjacency means the
	// two outputs are exactly complementary: no triangle is duplicated between them, so the retained
	// geometry cannot visually double up with the extracted leaf.
	TSet<int32> SelectedTriangles;
	TSet<int32> RemainderTriangles;

	for (int32 PieceIndex = 0; PieceIndex < Analysis.Pieces.Num(); ++PieceIndex)
	{
		const bool bSelected = SelectedPieceIndices.Contains(PieceIndex);
		TSet<int32>& Destination = bSelected ? SelectedTriangles : RemainderTriangles;

		for (int32 RawTriangleId : Analysis.Pieces[PieceIndex].TriangleIds)
		{
			Destination.Add(RawTriangleId);
		}
	}

	FMeshDescription SelectedDescription;
	TArray<FName> SelectedSlots;
	BuildSubsetDescription(*SourceDescription, SelectedTriangles, Analysis.NumUVChannels, SelectedDescription, SelectedSlots);

	FMeshDescription RemainderDescription;
	TArray<FName> RemainderSlots;
	BuildSubsetDescription(*SourceDescription, RemainderTriangles, Analysis.NumUVChannels, RemainderDescription, RemainderSlots);

	const FString CleanBaseName = BaseAssetName.IsEmpty() ? SourceMesh->GetName() : BaseAssetName;

	UStaticMesh* ExtractedMesh = CreateStaticMeshAsset(
		SourceMesh, SelectedDescription, SelectedSlots, PackagePath,
		CleanBaseName + TEXT("_Leaf"), CollisionOption, OutError);

	if (ExtractedMesh == nullptr)
	{
		return false;
	}

	UStaticMesh* RemainderMesh = CreateStaticMeshAsset(
		SourceMesh, RemainderDescription, RemainderSlots, PackagePath,
		CleanBaseName + TEXT("_Fixed"), CollisionOption, OutError);

	if (RemainderMesh == nullptr)
	{
		// The leaf asset already exists at this point; say so rather than leaving it a mystery.
		OutError = FText::Format(
			LOCTEXT("PartialFmt", "The leaf asset '{0}' was created but the fixed-part asset failed: {1}"),
			FText::FromString(ExtractedMesh->GetName()), OutError);
		return false;
	}

	OutResult.ExtractedMesh = ExtractedMesh;
	OutResult.RemainderMesh = RemainderMesh;
	OutResult.ExtractedTriangles = SelectedTriangles.Num();
	OutResult.RemainderTriangles = RemainderTriangles.Num();

	// Everything the artist has to know about what the output does and does not carry.
	OutResult.Notes.Add(LOCTEXT("NoteSourceUntouched",
		"The source asset was not modified in any way."));
	OutResult.Notes.Add(LOCTEXT("NoteLod0",
		"Only LOD0 was extracted. The new assets have a single LOD; any LODs the source had were not carried over. Generate LODs on the new assets if you need them."));
	OutResult.Notes.Add(LOCTEXT("NoteNormals",
		"Positions, normals, tangents, binormal signs, vertex colours and every UV channel were copied verbatim, and the build was told not to recompute normals or tangents."));
	OutResult.Notes.Add(LOCTEXT("NoteLightmap",
		"Lightmap UV index, lightmap resolution, Nanite settings and the source's build settings were copied. Generated lightmap UVs are rebuilt by the build for each new asset, so they will not match the source's packing."));
	OutResult.Notes.Add(LOCTEXT("NoteCollision",
		"Simple collision primitives were NOT copied: a hull authored for the whole source shape would be wrong for a subset. Collision on the new assets follows the option you chose."));
	OutResult.Notes.Add(LOCTEXT("NoteSockets",
		"Sockets and any custom asset metadata on the source were not carried over."));
	OutResult.Notes.Add(LOCTEXT("NoteUndo",
		"Undo does not delete these asset files. Undo can revert level changes, but newly created assets must be deleted from the Content Browser by hand. The packages are marked dirty and are not saved until you save them."));

	UE_LOG(LogArchOpenings, Log,
		TEXT("Extracted %d triangles into '%s' and %d triangles into '%s' from '%s'."),
		OutResult.ExtractedTriangles, *ExtractedMesh->GetPathName(),
		OutResult.RemainderTriangles, *RemainderMesh->GetPathName(),
		*SourceMesh->GetPathName());

	return true;
}

#undef LOCTEXT_NAMESPACE
