// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningPieceSetVisualizer.h"

#include "ArchOpeningPieceSetComponent.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "EditorViewportClient.h"
#include "MeshDescription.h"
#include "SceneManagement.h"
#include "StaticMeshAttributes.h"

IMPLEMENT_HIT_PROXY(HArchOpeningPieceProxy, HComponentVisProxy);

#define LOCTEXT_NAMESPACE "ArchOpeningPieceSetVisualizer"

void FArchOpeningPieceSetVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* /*View*/, FPrimitiveDrawInterface* PDI)
{
	const UArchOpeningPieceSetComponent* PieceSet = Cast<const UArchOpeningPieceSetComponent>(Component);
	if (PieceSet == nullptr || PDI == nullptr || PieceSet->Pieces.IsEmpty())
	{
		return;
	}

	const FTransform ToWorld = PieceSet->GetSourceWorldTransform();
	const FMatrix ToWorldMatrix = ToWorld.ToMatrixWithScale();

	for (int32 PieceIndex = 0; PieceIndex < PieceSet->Pieces.Num(); ++PieceIndex)
	{
		const FArchOpeningPieceRecord& Piece = PieceSet->Pieces[PieceIndex];
		if (!Piece.LocalBounds.IsValid)
		{
			continue;
		}

		const bool bHovered = (PieceIndex == PieceSet->HoveredPieceIndex);
		const bool bSelected = PieceSet->SelectedPieces.Contains(PieceIndex);

		// The hit proxy has to wrap the drawing calls: everything emitted between SetHitProxy and
		// the reset below is clickable and reports this piece index.
		PDI->SetHitProxy(new HArchOpeningPieceProxy(Component, PieceIndex));

		const float Thickness = bHovered ? 4.0f : (bSelected ? 3.0f : 1.5f);

		DrawWireBox(PDI, ToWorldMatrix, Piece.LocalBounds,
			PieceSet->GetPieceDrawColor(PieceIndex), SDPG_Foreground, Thickness);

		// A point at the centre gives small mouldings something clickable even when their box is
		// only a few pixels across on screen.
		PDI->DrawPoint(ToWorld.TransformPosition(Piece.LocalBounds.GetCenter()),
			PieceSet->GetPieceDrawColor(PieceIndex), bHovered ? 14.0f : 8.0f, SDPG_Foreground);

		PDI->SetHitProxy(nullptr);
	}

	if (PieceSet->bDrawPieceWireframe)
	{
		DrawPieceWireframe(PieceSet, ToWorld, PDI);
	}
}

void FArchOpeningPieceSetVisualizer::DrawPieceWireframe(const UArchOpeningPieceSetComponent* PieceSet, const FTransform& ToWorld, FPrimitiveDrawInterface* PDI) const
{
	UStaticMesh* SourceMesh = PieceSet->SourceMesh;
	if (!::IsValid(SourceMesh))
	{
		return;
	}

	const FMeshDescription* Description = SourceMesh->GetMeshDescription(0);
	if (Description == nullptr)
	{
		return;
	}

	// Triangle ids are not stored per piece here (the component keeps only the summary the UI
	// needs), so the overlay is derived by walking the description once and colouring each triangle
	// by the piece whose bounds contain its centroid. That is approximate where two pieces overlap,
	// and it is only a visual aid, never the basis for the actual split.
	FStaticMeshConstAttributes Attributes(*Description);
	TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();

	int32 LinesDrawn = 0;

	for (const FTriangleID TriangleID : Description->Triangles().GetElementIDs())
	{
		if (LinesDrawn >= MaxWireframeLines)
		{
			break;
		}

		TArrayView<const FVertexInstanceID> Instances = Description->GetTriangleVertexInstances(TriangleID);
		if (Instances.Num() < 3)
		{
			continue;
		}

		FVector Corners[3];
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			Corners[Corner] = FVector(Positions[Description->GetVertexInstanceVertex(Instances[Corner])]);
		}

		const FVector Centroid = (Corners[0] + Corners[1] + Corners[2]) / 3.0;

		int32 OwningPiece = INDEX_NONE;
		double SmallestVolume = TNumericLimits<double>::Max();

		for (int32 PieceIndex = 0; PieceIndex < PieceSet->Pieces.Num(); ++PieceIndex)
		{
			const FBox& Box = PieceSet->Pieces[PieceIndex].LocalBounds;
			if (!Box.IsValid || !Box.IsInsideOrOn(Centroid))
			{
				continue;
			}

			// Nested boxes are common (a moulding sits inside the leaf's box), so the tightest
			// containing box is the better guess at which piece a triangle belongs to.
			const FVector Size = Box.GetSize();
			const double Volume = Size.X * Size.Y * Size.Z;
			if (Volume < SmallestVolume)
			{
				SmallestVolume = Volume;
				OwningPiece = PieceIndex;
			}
		}

		const FLinearColor Color = (OwningPiece != INDEX_NONE)
			? PieceSet->GetPieceDrawColor(OwningPiece)
			: FLinearColor(0.2f, 0.2f, 0.2f);

		for (int32 Edge = 0; Edge < 3; ++Edge)
		{
			PDI->DrawLine(
				ToWorld.TransformPosition(Corners[Edge]),
				ToWorld.TransformPosition(Corners[(Edge + 1) % 3]),
				Color, SDPG_World, 0.5f);
		}

		LinesDrawn += 3;
	}
}

void FArchOpeningPieceSetVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* /*Viewport*/, const FSceneView* View, FCanvas* Canvas)
{
	const UArchOpeningPieceSetComponent* PieceSet = Cast<const UArchOpeningPieceSetComponent>(Component);
	if (PieceSet == nullptr || Canvas == nullptr || View == nullptr)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}

	// Label the hovered piece only. Labelling thirty pieces at once would be unreadable, and the
	// list panel already carries the full detail.
	if (!PieceSet->Pieces.IsValidIndex(PieceSet->HoveredPieceIndex))
	{
		return;
	}

	const FArchOpeningPieceRecord& Piece = PieceSet->Pieces[PieceSet->HoveredPieceIndex];
	const FTransform ToWorld = PieceSet->GetSourceWorldTransform();
	const FVector WorldCentre = ToWorld.TransformPosition(Piece.LocalBounds.GetCenter());

	const FVector4 Projected = View->WorldToScreen(WorldCentre);
	if (Projected.W <= 0.0f)
	{
		return;
	}

	FVector2D ScreenPosition;
	if (!View->ScreenToPixel(Projected, ScreenPosition))
	{
		return;
	}

	const FName GroupName = PieceSet->Groups.IsValidIndex(Piece.GroupIndex)
		? PieceSet->Groups[Piece.GroupIndex].GroupName
		: NAME_None;

	const FString Label = FString::Printf(TEXT("Piece %d  |  %d tris  |  %s"),
		PieceSet->HoveredPieceIndex, Piece.TriangleCount, *GroupName.ToString());

	FCanvasTextItem TextItem(ScreenPosition, FText::FromString(Label), Font, FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(TextItem);
}

bool FArchOpeningPieceSetVisualizer::VisProxyHandleClick(FEditorViewportClient* /*InViewportClient*/, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if (VisProxy == nullptr || !VisProxy->IsA(HArchOpeningPieceProxy::StaticGetType()))
	{
		return false;
	}

	HArchOpeningPieceProxy* PieceProxy = static_cast<HArchOpeningPieceProxy*>(VisProxy);

	// const_cast is the established pattern here: HComponentVisProxy holds the component const, but
	// handling a click is exactly the case where the visualizer is meant to write to it.
	const UArchOpeningPieceSetComponent* ConstPieceSet =
		Cast<UArchOpeningPieceSetComponent>(PieceProxy->Component.Get());

	UArchOpeningPieceSetComponent* PieceSet = const_cast<UArchOpeningPieceSetComponent*>(ConstPieceSet);

	if (PieceSet == nullptr || !PieceSet->Pieces.IsValidIndex(PieceProxy->PieceIndex))
	{
		return false;
	}

	const int32 PieceIndex = PieceProxy->PieceIndex;

	if (Click.IsControlDown())
	{
		// Ctrl builds a multi-selection for the batch utilities without assigning anything.
		PieceSet->ToggleSelection(PieceIndex);
	}
	else if (Click.IsShiftDown())
	{
		// Shift adds to the selection and assigns the whole selection at once.
		PieceSet->SelectedPieces.Add(PieceIndex);
		PieceSet->AssignSelectionToGroup(PieceSet->ActiveGroupIndex);
	}
	else
	{
		// The fast path: click a piece, it joins the active group immediately.
		PieceSet->SetSelection(PieceIndex);
		PieceSet->AssignPieceToGroup(PieceIndex, PieceSet->ActiveGroupIndex);
	}

	if (GEditor != nullptr)
	{
		GEditor->RedrawLevelEditingViewports();
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
