// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPathComponentVisualizer.h"

#include "ActorEditorUtils.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "TourEditorSettings.h"
#include "TourGeometryLibrary.h"
#include "TourPath.h"
#include "TourSplineComponent.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "ArchVizTourEditor"

IMPLEMENT_HIT_PROXY(HTourPointProxy, HComponentVisProxy)

namespace ArchVizTourEditor::VisualizerPrivate
{
	/** Segments drawn per spline segment. High enough that the drawn curve matches the real one. */
	static constexpr int32 CurveDrawSegments = 24;

	/** Depth priority used for handles, so they stay visible through geometry. */
	static constexpr ESceneDepthPriorityGroup HandleDepthPriority = SDPG_Foreground;

	/** Length of the direction arrow drawn at each point, in centimetres. */
	static constexpr float DirectionArrowLength = 60.0f;

	/** Scale applied to the tangent when drawing its handle, so long tangents stay on screen. */
	static constexpr float TangentHandleScale = 1.0f / 3.0f;

	/** World position of a point's arrive-tangent handle. */
	static FVector GetArriveHandleLocation(const ATourPath& Path, int32 PointIndex)
	{
		const FTourPoint& Point = Path.Points[PointIndex];
		return Path.GetActorTransform().TransformPosition(Point.Location - Point.ArriveTangent * TangentHandleScale);
	}

	/** World position of a point's leave-tangent handle. */
	static FVector GetLeaveHandleLocation(const ATourPath& Path, int32 PointIndex)
	{
		const FTourPoint& Point = Path.Points[PointIndex];
		return Path.GetActorTransform().TransformPosition(Point.Location + Point.LeaveTangent * TangentHandleScale);
	}
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

ATourPath* FTourPathComponentVisualizer::GetEditedPath() const
{
	if (const UActorComponent* Component = EditedComponentPath.GetComponent())
	{
		return Cast<ATourPath>(Component->GetOwner());
	}

	return nullptr;
}

void FTourPathComponentVisualizer::ClearSelection()
{
	EditedComponentPath = FComponentPropertyPath();
	SelectedPointIndex = INDEX_NONE;
	SelectedElement = ESelectedElement::None;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void FTourPathComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	using namespace ArchVizTourEditor::VisualizerPrivate;

	const UTourSplineComponent* SplineComponent = Cast<const UTourSplineComponent>(Component);
	if (SplineComponent == nullptr)
	{
		return;
	}

	const ATourPath* Path = Cast<ATourPath>(SplineComponent->GetOwner());
	if (Path == nullptr || Path->Points.Num() == 0)
	{
		return;
	}

	const UTourEditorSettings& Settings = UTourEditorSettings::Get();
	const FTransform ActorTransform = Path->GetActorTransform();
	const FTourPathData PathData = Path->BuildPathData();

	// --- The curve itself ---------------------------------------------------
	const int32 SegmentCount = PathData.GetSegmentCount();
	for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
	{
		FVector PreviousPoint = ActorTransform.TransformPosition(
			UTourGeometryLibrary::EvaluatePositionAtKey(PathData, static_cast<float>(Segment)));

		for (int32 Step = 1; Step <= CurveDrawSegments; ++Step)
		{
			const float Key = static_cast<float>(Segment) + static_cast<float>(Step) / static_cast<float>(CurveDrawSegments);
			const FVector NextPoint = ActorTransform.TransformPosition(UTourGeometryLibrary::EvaluatePositionAtKey(PathData, Key));

			PDI->DrawLine(PreviousPoint, NextPoint, Settings.PointColor, HandleDepthPriority, 2.0f);
			PreviousPoint = NextPoint;
		}
	}

	// --- Handles ------------------------------------------------------------
	for (int32 PointIndex = 0; PointIndex < Path->Points.Num(); ++PointIndex)
	{
		const FTourPoint& Point = Path->Points[PointIndex];
		const FVector WorldLocation = ActorTransform.TransformPosition(Point.Location);

		const bool bSelected = (PointIndex == SelectedPointIndex) && (GetEditedPath() == Path);
		const FLinearColor PointColor = bSelected ? Settings.SelectedPointColor : Settings.PointColor;

		PDI->SetHitProxy(new HTourPointProxy(Component, PointIndex, static_cast<int32>(ESelectedElement::Point)));
		PDI->DrawPoint(WorldLocation, PointColor, Settings.PointHandleSize, HandleDepthPriority);
		PDI->SetHitProxy(nullptr);

		if (Settings.bDrawTangentHandles)
		{
			const FVector ArriveHandle = GetArriveHandleLocation(*Path, PointIndex);
			const FVector LeaveHandle  = GetLeaveHandleLocation(*Path, PointIndex);

			PDI->DrawLine(ArriveHandle, WorldLocation, Settings.TangentColor, HandleDepthPriority, 1.0f);
			PDI->DrawLine(WorldLocation, LeaveHandle, Settings.TangentColor, HandleDepthPriority, 1.0f);

			PDI->SetHitProxy(new HTourPointProxy(Component, PointIndex, static_cast<int32>(ESelectedElement::ArriveTangent)));
			PDI->DrawPoint(ArriveHandle, Settings.TangentColor, Settings.PointHandleSize * 0.7f, HandleDepthPriority);
			PDI->SetHitProxy(nullptr);

			PDI->SetHitProxy(new HTourPointProxy(Component, PointIndex, static_cast<int32>(ESelectedElement::LeaveTangent)));
			PDI->DrawPoint(LeaveHandle, Settings.TangentColor, Settings.PointHandleSize * 0.7f, HandleDepthPriority);
			PDI->SetHitProxy(nullptr);
		}

		if (Settings.bDrawDirectionArrows)
		{
			// The direction of travel is the leave tangent; drawing it removes any doubt about
			// which way a reversed path now runs.
			const FVector Direction = ActorTransform.TransformVector(Point.LeaveTangent).GetSafeNormal();
			if (!Direction.IsNearlyZero())
			{
				const FVector ArrowEnd = WorldLocation + Direction * DirectionArrowLength;
				PDI->DrawLine(WorldLocation, ArrowEnd, PointColor, HandleDepthPriority, 2.0f);

				// Two short barbs, drawn in the plane that faces the viewer's up axis.
				const FVector Side = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal() * (DirectionArrowLength * 0.25f);
				PDI->DrawLine(ArrowEnd, ArrowEnd - Direction * (DirectionArrowLength * 0.25f) + Side, PointColor, HandleDepthPriority, 2.0f);
				PDI->DrawLine(ArrowEnd, ArrowEnd - Direction * (DirectionArrowLength * 0.25f) - Side, PointColor, HandleDepthPriority, 2.0f);
			}
		}

		if (Point.bUseExplicitRotation)
		{
			// Show where an explicitly rotated point actually looks, which is the one thing the
			// curve geometry cannot convey.
			const FQuat WorldRotation = ActorTransform.GetRotation() * Point.Rotation.Quaternion();
			PDI->DrawLine(
				WorldLocation,
				WorldLocation + WorldRotation.GetForwardVector() * DirectionArrowLength * 2.0f,
				FLinearColor::Yellow, HandleDepthPriority, 1.0f);
		}
	}
}

void FTourPathComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	const UTourEditorSettings& Settings = UTourEditorSettings::Get();
	if (!Settings.bDrawPointLabels)
	{
		return;
	}

	const UTourSplineComponent* SplineComponent = Cast<const UTourSplineComponent>(Component);
	if (SplineComponent == nullptr)
	{
		return;
	}

	const ATourPath* Path = Cast<ATourPath>(SplineComponent->GetOwner());
	if (Path == nullptr)
	{
		return;
	}

	const FTransform ActorTransform = Path->GetActorTransform();
	UFont* Font = GEngine->GetSmallFont();

	for (int32 PointIndex = 0; PointIndex < Path->Points.Num(); ++PointIndex)
	{
		const FTourPoint& Point = Path->Points[PointIndex];
		const FVector WorldLocation = ActorTransform.TransformPosition(Point.Location);

		const FPlane Projected = View->Project(WorldLocation);
		if (Projected.W <= 0.0f)
		{
			// Behind the camera; projecting it would draw the label on the wrong side of the screen.
			continue;
		}

		const FIntRect ViewRect = View->UnscaledViewRect;
		const float ScreenX = ViewRect.Min.X + (0.5f + Projected.X * 0.5f) * ViewRect.Width();
		const float ScreenY = ViewRect.Min.Y + (0.5f - Projected.Y * 0.5f) * ViewRect.Height();

		FString Label = FString::Printf(TEXT("%d"), PointIndex);
		if (!Point.Label.IsEmpty())
		{
			Label += TEXT(" ") + Point.Label.ToString();
		}
		if (Point.DwellTime > 0.0f)
		{
			Label += FString::Printf(TEXT("  [dwell %.1fs]"), Point.DwellTime);
		}
		Label += FString::Printf(TEXT("  %.0fmm  f/%.1f"), Point.FocalLength, Point.Aperture);

		const bool bSelected = (PointIndex == SelectedPointIndex) && (GetEditedPath() == Path);
		const FLinearColor Color = bSelected ? Settings.SelectedPointColor : Settings.PointColor;

		FCanvasTextItem TextItem(FVector2D(ScreenX + 8.0f, ScreenY - 8.0f), FText::FromString(Label), Font, Color);
		TextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TextItem);
	}
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

bool FTourPathComponentVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if (VisProxy == nullptr || !VisProxy->IsA(HTourPointProxy::StaticGetType()))
	{
		ClearSelection();
		return false;
	}

	const HTourPointProxy* PointProxy = static_cast<HTourPointProxy*>(VisProxy);

	const UActorComponent* Component = PointProxy->Component.Get();
	const UTourSplineComponent* SplineComponent = Cast<const UTourSplineComponent>(Component);
	if (SplineComponent == nullptr)
	{
		ClearSelection();
		return false;
	}

	const ATourPath* Path = Cast<ATourPath>(SplineComponent->GetOwner());
	if (Path == nullptr || !Path->Points.IsValidIndex(PointProxy->PointIndex))
	{
		ClearSelection();
		return false;
	}

	// A property path rather than a raw pointer: it survives the component being reconstructed
	// by a Blueprint recompile, which a raw pointer would not.
	EditedComponentPath = FComponentPropertyPath(SplineComponent);
	SelectedPointIndex = PointProxy->PointIndex;
	SelectedElement = static_cast<ESelectedElement>(PointProxy->Element);

	return true;
}

bool FTourPathComponentVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	using namespace ArchVizTourEditor::VisualizerPrivate;

	const ATourPath* Path = GetEditedPath();
	if (Path == nullptr || !Path->Points.IsValidIndex(SelectedPointIndex))
	{
		return false;
	}

	switch (SelectedElement)
	{
	case ESelectedElement::ArriveTangent:
		OutLocation = GetArriveHandleLocation(*Path, SelectedPointIndex);
		return true;

	case ESelectedElement::LeaveTangent:
		OutLocation = GetLeaveHandleLocation(*Path, SelectedPointIndex);
		return true;

	case ESelectedElement::Point:
		OutLocation = Path->GetActorTransform().TransformPosition(Path->Points[SelectedPointIndex].Location);
		return true;

	default:
		return false;
	}
}

bool FTourPathComponentVisualizer::HandleInputDelta(
	FEditorViewportClient* ViewportClient, FViewport* Viewport,
	FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{
	using namespace ArchVizTourEditor::VisualizerPrivate;

	ATourPath* Path = GetEditedPath();
	if (Path == nullptr || !Path->Points.IsValidIndex(SelectedPointIndex) || SelectedElement == ESelectedElement::None)
	{
		return false;
	}

	if (DeltaTranslate.IsNearlyZero() && DeltaRotate.IsNearlyZero())
	{
		return false;
	}

	// The drag is a single undo unit: FScopedTransaction here would open one transaction per
	// mouse-move event, so the editor's own drag transaction is relied on and only Modify is
	// called. NotifyPropertyModified below is what actually records the change.
	Path->Modify();

	const FTransform ActorTransform = Path->GetActorTransform();
	// Deltas arrive in world space; the points are actor-local.
	const FVector LocalDelta = ActorTransform.InverseTransformVector(DeltaTranslate);

	FTourPoint& Point = Path->Points[SelectedPointIndex];

	switch (SelectedElement)
	{
	case ESelectedElement::Point:
		Point.Location += LocalDelta;

		if (!DeltaRotate.IsNearlyZero())
		{
			// Rotating a point handle is how an explicit camera orientation is authored, so the
			// rotation gizmo implicitly opts the point in.
			Point.Rotation = (DeltaRotate.Quaternion() * Point.Rotation.Quaternion()).Rotator();
			Point.bUseExplicitRotation = true;
		}
		break;

	case ESelectedElement::ArriveTangent:
		// The handle is drawn at a third of the tangent, so a handle-space delta scales back up.
		Point.ArriveTangent -= LocalDelta / TangentHandleScale;
		Point.PointType = ESplinePointType::CurveCustomTangent;
		break;

	case ESelectedElement::LeaveTangent:
		Point.LeaveTangent += LocalDelta / TangentHandleScale;
		Point.PointType = ESplinePointType::CurveCustomTangent;
		break;

	default:
		return false;
	}

	Path->SyncSplineFromPoints();
	Path->RebuildRailMeshes();

	NotifyPropertyModified(Path, FindFProperty<FProperty>(ATourPath::StaticClass(), GET_MEMBER_NAME_CHECKED(ATourPath, Points)));

	return true;
}

bool FTourPathComponentVisualizer::HandleInputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Event != IE_Pressed || Key != EKeys::Delete)
	{
		return false;
	}

	if (SelectedPointIndex == INDEX_NONE)
	{
		return false;
	}

	DeleteSelectedPoint();
	return true;
}

void FTourPathComponentVisualizer::EndEditing()
{
	ClearSelection();
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

TSharedPtr<SWidget> FTourPathComponentVisualizer::GenerateContextMenu() const
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/ true, /*CommandList*/ nullptr);

	// The visualizer is owned by the editor and outlives the menu, so binding raw is safe here;
	// the menu cannot outlive the viewport that opened it.
	FTourPathComponentVisualizer* MutableThis = const_cast<FTourPathComponentVisualizer*>(this);

	MenuBuilder.BeginSection(TEXT("TourPathPoint"), LOCTEXT("TourPointSection", "Tour Point"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("InsertPointAfter", "Insert Point After"),
			LOCTEXT("InsertPointAfterTooltip", "Insert a new point midway along the segment following this one."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(MutableThis, &FTourPathComponentVisualizer::InsertPointAfterSelection)));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeletePoint", "Delete Point"),
			LOCTEXT("DeletePointTooltip", "Delete the selected point. A path must keep at least two points."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(MutableThis, &FTourPathComponentVisualizer::DeleteSelectedPoint)));
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(TEXT("TourPathPointType"), LOCTEXT("TourPointTypeSection", "Point Type"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("PointTypeCurve", "Curve"),
			LOCTEXT("PointTypeCurveTooltip", "Smooth interpolation using automatically computed tangents."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(MutableThis, &FTourPathComponentVisualizer::SetSelectedPointType, TEnumAsByte<ESplinePointType::Type>(ESplinePointType::Curve))));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PointTypeCustom", "Custom Tangent"),
			LOCTEXT("PointTypeCustomTooltip", "Smooth interpolation using the authored tangents. This is what the arc generator produces."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(MutableThis, &FTourPathComponentVisualizer::SetSelectedPointType, TEnumAsByte<ESplinePointType::Type>(ESplinePointType::CurveCustomTangent))));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PointTypeLinear", "Linear"),
			LOCTEXT("PointTypeLinearTooltip", "Straight line to the next point."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(MutableThis, &FTourPathComponentVisualizer::SetSelectedPointType, TEnumAsByte<ESplinePointType::Type>(ESplinePointType::Linear))));
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FTourPathComponentVisualizer::InsertPointAfterSelection()
{
	ATourPath* Path = GetEditedPath();
	if (Path == nullptr || !Path->Points.IsValidIndex(SelectedPointIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("InsertTourPointTransaction", "Insert Tour Point"));
	Path->Modify();

	const int32 NextIndex = (SelectedPointIndex + 1) % Path->Points.Num();
	const FTourPoint& Current = Path->Points[SelectedPointIndex];
	const FTourPoint& Next = Path->Points[NextIndex];

	// Placed on the curve rather than on the chord, so inserting a point does not visibly change
	// the shape of the path.
	const FTourPathData PathData = Path->BuildPathData();
	FTourPoint Inserted = Current;
	Inserted.Location = UTourGeometryLibrary::EvaluatePositionAtKey(PathData, static_cast<float>(SelectedPointIndex) + 0.5f);

	const FVector Tangent = UTourGeometryLibrary::EvaluateTangentAtKey(PathData, static_cast<float>(SelectedPointIndex) + 0.5f) * 0.5f;
	Inserted.ArriveTangent = Tangent;
	Inserted.LeaveTangent  = Tangent;
	Inserted.PointType     = ESplinePointType::CurveCustomTangent;
	Inserted.DwellTime     = 0.0f;
	Inserted.Label         = FText::GetEmpty();
	Inserted.FocalLength   = (Current.FocalLength + Next.FocalLength) * 0.5f;
	Inserted.Aperture      = (Current.Aperture + Next.Aperture) * 0.5f;

	Path->Points.Insert(Inserted, SelectedPointIndex + 1);
	Path->SyncSplineFromPoints();
	Path->RebuildRailMeshes();

	SelectedPointIndex = SelectedPointIndex + 1;

	NotifyPropertyModified(Path, FindFProperty<FProperty>(ATourPath::StaticClass(), GET_MEMBER_NAME_CHECKED(ATourPath, Points)));
}

void FTourPathComponentVisualizer::DeleteSelectedPoint()
{
	ATourPath* Path = GetEditedPath();
	if (Path == nullptr || !Path->Points.IsValidIndex(SelectedPointIndex))
	{
		return;
	}

	if (Path->Points.Num() <= 2)
	{
		// Below two points there is no path to traverse, and every downstream query degenerates.
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteTourPointTransaction", "Delete Tour Point"));
	Path->Modify();

	Path->Points.RemoveAt(SelectedPointIndex);
	Path->SyncSplineFromPoints();
	Path->RebuildRailMeshes();

	SelectedPointIndex = FMath::Clamp(SelectedPointIndex, 0, Path->Points.Num() - 1);

	NotifyPropertyModified(Path, FindFProperty<FProperty>(ATourPath::StaticClass(), GET_MEMBER_NAME_CHECKED(ATourPath, Points)));
}

void FTourPathComponentVisualizer::SetSelectedPointType(TEnumAsByte<ESplinePointType::Type> NewType)
{
	ATourPath* Path = GetEditedPath();
	if (Path == nullptr || !Path->Points.IsValidIndex(SelectedPointIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetTourPointTypeTransaction", "Set Tour Point Type"));
	Path->Modify();

	Path->Points[SelectedPointIndex].PointType = NewType;
	Path->SyncSplineFromPoints();
	Path->RebuildRailMeshes();

	NotifyPropertyModified(Path, FindFProperty<FProperty>(ATourPath::StaticClass(), GET_MEMBER_NAME_CHECKED(ATourPath, Points)));
}

#undef LOCTEXT_NAMESPACE
