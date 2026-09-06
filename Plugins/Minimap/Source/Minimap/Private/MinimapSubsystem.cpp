#include "MinimapSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MinimapFunctionLibrary.h"
#include "MinimapModule.h"
#include "MinimapTrackedComponent.h"
#include "MinimapViewComponent.h"
#include "Stats/Stats.h"

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

bool UMinimapSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}

	// Game/PIE for actual play; Editor so AMinimapBoundsVolume's "Apply Calibration"
	// button works on a level that is not being simulated.
	switch (World->WorldType)
	{
	case EWorldType::Game:
	case EWorldType::PIE:
	case EWorldType::Editor:
		return true;
	default:
		return false;
	}
}

void UMinimapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Start invalid on purpose. Until a bounds volume (or explicit SetCalibration call)
	// supplies real numbers, projecting would be guesswork - so the system stays quiet
	// instead of silently using a made-up extent.
	bCalibrationValid = false;
	bCalibrationDirty = true;
	TimeAccumulator = 0.0f;

	UE_LOG(LogMinimap, Log, TEXT("UMinimapSubsystem initialized for world '%s'."), *GetNameSafe(GetWorld()));
}

void UMinimapSubsystem::Deinitialize()
{
	Markers.Reset();
	Views.Reset();
	SnapshotScratch.Reset();

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Ticking
// ---------------------------------------------------------------------------

bool UMinimapSubsystem::IsTickable() const
{
	if (!Super::IsTickable() || !bMinimapEnabled)
	{
		return false;
	}

	// Never burn time in the editor world; only Game and PIE actually run the loop.
	const UWorld* World = GetWorld();
	if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
	{
		return false;
	}

	// Nothing to project for.
	return Views.Num() > 0;
}

TStatId UMinimapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMinimapSubsystem, STATGROUP_Tickables);
}

void UMinimapSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bMinimapEnabled)
	{
		return;
	}

	// Fixed-cadence throttle. Accumulate real time and only do work when the interval
	// elapses, so the cost is independent of frame rate.
	if (TickInterval > 0.0f)
	{
		TimeAccumulator += DeltaTime;
		if (TimeAccumulator < TickInterval)
		{
			return;
		}
		TimeAccumulator = 0.0f;
	}

	UpdateAllViews(/*bForceFullUpdate=*/false);
}

UMinimapSubsystem* UMinimapSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	return World ? World->GetSubsystem<UMinimapSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------

bool UMinimapSubsystem::SetCalibration(const FMinimapCalibration& NewCalibration)
{
	FString Reason;
	if (!NewCalibration.IsValidCalibration(&Reason))
	{
		// Refuse rather than accept: keeping the previous good calibration is far less
		// disruptive than propagating a degenerate one into every widget transform.
		UE_LOG(LogMinimap, Warning,
			TEXT("SetCalibration rejected: %s Previous calibration retained (valid=%s)."),
			*Reason, bCalibrationValid ? TEXT("true") : TEXT("false"));
		return false;
	}

	if (bCalibrationValid && Calibration == NewCalibration)
	{
		return true; // No-op; do not churn delegates.
	}

	Calibration = NewCalibration;
	bCalibrationValid = true;
	bCalibrationDirty = true;

	UE_LOG(LogMinimap, Log,
		TEXT("Minimap calibration applied. Center=(%.1f, %.1f) Extent=(%.1f, %.1f) MapYaw=%.1f "
		     "Zoom=%.2f Z=[%.1f..%.1f] Aspect=%s Shape=%s SwapUV=%s InvertU=%s InvertV=%s"),
		Calibration.WorldCenter.X, Calibration.WorldCenter.Y,
		Calibration.WorldExtent.X, Calibration.WorldExtent.Y,
		Calibration.MapYaw, Calibration.Zoom, Calibration.MinZ, Calibration.MaxZ,
		Calibration.bPreserveAspectRatio ? TEXT("preserved") : TEXT("raw"),
		Calibration.bCircularMap ? TEXT("circular") : TEXT("rectangular"),
		Calibration.bSwapUV   ? TEXT("yes") : TEXT("no"),
		Calibration.bInvertU  ? TEXT("yes") : TEXT("no"),
		Calibration.bInvertV  ? TEXT("yes") : TEXT("no"));

	OnCalibrationChanged.Broadcast(Calibration);
	return true;
}

void UMinimapSubsystem::SetZoom(float NewZoom)
{
	const float Clamped = FMath::Max(NewZoom, 0.01f);
	if (FMath::IsNearlyEqual(Calibration.Zoom, Clamped))
	{
		return;
	}

	Calibration.Zoom = Clamped;
	bCalibrationDirty = true;
	OnCalibrationChanged.Broadcast(Calibration);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

void UMinimapSubsystem::RegisterMarker(UMinimapTrackedComponent* Marker)
{
	if (!IsValid(Marker))
	{
		return;
	}

	const TWeakObjectPtr<UMinimapTrackedComponent> WeakMarker(Marker);
	if (Markers.Contains(WeakMarker))
	{
		return; // Idempotent - double registration must not duplicate the marker.
	}

	Markers.Add(WeakMarker);
	OnMarkerRegistered.Broadcast(Marker);
}

void UMinimapSubsystem::UnregisterMarker(UMinimapTrackedComponent* Marker)
{
	if (!Marker)
	{
		return;
	}

	// No IsValid() gate: this is routinely called from EndPlay while the component is
	// already being torn down, and the entry still has to come out of the registry.
	const int32 Removed = Markers.RemoveAll(
		[Marker](const TWeakObjectPtr<UMinimapTrackedComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Marker;
		});

	if (Removed > 0)
	{
		OnMarkerUnregistered.Broadcast(Marker);
	}
}

void UMinimapSubsystem::RegisterView(UMinimapViewComponent* View)
{
	if (!IsValid(View))
	{
		return;
	}

	const TWeakObjectPtr<UMinimapViewComponent> WeakView(View);
	if (!Views.Contains(WeakView))
	{
		Views.Add(WeakView);
	}
}

void UMinimapSubsystem::UnregisterView(UMinimapViewComponent* View)
{
	if (!View)
	{
		return;
	}

	Views.RemoveAll(
		[View](const TWeakObjectPtr<UMinimapViewComponent>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == View;
		});
}

UMinimapViewComponent* UMinimapSubsystem::GetPrimaryView() const
{
	UMinimapViewComponent* FirstActive = nullptr;

	for (const TWeakObjectPtr<UMinimapViewComponent>& WeakView : Views)
	{
		UMinimapViewComponent* View = WeakView.Get();
		if (!IsValid(View) || !View->IsViewActive())
		{
			continue;
		}

		if (View->bPrimaryView)
		{
			return View;
		}

		if (!FirstActive)
		{
			FirstActive = View;
		}
	}

	// Fall back to the first active view so single-view projects work with no flag set.
	return FirstActive;
}

TArray<UMinimapTrackedComponent*> UMinimapSubsystem::GetRegisteredMarkers() const
{
	TArray<UMinimapTrackedComponent*> Result;
	Result.Reserve(Markers.Num());

	for (const TWeakObjectPtr<UMinimapTrackedComponent>& WeakMarker : Markers)
	{
		if (UMinimapTrackedComponent* Marker = WeakMarker.Get())
		{
			Result.Add(Marker);
		}
	}
	return Result;
}

// ---------------------------------------------------------------------------
// Update control
// ---------------------------------------------------------------------------

void UMinimapSubsystem::SetTickInterval(float NewInterval)
{
	TickInterval = FMath::Max(NewInterval, 0.0f);
	TimeAccumulator = 0.0f;
}

void UMinimapSubsystem::SetMinimapEnabled(bool bEnabled)
{
	if (bMinimapEnabled == bEnabled)
	{
		return;
	}

	bMinimapEnabled = bEnabled;
	TimeAccumulator = 0.0f;

	// Re-enabling must not present stale positions from before the pause.
	if (bEnabled)
	{
		ForceUpdate();
	}
}

void UMinimapSubsystem::ForceUpdate()
{
	UpdateAllViews(/*bForceFullUpdate=*/true);
}

// ---------------------------------------------------------------------------
// The batched update
// ---------------------------------------------------------------------------

void UMinimapSubsystem::CompactRegistries()
{
	Markers.RemoveAll([](const TWeakObjectPtr<UMinimapTrackedComponent>& Entry) { return !Entry.IsValid(); });
	Views.RemoveAll([](const TWeakObjectPtr<UMinimapViewComponent>& Entry) { return !Entry.IsValid(); });
}

void UMinimapSubsystem::UpdateAllViews(bool bForceFullUpdate)
{
	// Dead weak pointers are dropped before anything is dereferenced.
	CompactRegistries();

	if (!bCalibrationValid || Views.Num() == 0)
	{
		return;
	}

	const UMinimapViewComponent* PrimaryView = GetPrimaryView();
	const bool bCalibrationChanged = bCalibrationDirty || bForceFullUpdate;

	// Copy because a delegate fired mid-loop could legally register or unregister a view;
	// iterating the live array would then invalidate the iterator.
	TArray<TWeakObjectPtr<UMinimapViewComponent>> ViewsSnapshot = Views;

	for (const TWeakObjectPtr<UMinimapViewComponent>& WeakView : ViewsSnapshot)
	{
		UMinimapViewComponent* View = WeakView.Get();
		if (!IsValid(View) || !View->IsViewActive())
		{
			// Covers both destroyed views and views whose widget is off screen.
			continue;
		}

		const bool bViewDirty = View->RefreshViewState(Calibration, bCalibrationChanged);

		if (!View->IsProjectionValid())
		{
			continue;
		}

		const bool bIsPrimary = (View == PrimaryView);
		UpdateMarkersForView(*View, bViewDirty, bIsPrimary, bForceFullUpdate);
	}

	bCalibrationDirty = false;
}

bool UMinimapSubsystem::UpdateMarkersForView(UMinimapViewComponent& View, bool bViewDirty, bool bIsPrimary, bool bForceFullUpdate)
{
	const bool bFullRebuild = bViewDirty || bForceFullUpdate;

	SnapshotScratch.Reset(Markers.Num());
	bool bAnythingChanged = bFullRebuild;

	for (const TWeakObjectPtr<UMinimapTrackedComponent>& WeakMarker : Markers)
	{
		UMinimapTrackedComponent* Marker = WeakMarker.Get();
		if (!IsValid(Marker) || !Marker->IsMarkerEnabled())
		{
			continue;
		}

		const AActor* Owner = Marker->GetOwner();
		if (!IsValid(Owner))
		{
			continue;
		}

		const FVector WorldLocation = Owner->GetActorLocation();
		const float WorldYaw = static_cast<float>(Owner->GetActorRotation().Yaw);

		// The dirty check must be consumed for every marker on every pass, otherwise a
		// marker's latched transform would drift out of sync with reality.
		const bool bMarkerDirty = Marker->CheckAndConsumeDirty(WorldLocation, WorldYaw);

		// Reuse the cached snapshot only for the primary view: a marker's projected
		// position differs per view, and the component only caches one of them.
		const bool bCanReuseCache = bIsPrimary && !bFullRebuild && !bMarkerDirty && Marker->HasSnapshot();

		if (bCanReuseCache)
		{
			SnapshotScratch.Add(Marker->GetLastSnapshot());
			continue;
		}

		FMinimapMarkerSnapshot Snapshot = BuildSnapshot(*Marker, View, WorldLocation, WorldYaw);

		if (bIsPrimary)
		{
			// Broadcasts only the component delegates whose state actually changed.
			Marker->ApplySnapshot(Snapshot);
		}

		SnapshotScratch.Add(MoveTemp(Snapshot));
		bAnythingChanged = true;
	}

	if (!bAnythingChanged)
	{
		// Nothing moved and the view did not move: skip the broadcast entirely so the
		// widget layer does no relayout work.
		return false;
	}

	// Highest priority last so a canvas that draws in array order puts it on top.
	SnapshotScratch.StableSort(
		[](const FMinimapMarkerSnapshot& A, const FMinimapMarkerSnapshot& B)
		{
			return A.Priority < B.Priority;
		});

	// Per-view budget: keep the HIGHEST priority markers, which live at the tail.
	if (View.MaxMarkersPerView > 0 && SnapshotScratch.Num() > View.MaxMarkersPerView)
	{
		const int32 Excess = SnapshotScratch.Num() - View.MaxMarkersPerView;
		SnapshotScratch.RemoveAt(0, Excess, EAllowShrinking::No);
	}

	TArray<FMinimapMarkerSnapshot>& ViewSnapshots = View.GetMutableSnapshots();
	ViewSnapshots = SnapshotScratch;

	View.BroadcastViewUpdated();
	return true;
}

FMinimapMarkerSnapshot UMinimapSubsystem::BuildSnapshot(
	UMinimapTrackedComponent& Marker,
	const UMinimapViewComponent& View,
	const FVector& WorldLocation,
	float WorldYaw) const
{
	const FMinimapProjectionContext& Context = View.GetProjectionContext();

	FMinimapMarkerSnapshot Snapshot;
	Snapshot.Tracked  = &Marker;
	Snapshot.Priority = Marker.Priority;

	// --- Projection (no trigonometry: the context is pre-baked) ------------
	Snapshot.Normalized = Context.Project(WorldLocation);

	// --- Height -----------------------------------------------------------
	Snapshot.HeightRatio  = Context.GetHeightRatio(static_cast<float>(WorldLocation.Z));
	Snapshot.HeightOffset = static_cast<float>(WorldLocation.Z) - Context.AnchorZ;

	// --- Distance culling -------------------------------------------------
	const FVector2D WorldXY(WorldLocation.X, WorldLocation.Y);
	Snapshot.DistanceToAnchor = static_cast<float>(FVector2D::Distance(WorldXY, Context.Anchor));

	bool bVisible = Marker.IsMarkerVisible();

	if (bVisible && Marker.MaxTrackDistance > 0.0f && Snapshot.DistanceToAnchor > Marker.MaxTrackDistance)
	{
		bVisible = false;
	}

	// --- Height filtering -------------------------------------------------
	if (bVisible && Marker.bUseHeightFilter)
	{
		if (Snapshot.HeightOffset > Marker.HeightFilterAbove ||
		    Snapshot.HeightOffset < -Marker.HeightFilterBelow)
		{
			bVisible = false;
		}
	}

	// --- Out of bounds, with hysteresis -----------------------------------
	const float ShapeMagnitude = UMinimapFunctionLibrary::GetShapeMagnitude(Snapshot.Normalized, Context.bCircularMap);
	const bool bWasOutOfBounds = Marker.HasSnapshot() && Marker.GetLastSnapshot().bOutOfBounds;

	Snapshot.bOutOfBounds = UMinimapFunctionLibrary::ResolveOutOfBounds(
		ShapeMagnitude,
		bWasOutOfBounds,
		View.OutOfBoundsEnterThreshold,
		View.OutOfBoundsExitThreshold);

	Snapshot.EdgeAngle = UMinimapFunctionLibrary::GetEdgeAngle(Snapshot.Normalized);

	// --- Clamping policy --------------------------------------------------
	switch (Marker.OutOfBoundsPolicy)
	{
	case EMinimapOutOfBoundsPolicy::Hide:
		{
			bool bUnusedClamped = false;
			Snapshot.Clamped = UMinimapFunctionLibrary::ClampNormalizedToShape(
				Snapshot.Normalized, Context.bCircularMap, bUnusedClamped);
			if (Snapshot.bOutOfBounds)
			{
				bVisible = false;
			}
			break;
		}

	case EMinimapOutOfBoundsPolicy::AlwaysShow:
		// Deliberately unclamped: the caller wants the true position even off-map.
		Snapshot.Clamped = Snapshot.Normalized;
		break;

	case EMinimapOutOfBoundsPolicy::ClampToEdge:
	default:
		{
			bool bUnusedClamped = false;
			Snapshot.Clamped = UMinimapFunctionLibrary::ClampNormalizedToShape(
				Snapshot.Normalized, Context.bCircularMap, bUnusedClamped);
			break;
		}
	}

	// --- Icon rotation ----------------------------------------------------
	Snapshot.IconAngle = Marker.bUseActorYaw
		? UMinimapFunctionLibrary::GetMarkerIconAngle(WorldYaw, Context.ViewYaw)
		: 0.0f;

	Snapshot.bVisible = bVisible;
	return Snapshot;
}
