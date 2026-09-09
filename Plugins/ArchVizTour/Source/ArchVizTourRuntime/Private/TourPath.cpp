// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourPath.h"

#include "ArchVizTourLog.h"
#include "Components/SplineMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "TourGeometryLibrary.h"
#include "TourPathPreset.h"
#include "TourSplineComponent.h"

#define LOCTEXT_NAMESPACE "ArchVizTour"

namespace ArchVizTour::PathPrivate
{
	/** Collision channel used by SnapPointsToFloor. Visibility hits the same surfaces a camera sees. */
	static constexpr ECollisionChannel SnapTraceChannel = ECC_Visibility;

	/** Below this the spline has no meaningful length and every query degenerates. */
	static constexpr float MinUsefulLength = 1.e-2f;
}

ATourPath::ATourPath()
{
	// The path is passive: the subsystem samples it, it never samples itself.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Spline = CreateDefaultSubobject<UTourSplineComponent>(TEXT("Spline"));
	check(Spline != nullptr);
	SetRootComponent(Spline);
	Spline->SetMobility(EComponentMobility::Movable);

	// Two points is the minimum traversable path, and starting from a usable default means a
	// freshly dragged-in actor is immediately valid rather than logging warnings.
	FTourPoint Start;
	Start.Location      = FVector::ZeroVector;
	Start.LeaveTangent  = FVector(500.0, 0.0, 0.0);
	Start.ArriveTangent = Start.LeaveTangent;

	FTourPoint End;
	End.Location      = FVector(1000.0, 0.0, 0.0);
	End.LeaveTangent  = FVector(500.0, 0.0, 0.0);
	End.ArriveTangent = End.LeaveTangent;

	Points = { Start, End };
}

// ---------------------------------------------------------------------------
// AActor
// ---------------------------------------------------------------------------

void ATourPath::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// A Blueprint subclass can change Points after construction, and a level placed before the
	// spline cache existed has none at all; resyncing here guarantees the first evaluation of a
	// tour sees the right curve.
	SyncSplineFromPoints();
}

void ATourPath::PostLoad()
{
	Super::PostLoad();

	LookAtTargetCache.Reset();
	ReportedMissingTargets.Reset();
}

#if WITH_EDITOR

void ATourPath::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Every authored property either changes the curve or the rail, and both rebuilds are cheap
	// enough that discriminating between them would only add a list to keep in sync.
	LookAtTargetCache.Reset();
	ReportedMissingTargets.Reset();
	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	// Edits to a member of an element of Points arrive here rather than in PostEditChangeProperty.
	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Points are actor-local, so moving the actor moves the whole path; only the rail's world
	// transforms need refreshing, and only once the drag settles.
	if (bFinished)
	{
		RebuildRailMeshes();
	}
}

#endif // WITH_EDITOR

// ---------------------------------------------------------------------------
// Data conversion
// ---------------------------------------------------------------------------

FTourPathData ATourPath::BuildPathData() const
{
	FTourPathData Data;
	Data.Points               = Points;
	Data.bClosedLoop          = bClosedLoop;
	Data.DefaultSpeed         = DefaultSpeed;
	Data.ArcGenerationParams  = GenerationParams;
	Data.SplineWorldTransform = GetActorTransform();
	return Data;
}

void ATourPath::ApplyPathData(const FTourPathData& InPathData, bool bApplyTransform)
{
#if WITH_EDITOR
	Modify();
#endif

	Points           = InPathData.Points;
	bClosedLoop      = InPathData.bClosedLoop;
	DefaultSpeed     = InPathData.DefaultSpeed;
	GenerationParams = InPathData.ArcGenerationParams;

	if (bApplyTransform)
	{
		SetActorTransform(InPathData.SplineWorldTransform);
	}

	LookAtTargetCache.Reset();
	ReportedMissingTargets.Reset();

	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::SyncSplineFromPoints()
{
	if (!IsValid(Spline))
	{
		return;
	}

	Spline->ClearSplinePoints(/*bUpdateSpline*/ false);

	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FTourPoint& Point = Points[Index];

		FSplinePoint SplinePoint;
		SplinePoint.InputKey      = static_cast<float>(Index);
		SplinePoint.Position      = Point.Location;
		SplinePoint.ArriveTangent = Point.ArriveTangent;
		SplinePoint.LeaveTangent  = Point.LeaveTangent;
		SplinePoint.Rotation      = Point.Rotation;
		SplinePoint.Scale         = FVector::OneVector;
		SplinePoint.Type          = Point.PointType;

		Spline->AddPoint(SplinePoint, /*bUpdateSpline*/ false);
	}

	Spline->SetClosedLoop(bClosedLoop, /*bUpdateSpline*/ false);
	Spline->ReparamStepsPerSegment = FMath::Clamp(ReparamStepsPerSegment, 4, 256);

	// One update at the end rather than per point: UpdateSpline rebuilds the whole
	// reparameterisation table, so calling it inside the loop is quadratic.
	Spline->UpdateSpline();
}

void ATourPath::SyncPointsFromSpline()
{
	if (!IsValid(Spline))
	{
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	const int32 SplinePointCount = Spline->GetNumberOfSplinePoints();

	TArray<FTourPoint> Rebuilt;
	Rebuilt.Reserve(SplinePointCount);

	for (int32 Index = 0; Index < SplinePointCount; ++Index)
	{
		// Camera data has no counterpart in the spline, so it is carried across by index and
		// only defaulted for points the spline gained.
		FTourPoint Point = Points.IsValidIndex(Index) ? Points[Index] : FTourPoint();

		Point.Location      = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
		Point.ArriveTangent = Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local);
		Point.LeaveTangent  = Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local);
		Point.PointType     = Spline->GetSplinePointType(Index);

		Rebuilt.Add(MoveTemp(Point));
	}

	Points = MoveTemp(Rebuilt);
	bClosedLoop = Spline->IsClosedLoop();

	// The curve is now hand-edited, so the stored generator parameters no longer describe it.
	GenerationParams.GeneratorType = ETourGeneratorType::None;

	RebuildRailMeshes();
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

bool ATourPath::IsTraversable() const
{
	return IsValid(Spline)
		&& Spline->GetNumberOfSplinePoints() >= 2
		&& Spline->GetSplineLength() > ArchVizTour::PathPrivate::MinUsefulLength;
}

float ATourPath::GetPathLength() const
{
	return IsTraversable() ? Spline->GetSplineLength() : 0.0f;
}

float ATourPath::GetDistanceAtInputKey(float InputKey) const
{
	if (!IsTraversable())
	{
		return 0.0f;
	}

	return Spline->GetDistanceAlongSplineAtSplineInputKey(NormalizeInputKey(InputKey));
}

float ATourPath::NormalizeInputKey(float InputKey) const
{
	if (!IsValid(Spline))
	{
		return 0.0f;
	}

	const int32 PointCount = Spline->GetNumberOfSplinePoints();
	if (PointCount < 2)
	{
		return 0.0f;
	}

	const float MaxKey = static_cast<float>(bClosedLoop ? PointCount : PointCount - 1);

	if (bClosedLoop)
	{
		InputKey = FMath::Fmod(InputKey, MaxKey);
		if (InputKey < 0.0f)
		{
			InputKey += MaxKey;
		}
		return InputKey;
	}

	return FMath::Clamp(InputKey, 0.0f, MaxKey);
}

bool ATourPath::EvaluateAtDistance(float Distance, FTourCameraState& OutState) const
{
	SCOPE_CYCLE_COUNTER(STAT_ArchVizTour_PathEvaluate);

	OutState = FTourCameraState();

	if (!IsTraversable())
	{
		return false;
	}

	const float Length = Spline->GetSplineLength();
	Distance = bClosedLoop
		? FMath::Fmod(FMath::Fmod(Distance, Length) + Length, Length)
		: FMath::Clamp(Distance, 0.0f, Length);

	OutState.Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

	// GetInputKeyValueAtDistanceAlongSpline is the 5.3+ replacement for the deprecated
	// GetInputKeyAtDistanceAlongSpline; using it keeps the build warning-free on 5.4 and 5.5.
	const float InputKey = Spline->GetInputKeyValueAtDistanceAlongSpline(Distance);

	SampleCameraAttributes(InputKey, OutState);

	if (!OutState.bValid)
	{
		// No authored rotation and no LookAt target: follow the curve.
		const FVector Direction = Spline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		if (!Direction.IsNearlyZero())
		{
			OutState.Rotation = Direction.Rotation();
		}
	}

	OutState.bValid = true;
	return true;
}

bool ATourPath::EvaluateAtInputKey(float InputKey, FTourCameraState& OutState) const
{
	if (!IsTraversable())
	{
		OutState = FTourCameraState();
		return false;
	}

	return EvaluateAtDistance(GetDistanceAtInputKey(InputKey), OutState);
}

void ATourPath::SampleCameraAttributes(float InputKey, FTourCameraState& OutState) const
{
	if (Points.Num() == 0)
	{
		return;
	}

	const int32 PointCount = Points.Num();
	const float NormalizedKey = NormalizeInputKey(InputKey);

	const int32 IndexA = FMath::Clamp(FMath::FloorToInt(NormalizedKey), 0, PointCount - 1);
	const int32 IndexB = bClosedLoop ? ((IndexA + 1) % PointCount) : FMath::Min(IndexA + 1, PointCount - 1);
	const float Alpha = FMath::Clamp(NormalizedKey - static_cast<float>(IndexA), 0.0f, 1.0f);

	const FTourPoint& A = Points[IndexA];
	const FTourPoint& B = Points[IndexB];

	OutState.FocalLength = FMath::Lerp(A.FocalLength, B.FocalLength, Alpha);
	OutState.Aperture    = FMath::Lerp(A.Aperture, B.Aperture, Alpha);

	if (A.ManualFocusDistance > 0.0f && B.ManualFocusDistance > 0.0f)
	{
		OutState.FocusDistance = FMath::Lerp(A.ManualFocusDistance, B.ManualFocusDistance, Alpha);
	}
	else
	{
		OutState.FocusDistance = (Alpha < 0.5f) ? A.ManualFocusDistance : B.ManualFocusDistance;
	}

	// Resolution order, highest priority first. bValid doubles as "rotation has been resolved"
	// inside this helper; EvaluateAtDistance falls back to the tangent when it is still false.
	OutState.bValid = false;

	if (A.bUseExplicitRotation || B.bUseExplicitRotation)
	{
		// Interpolating as quaternions avoids the 359 -> 1 degree wrap that Euler lerping
		// turns into a full backwards spin.
		const FQuat QuatA = (A.bUseExplicitRotation ? A.Rotation : B.Rotation).Quaternion();
		const FQuat QuatB = (B.bUseExplicitRotation ? B.Rotation : A.Rotation).Quaternion();
		const FQuat Local = FQuat::Slerp(QuatA, QuatB, Alpha);

		// Points are authored in actor-local space, so their rotations are too.
		OutState.Rotation = (GetActorTransform().GetRotation() * Local).Rotator();
		OutState.bValid = true;
		return;
	}

	const FTourPoint& LookAtSource = (Alpha < 0.5f) ? A : B;
	if (LookAtSource.bUseLookAtTarget && LookAtSource.LookAtTargetName != NAME_None)
	{
		if (const AActor* Target = ResolveLookAtTarget(LookAtSource.LookAtTargetName))
		{
			const FVector ToTarget = Target->GetActorLocation() - OutState.Location;
			if (!ToTarget.IsNearlyZero())
			{
				OutState.Rotation = ToTarget.Rotation();
				OutState.bValid = true;
			}
		}
	}
}

float ATourPath::GetSpeedAtDistance(float Distance) const
{
	const float Fallback = FMath::Max(DefaultSpeed, UE_KINDA_SMALL_NUMBER);

	if (!IsTraversable() || Points.Num() == 0)
	{
		return Fallback;
	}

	const float InputKey = NormalizeInputKey(Spline->GetInputKeyValueAtDistanceAlongSpline(Distance));

	const int32 PointCount = Points.Num();
	const int32 IndexA = FMath::Clamp(FMath::FloorToInt(InputKey), 0, PointCount - 1);
	const int32 IndexB = bClosedLoop ? ((IndexA + 1) % PointCount) : FMath::Min(IndexA + 1, PointCount - 1);
	const float Alpha = FMath::Clamp(InputKey - static_cast<float>(IndexA), 0.0f, 1.0f);

	const float SpeedA = Points[IndexA].Speed > 0.0f ? Points[IndexA].Speed : Fallback;
	const float SpeedB = Points[IndexB].Speed > 0.0f ? Points[IndexB].Speed : Fallback;

	return FMath::Max(FMath::Lerp(SpeedA, SpeedB, Alpha), UE_KINDA_SMALL_NUMBER);
}

AActor* ATourPath::ResolveLookAtTarget(FName TargetName) const
{
	if (TargetName == NAME_None)
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<AActor>* Cached = LookAtTargetCache.Find(TargetName))
	{
		if (AActor* Resolved = Cached->Get())
		{
			return Resolved;
		}

		// The cached actor was destroyed; drop the entry and search again rather than
		// returning null forever.
		LookAtTargetCache.Remove(TargetName);
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// The scan runs once per name per path, not per frame; the result is cached above.
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		if (Actor->GetFName() == TargetName || Actor->ActorHasTag(TargetName))
		{
			LookAtTargetCache.Add(TargetName, Actor);
			ReportedMissingTargets.Remove(TargetName);
			return Actor;
		}
	}

	if (!ReportedMissingTargets.Contains(TargetName))
	{
		ReportedMissingTargets.Add(TargetName);
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path '%s': no actor named or tagged '%s' in the level. Falling back to the spline tangent."),
			*GetName(), *TargetName.ToString());
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// Rail visual
// ---------------------------------------------------------------------------

void ATourPath::RebuildRailMeshes()
{
	SCOPE_CYCLE_COUNTER(STAT_ArchVizTour_RailRebuild);

	if (!IsValid(Spline))
	{
		return;
	}

	// Reconstruction can destroy pooled components behind our back (a Blueprint subclass's
	// construction script rerun does exactly that) and can equally leave orphans still attached
	// to the spline. Rebuilding the pool from what actually exists makes the pooling correct
	// regardless of who did what, and is far cheaper than recreating every segment.
	RailMeshComponents.RemoveAll([](const TObjectPtr<USplineMeshComponent>& Component)
	{
		return !IsValid(Component);
	});

	{
		TArray<USceneComponent*> Children;
		Spline->GetChildrenComponents(/*bIncludeAllDescendants*/ false, Children);
		for (USceneComponent* Child : Children)
		{
			if (USplineMeshComponent* Adopted = Cast<USplineMeshComponent>(Child))
			{
				RailMeshComponents.AddUnique(Adopted);
			}
		}
	}

	const bool bWantRail = bShowRailMesh && RailMesh != nullptr && IsTraversable();

	const int32 SegmentsPerSplineSegment = FMath::Clamp(RailSegmentsPerSplineSegment, 1, 32);
	const int32 DesiredCount = bWantRail
		? Spline->GetNumberOfSplineSegments() * SegmentsPerSplineSegment
		: 0;

	while (RailMeshComponents.Num() < DesiredCount)
	{
		USplineMeshComponent* Component = NewObject<USplineMeshComponent>(this, NAME_None, RF_Transient);
		check(Component != nullptr);

		Component->SetMobility(EComponentMobility::Movable);
		Component->SetupAttachment(Spline);
		// The rail is a wayfinding aid, never gameplay geometry: no collision, no shadow cost.
		Component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Component->SetCastShadow(false);
		Component->RegisterComponent();

		RailMeshComponents.Add(Component);
	}

	const float SplineLength = bWantRail ? Spline->GetSplineLength() : 0.0f;
	const float SegmentLength = (DesiredCount > 0) ? SplineLength / static_cast<float>(DesiredCount) : 0.0f;

	for (int32 Index = 0; Index < RailMeshComponents.Num(); ++Index)
	{
		USplineMeshComponent* Component = RailMeshComponents[Index];
		if (!IsValid(Component))
		{
			continue;
		}

		if (Index >= DesiredCount)
		{
			// Surplus components are parked, not destroyed: destroying and recreating them on
			// every property edit costs a render-state rebuild per segment and shows up as
			// editor hitching on a long path.
			Component->SetVisibility(false, /*bPropagateToChildren*/ false);
			Component->SetHiddenInGame(true);
			continue;
		}

		const float StartDistance = SegmentLength * static_cast<float>(Index);
		const float EndDistance   = SegmentLength * static_cast<float>(Index + 1);

		const FVector StartPosition = Spline->GetLocationAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::Local);
		const FVector EndPosition   = Spline->GetLocationAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::Local);

		// USplineMeshComponent expects tangents scaled to the segment, not unit vectors; using
		// the raw spline tangent would over-stretch every segment by the segment count.
		const FVector StartTangent = Spline->GetTangentAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::Local).GetSafeNormal() * SegmentLength;
		const FVector EndTangent   = Spline->GetTangentAtDistanceAlongSpline(EndDistance, ESplineCoordinateSpace::Local).GetSafeNormal() * SegmentLength;

		Component->SetStaticMesh(RailMesh);
		if (RailMaterial != nullptr)
		{
			Component->SetMaterial(0, RailMaterial);
		}

		Component->SetForwardAxis(RailForwardAxis, /*bUpdateMesh*/ false);
		Component->SetStartScale(RailScale, /*bUpdateMesh*/ false);
		Component->SetEndScale(RailScale, /*bUpdateMesh*/ false);
		Component->SetStartAndEnd(StartPosition, StartTangent, EndPosition, EndTangent, /*bUpdateMesh*/ true);

		Component->SetVisibility(true, /*bPropagateToChildren*/ false);
		Component->SetHiddenInGame(false);
	}
}

void ATourPath::SetRailVisible(bool bVisible)
{
	for (USplineMeshComponent* Component : RailMeshComponents)
	{
		if (IsValid(Component))
		{
			Component->SetHiddenInGame(!bVisible);
			Component->SetVisibility(bVisible, /*bPropagateToChildren*/ false);
		}
	}
}

// ---------------------------------------------------------------------------
// Authoring actions
//
// CallInEditor buttons are invoked by FObjectDetails::ExecuteEditorFunction, which already
// opens a transaction around the call, so Modify() alone is enough to make these undoable.
// FScopedTransaction itself lives in UnrealEd and must not be referenced from a Runtime module.
// ---------------------------------------------------------------------------

void ATourPath::GenerateArcRail()
{
#if WITH_EDITOR
	Modify();
#endif

	FTourArcGenerationParams Params = GenerationParams;
	if (Params.GeneratorType == ETourGeneratorType::None)
	{
		// The button has to do something useful on a path that was never generated; an arc with
		// the stored radius and sweep is the least surprising default.
		Params.GeneratorType = ETourGeneratorType::Arc;
	}

	FTourPathData Generated;

	if (Params.GeneratorType == ETourGeneratorType::Orbit && Params.OrbitTargetName != NAME_None)
	{
		if (const AActor* Target = ResolveLookAtTarget(Params.OrbitTargetName))
		{
			// GenerateOrbitAround works in world space because it aims at a world-space actor;
			// the result is pulled back into actor-local space, which is where Points live.
			Generated = UTourGeometryLibrary::GenerateOrbitAround(
				Target, Params.Radius, Params.HeightDelta, Params.SweepAngleDeg, Params.PointCount, Params.StartAngleDeg);

			const FTransform WorldToLocal = GetActorTransform().Inverse();
			for (FTourPoint& Point : Generated.Points)
			{
				Point.Location      = WorldToLocal.TransformPosition(Point.Location);
				Point.ArriveTangent = WorldToLocal.TransformVector(Point.ArriveTangent);
				Point.LeaveTangent  = WorldToLocal.TransformVector(Point.LeaveTangent);
				Point.Rotation      = (WorldToLocal.GetRotation() * Point.Rotation.Quaternion()).Rotator();
			}
		}
		else
		{
			UE_LOG(LogArchVizTour, Warning,
				TEXT("Tour path '%s': orbit target '%s' not found; generating a plain arc about the stored centre instead."),
				*GetName(), *Params.OrbitTargetName.ToString());
			Generated = UTourGeometryLibrary::GenerateFromParams(Params, FTransform::Identity);
		}
	}
	else
	{
		// Points are actor-local, so generation runs at identity and the actor transform places it.
		Generated = UTourGeometryLibrary::GenerateFromParams(Params, FTransform::Identity);
	}

	if (Generated.Points.Num() < 2)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path '%s': generation produced %d points; leaving the existing path untouched."),
			*GetName(), Generated.Points.Num());
		return;
	}

	// Carry the authored camera settings of the first point onto every generated point, so a
	// regenerate does not silently reset focal length and speed back to struct defaults.
	if (Points.Num() > 0)
	{
		const FTourPoint& Template = Points[0];
		for (FTourPoint& Point : Generated.Points)
		{
			Point.FocalLength         = Template.FocalLength;
			Point.Aperture            = Template.Aperture;
			Point.ManualFocusDistance = Template.ManualFocusDistance;
			Point.Speed               = Template.Speed;
			Point.EaseIn              = Template.EaseIn;
			Point.EaseOut             = Template.EaseOut;
		}
	}

	Points           = MoveTemp(Generated.Points);
	bClosedLoop      = Generated.bClosedLoop;
	GenerationParams = Generated.ArcGenerationParams;

	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::MirrorPath()
{
#if WITH_EDITOR
	Modify();
#endif

	FTourPathData Data = BuildPathData();
	// Points are actor-local, so the mirror plane is the actor's local YZ plane: origin at the
	// actor, normal along local +X.
	Data = UTourGeometryLibrary::MirrorPath(Data, FVector::ZeroVector, FVector::ForwardVector);

	Points           = MoveTemp(Data.Points);
	GenerationParams = Data.ArcGenerationParams;

	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::ReversePath()
{
#if WITH_EDITOR
	Modify();
#endif

	const FTourPathData Reversed = UTourGeometryLibrary::ReversePath(BuildPathData());
	Points = Reversed.Points;

	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::SnapPointsToFloor()
{
	using namespace ArchVizTour::PathPrivate;

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Tour path '%s': SnapPointsToFloor needs a world."), *GetName());
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TourPathSnapToFloor), /*bTraceComplex*/ true, this);
	QueryParams.AddIgnoredActor(this);

	const FTransform ActorTransform = GetActorTransform();
	int32 MissCount = 0;

	for (FTourPoint& Point : Points)
	{
		const FVector WorldPoint = ActorTransform.TransformPosition(Point.Location);
		const FVector TraceStart = WorldPoint + FVector(0.0, 0.0, SnapTraceStartHeight);
		const FVector TraceEnd   = TraceStart - FVector(0.0, 0.0, SnapTraceStartHeight + SnapTraceLength);

		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, SnapTraceChannel, QueryParams))
		{
			const FVector SnappedWorld = Hit.ImpactPoint + FVector(0.0, 0.0, SnapHeightOffset);
			Point.Location = ActorTransform.InverseTransformPosition(SnappedWorld);
		}
		else
		{
			// Leaving an unhit point where it is beats teleporting it to the trace end, which
			// would drop it through the floor of the model.
			++MissCount;
		}
	}

	if (MissCount > 0)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path '%s': %d of %d points found no surface below them and were left in place."),
			*GetName(), MissCount, Points.Num());
	}

	// Snapping breaks whatever analytic shape the path had.
	GenerationParams.GeneratorType = ETourGeneratorType::None;

	SyncSplineFromPoints();
	RebuildRailMeshes();
}

void ATourPath::FlattenToHeight()
{
#if WITH_EDITOR
	Modify();
#endif

	const FTourPathData Flattened = UTourGeometryLibrary::FlattenToHeight(BuildPathData(), FlattenHeight);
	Points = Flattened.Points;

	SyncSplineFromPoints();
	RebuildRailMeshes();
}

// ---------------------------------------------------------------------------
// Preset binding
// ---------------------------------------------------------------------------

void ATourPath::SaveToPreset()
{
	if (LinkedPreset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path '%s': no Linked Preset assigned; nothing to save to."), *GetName());
		return;
	}

	SaveToPresetAsset(LinkedPreset);
}

void ATourPath::LoadFromPreset()
{
	if (LinkedPreset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path '%s': no Linked Preset assigned; nothing to load from."), *GetName());
		return;
	}

	LoadFromPresetAsset(LinkedPreset);
}

bool ATourPath::SaveToPresetAsset(UTourPathPreset* Preset)
{
	if (Preset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Tour path '%s': SaveToPresetAsset called with a null preset."), *GetName());
		return false;
	}

#if WITH_EDITOR
	// Modify() before the write registers the pre-change state with the open transaction, and
	// MarkPackageDirty() after it is what makes the editor offer to save the asset.
	Preset->Modify();
#endif

	Preset->PathData     = BuildPathData();
	Preset->SchemaVersion = UTourPathPreset::CurrentSchemaVersion;

	if (Preset->DisplayName.IsEmpty())
	{
		Preset->DisplayName = FText::FromString(GetActorNameOrLabel());
	}

#if WITH_EDITOR
	Preset->MarkPackageDirty();
#endif

	UE_LOG(LogArchVizTour, Log, TEXT("Tour path '%s' saved to preset '%s' (%d points)."),
		*GetName(), *Preset->GetName(), Preset->PathData.Points.Num());
	return true;
}

bool ATourPath::LoadFromPresetAsset(const UTourPathPreset* Preset)
{
	if (Preset == nullptr)
	{
		UE_LOG(LogArchVizTour, Warning, TEXT("Tour path '%s': LoadFromPresetAsset called with a null preset."), *GetName());
		return false;
	}

	if (Preset->PathData.Points.Num() < 2)
	{
		UE_LOG(LogArchVizTour, Warning,
			TEXT("Tour path '%s': preset '%s' holds %d points, which is not a traversable path; ignoring."),
			*GetName(), *Preset->GetName(), Preset->PathData.Points.Num());
		return false;
	}

	// The preset's own world transform is deliberately not applied: the actor's placement in
	// this level wins, so the same preset can be reused at several locations.
	ApplyPathData(Preset->PathData, /*bApplyTransform*/ false);

	UE_LOG(LogArchVizTour, Log, TEXT("Tour path '%s' loaded from preset '%s' (%d points)."),
		*GetName(), *Preset->GetName(), Points.Num());
	return true;
}

#undef LOCTEXT_NAMESPACE
