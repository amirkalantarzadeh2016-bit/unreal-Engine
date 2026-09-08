// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningComponentVisualizer.h"

#include "ArchOpeningComponent.h"
#include "ArchOpeningSolver.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "SceneManagement.h"

namespace ArchOpeningVisualizerStyle
{
	static const FLinearColor HingeColor(1.0f, 0.55f, 0.10f);
	static const FLinearColor AxisColor(1.0f, 0.80f, 0.25f);
	static const FLinearColor OutsideColor(0.20f, 0.80f, 1.0f);
	static const FLinearColor ArcColor(0.30f, 1.0f, 0.45f);
	static const FLinearColor SlideColor(0.30f, 1.0f, 0.45f);
	static const FLinearColor BoundsColor(0.55f, 0.55f, 0.60f);
	static const FLinearColor ProximityColor(0.90f, 0.35f, 0.90f);

	static constexpr float LineThickness = 2.0f;
	static constexpr float AxisHalfLength = 60.0f;
	static constexpr float OutsideArrowLength = 80.0f;
	static constexpr int32 ArcSegments = 24;
}

void FArchOpeningComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* /*View*/, FPrimitiveDrawInterface* PDI)
{
	const UArchOpeningComponent* Opening = Cast<const UArchOpeningComponent>(Component);
	if (Opening == nullptr || PDI == nullptr)
	{
		return;
	}

	const FTransform Frame = Opening->GetCalibrationFrame();

	DrawShared(Opening, Frame, PDI);

	if (Opening->MotionType == EArchOpeningMotionType::Hinged)
	{
		DrawHinged(Opening, Frame, PDI);
	}
	else
	{
		DrawSliding(Opening, Frame, PDI);
	}
}

void FArchOpeningComponentVisualizer::DrawShared(const UArchOpeningComponent* Opening, const FTransform& Frame, FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchOpeningVisualizerStyle;

	const FVector Origin = Frame.GetLocation();

	// The reference outside direction. Everything about handing and swing is measured from this, so
	// it is drawn prominently and labelled in the HUD pass.
	const FVector Outside = Frame.TransformVectorNoScale(Opening->Calibration.OutsideDirection.GetSafeNormal());
	if (!Outside.IsNearlyZero())
	{
		const FMatrix ArrowToWorld = FRotationMatrix::MakeFromX(Outside) * FTranslationMatrix(Origin);
		DrawDirectionalArrow(PDI, ArrowToWorld, OutsideColor, OutsideArrowLength,
			/*ArrowSize*/ 12.0f, SDPG_Foreground, LineThickness);
	}

	// Leaf bounds: the volume the obstruction query sweeps.
	const FBox LeafBounds = Opening->GetLeafLocalBounds();
	if (LeafBounds.IsValid)
	{
		DrawWireBox(PDI, Frame.ToMatrixWithScale(), LeafBounds, BoundsColor, SDPG_World, 1.0f);
	}

	// The proximity trigger, which at edit time exists only as this drawing: the real box component
	// is created at BeginPlay, so the level never carries it.
	if (Opening->Interaction.Mode == EArchOpeningInteractionMode::ProximityOnly ||
		Opening->Interaction.Mode == EArchOpeningInteractionMode::ClickAndProximity)
	{
		const FTransform BoxTransform(
			Frame.GetRotation(),
			Frame.TransformPosition(Opening->Proximity.BoxOffset),
			FVector::OneVector);

		DrawWireBox(
			PDI,
			BoxTransform.ToMatrixWithScale(),
			FBox(-Opening->Proximity.BoxExtent, Opening->Proximity.BoxExtent),
			ProximityColor, SDPG_World, 1.0f);
	}

	// Handle pivots and their rotation axes.
	for (const FArchOpeningHandleGroup& Group : Opening->HandleGroups)
	{
		const FVector PivotWorld = Frame.TransformPosition(Group.PivotLocation);
		PDI->DrawPoint(PivotWorld, HingeColor, 10.0f, SDPG_Foreground);

		const FVector AxisWorld = Frame.TransformVectorNoScale(Group.RotationAxis.GetSafeNormal());
		if (!AxisWorld.IsNearlyZero())
		{
			PDI->DrawLine(PivotWorld - AxisWorld * 15.0f, PivotWorld + AxisWorld * 15.0f,
				AxisColor, SDPG_Foreground, 1.5f);
		}
	}
}

void FArchOpeningComponentVisualizer::DrawHinged(const UArchOpeningComponent* Opening, const FTransform& Frame, FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchOpeningVisualizerStyle;

	const FVector HingeWorld = Frame.TransformPosition(Opening->Hinged.HingeLocation);

	// Hinge marker.
	PDI->DrawPoint(HingeWorld, HingeColor, 16.0f, SDPG_Foreground);

	const FVector AxisLocal = FArchOpeningSolver::ComputeHingeAxis(Opening->Hinged, Opening->Calibration);
	if (AxisLocal.IsNearlyZero())
	{
		return;	// Degenerate setup; validation already reports it.
	}

	const FVector AxisWorld = Frame.TransformVectorNoScale(AxisLocal);
	PDI->DrawLine(HingeWorld - AxisWorld * AxisHalfLength, HingeWorld + AxisWorld * AxisHalfLength,
		AxisColor, SDPG_Foreground, LineThickness);

	// Swing arc. Radius comes from the leaf bounds so the arc matches the leaf the artist assigned.
	// Arc radius follows the assigned leaf so the drawing matches the geometry, with a sensible
	// fallback before any leaf has been assigned.
	const FBox LeafBounds = Opening->GetLeafLocalBounds();

	FVector RadialLocal = FVector::ZeroVector;
	if (LeafBounds.IsValid)
	{
		const FVector ToCentre = LeafBounds.GetCenter() - Opening->Hinged.HingeLocation;
		RadialLocal = ToCentre - AxisLocal * FVector::DotProduct(ToCentre, AxisLocal);
		RadialLocal *= 2.0;	// Reach the free edge rather than stopping at the leaf centre.
	}

	if (RadialLocal.IsNearlyZero())
	{
		// No leaf yet: use any direction perpendicular to the hinge axis, 100 cm out.
		const FVector Outside = Opening->Calibration.OutsideDirection.GetSafeNormal();
		FVector Fallback = Outside - AxisLocal * FVector::DotProduct(Outside, AxisLocal);
		if (Fallback.IsNearlyZero())
		{
			Fallback = FVector::CrossProduct(AxisLocal, FVector::UpVector);
		}
		RadialLocal = Fallback.GetSafeNormal() * 100.0;
	}

	const FVector ArcStartLocal = Opening->Hinged.HingeLocation + RadialLocal;

	const float TotalAngle = Opening->Hinged.OpenAngle;
	FVector PreviousWorld = Frame.TransformPosition(ArcStartLocal);

	// Radial line marking the closed position.
	PDI->DrawLine(HingeWorld, PreviousWorld, ArcColor * 0.6f, SDPG_Foreground, 1.0f);

	for (int32 Segment = 1; Segment <= ArcSegments; ++Segment)
	{
		const float Alpha = static_cast<float>(Segment) / static_cast<float>(ArcSegments);
		const FTransform Delta = FArchOpeningSolver::MakeRotationDelta(
			Opening->Hinged.HingeLocation, AxisLocal, TotalAngle * Alpha);

		const FVector CurrentWorld = Frame.TransformPosition(Delta.TransformPosition(ArcStartLocal));
		PDI->DrawLine(PreviousWorld, CurrentWorld, ArcColor, SDPG_Foreground, LineThickness);
		PreviousWorld = CurrentWorld;
	}

	// Radial line marking the fully open position, so the angular range reads clearly.
	PDI->DrawLine(HingeWorld, PreviousWorld, ArcColor, SDPG_Foreground, LineThickness);
	PDI->DrawPoint(PreviousWorld, ArcColor, 10.0f, SDPG_Foreground);
}

void FArchOpeningComponentVisualizer::DrawSliding(const UArchOpeningComponent* Opening, const FTransform& Frame, FPrimitiveDrawInterface* PDI) const
{
	using namespace ArchOpeningVisualizerStyle;

	const FVector DirectionLocal = FArchOpeningSolver::ComputeSlideDirection(Opening->Sliding, Opening->Calibration);
	if (DirectionLocal.IsNearlyZero())
	{
		return;
	}

	const FBox LeafBounds = Opening->GetLeafLocalBounds();
	const FVector StartLocal = LeafBounds.IsValid ? LeafBounds.GetCenter() : FVector::ZeroVector;
	const FVector EndLocal = StartLocal + DirectionLocal * Opening->Sliding.TravelDistance;

	const FVector StartWorld = Frame.TransformPosition(StartLocal);
	const FVector EndWorld = Frame.TransformPosition(EndLocal);

	PDI->DrawLine(StartWorld, EndWorld, SlideColor, SDPG_Foreground, LineThickness);
	PDI->DrawPoint(StartWorld, SlideColor * 0.6f, 12.0f, SDPG_Foreground);
	PDI->DrawPoint(EndWorld, SlideColor, 16.0f, SDPG_Foreground);

	// Ghost of the leaf bounds at full travel, so the artist can see where the panel ends up.
	if (LeafBounds.IsValid)
	{
		const FTransform OpenTransform(
			Frame.GetRotation(),
			Frame.TransformPosition(DirectionLocal * Opening->Sliding.TravelDistance),
			Frame.GetScale3D());

		DrawWireBox(PDI, OpenTransform.ToMatrixWithScale(), LeafBounds, SlideColor * 0.5f, SDPG_World, 1.0f);
	}
}

void FArchOpeningComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* /*Viewport*/, const FSceneView* View, FCanvas* Canvas)
{
	const UArchOpeningComponent* Opening = Cast<const UArchOpeningComponent>(Component);
	if (Opening == nullptr || Canvas == nullptr || View == nullptr)
	{
		return;
	}

	const FTransform Frame = Opening->GetCalibrationFrame();
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}

	auto Label = [&](const FVector& WorldLocation, const FString& Text, const FLinearColor& Color)
	{
		const FVector4 Projected = View->WorldToScreen(WorldLocation);
		if (Projected.W <= 0.0f)
		{
			return;
		}

		FVector2D ScreenPosition;
		if (!View->ScreenToPixel(Projected, ScreenPosition))
		{
			return;
		}

		FCanvasTextItem TextItem(ScreenPosition, FText::FromString(Text), Font, Color);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
	};

	const FVector Outside = Frame.TransformVectorNoScale(Opening->Calibration.OutsideDirection.GetSafeNormal());
	Label(Frame.GetLocation() + Outside * ArchOpeningVisualizerStyle::OutsideArrowLength,
		TEXT("Outside"), ArchOpeningVisualizerStyle::OutsideColor);

	if (Opening->MotionType == EArchOpeningMotionType::Hinged)
	{
		const FString HingeLabel = FString::Printf(TEXT("Hinge  %s  %.0f deg"),
			Opening->Hinged.AxisPreset == EArchOpeningHingeAxisPreset::VerticalSide
				? (Opening->Hinged.HingeSide == EArchOpeningHingeSide::Left ? TEXT("(left)") : TEXT("(right)"))
				: TEXT(""),
			Opening->Hinged.OpenAngle);

		Label(Frame.TransformPosition(Opening->Hinged.HingeLocation), HingeLabel,
			ArchOpeningVisualizerStyle::HingeColor);
	}
	else
	{
		const FBox LeafBounds = Opening->GetLeafLocalBounds();
		const FVector StartLocal = LeafBounds.IsValid ? LeafBounds.GetCenter() : FVector::ZeroVector;
		const FVector DirectionLocal = FArchOpeningSolver::ComputeSlideDirection(Opening->Sliding, Opening->Calibration);

		Label(Frame.TransformPosition(StartLocal + DirectionLocal * Opening->Sliding.TravelDistance),
			FString::Printf(TEXT("Travel %.0f cm"), Opening->Sliding.TravelDistance),
			ArchOpeningVisualizerStyle::SlideColor);
	}
}
