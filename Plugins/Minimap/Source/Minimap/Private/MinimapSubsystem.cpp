#include "MinimapSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MinimapBoundsVolume.h"
#include "MinimapCaptureComponent.h"
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

	// Dropping these on teardown is what stops a PIE restart or level transition from
	// inheriting a stale provider and appearing to have duplicate resources.
	BoundsVolumes.Reset();
	BackgroundProvider.Reset();

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


// ---------------------------------------------------------------------------
// Bounds selection
// ---------------------------------------------------------------------------

void UMinimapSubsystem::RegisterBoundsVolume(AMinimapBoundsVolume* Volume)
{
	if (!IsValid(Volume))
	{
		return;
	}

	BoundsVolumes.RemoveAll([](const TWeakObjectPtr<AMinimapBoundsVolume>& Entry) { return !Entry.IsValid(); });

	const TWeakObjectPtr<AMinimapBoundsVolume> WeakVolume(Volume);
	if (!BoundsVolumes.Contains(WeakVolume))
	{
		BoundsVolumes.Add(WeakVolume);
	}

	// Surface ambiguity as soon as it appears rather than at first use.
	if (BoundsVolumes.Num() > 1)
	{
		FString Reason;
		if (!ResolveAuthoritativeBounds(Reason))
		{
			UE_LOG(LogMinimap, Warning, TEXT("Minimap bounds selection is ambiguous: %s"), *Reason);
		}
	}
}

void UMinimapSubsystem::UnregisterBoundsVolume(AMinimapBoundsVolume* Volume)
{
	if (!Volume)
	{
		return;
	}

	BoundsVolumes.RemoveAll(
		[Volume](const TWeakObjectPtr<AMinimapBoundsVolume>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == Volume;
		});
}

AMinimapBoundsVolume* UMinimapSubsystem::ResolveAuthoritativeBounds(FString& OutReason) const
{
	TArray<AMinimapBoundsVolume*> Eligible;
	Eligible.Reserve(BoundsVolumes.Num());
	for (const TWeakObjectPtr<AMinimapBoundsVolume>& Weak : BoundsVolumes)
	{
		if (AMinimapBoundsVolume* Volume = Weak.Get())
		{
			Eligible.Add(Volume);
		}
	}

	if (Eligible.Num() == 0)
	{
		OutReason = TEXT("No AMinimapBoundsVolume is registered. Place one in the level and make "
		                 "sure Apply On Begin Play is enabled.");
		return nullptr;
	}

	// --- 1. Explicit preference ------------------------------------------
	TArray<AMinimapBoundsVolume*> Preferred;
	for (AMinimapBoundsVolume* Volume : Eligible)
	{
		if (Volume->bPreferredBounds)
		{
			Preferred.Add(Volume);
		}
	}
	if (Preferred.Num() == 1)
	{
		OutReason = TEXT("Selected by bPreferredBounds.");
		return Preferred[0];
	}
	if (Preferred.Num() > 1)
	{
		OutReason = FString::Printf(
			TEXT("%d bounds volumes have bPreferredBounds set; exactly one must. Candidates: %s"),
			Preferred.Num(), *FString::JoinBy(Preferred, TEXT(", "),
				[](const AMinimapBoundsVolume* V) { return V->GetName(); }));
		return nullptr;
	}

	// --- 2. Configured tag ------------------------------------------------
	if (!RequiredBoundsTag.IsNone())
	{
		TArray<AMinimapBoundsVolume*> Tagged;
		for (AMinimapBoundsVolume* Volume : Eligible)
		{
			if (Volume->BoundsSelectionTag == RequiredBoundsTag)
			{
				Tagged.Add(Volume);
			}
		}
		if (Tagged.Num() == 1)
		{
			OutReason = FString::Printf(TEXT("Selected by BoundsSelectionTag '%s'."), *RequiredBoundsTag.ToString());
			return Tagged[0];
		}
		if (Tagged.Num() > 1)
		{
			OutReason = FString::Printf(
				TEXT("%d bounds volumes share BoundsSelectionTag '%s'; the tag must be unique."),
				Tagged.Num(), *RequiredBoundsTag.ToString());
			return nullptr;
		}
		OutReason = FString::Printf(
			TEXT("No bounds volume carries the required BoundsSelectionTag '%s'."), *RequiredBoundsTag.ToString());
		return nullptr;
	}

	// --- 3. Unique fallback -----------------------------------------------
	if (Eligible.Num() == 1)
	{
		OutReason = TEXT("Selected as the only registered bounds volume.");
		return Eligible[0];
	}

	// Deliberately refuses to guess. Picking an arbitrary first actor here is exactly the
	// silent misconfiguration this function exists to prevent.
	OutReason = FString::Printf(
		TEXT("%d bounds volumes are registered and none is distinguished. Set bPreferredBounds on "
		     "one, or give them BoundsSelectionTags and call SetRequiredBoundsTag. Candidates: %s"),
		Eligible.Num(), *FString::JoinBy(Eligible, TEXT(", "),
			[](const AMinimapBoundsVolume* V) { return V->GetName(); }));
	return nullptr;
}

void UMinimapSubsystem::SetRequiredBoundsTag(FName NewTag)
{
	RequiredBoundsTag = NewTag;
}

// ---------------------------------------------------------------------------
// Background
// ---------------------------------------------------------------------------

void UMinimapSubsystem::RegisterBackgroundProvider(UMinimapCaptureComponent* Provider)
{
	if (!IsValid(Provider))
	{
		return;
	}

	if (BackgroundProvider.Get() == Provider)
	{
		// Already the active provider: re-broadcast so a widget created after the first
		// capture still receives the texture.
		OnBackgroundTextureChanged.Broadcast(GetBackgroundTexture());
		return;
	}

	// One provider at a time. Several widgets share this single resource rather than each
	// spawning a capture of its own.
	BackgroundProvider = Provider;

	Provider->OnBackgroundCaptured.RemoveAll(this);
	Provider->OnBackgroundCaptured.AddDynamic(this, &UMinimapSubsystem::HandleBackgroundCaptured);

	OnBackgroundTextureChanged.Broadcast(GetBackgroundTexture());
}

void UMinimapSubsystem::UnregisterBackgroundProvider(UMinimapCaptureComponent* Provider)
{
	if (!Provider || BackgroundProvider.Get() != Provider)
	{
		return;
	}

	Provider->OnBackgroundCaptured.RemoveAll(this);
	BackgroundProvider.Reset();
	OnBackgroundTextureChanged.Broadcast(nullptr);
}

void UMinimapSubsystem::HandleBackgroundCaptured(UMinimapCaptureComponent* Capture, UTextureRenderTarget2D* RenderTarget)
{
	if (BackgroundProvider.Get() != Capture)
	{
		return;
	}
	OnBackgroundTextureChanged.Broadcast(RenderTarget);
}

UMinimapCaptureComponent* UMinimapSubsystem::GetBackgroundProvider() const
{
	return BackgroundProvider.Get();
}

UTexture* UMinimapSubsystem::GetBackgroundTexture() const
{
	const UMinimapCaptureComponent* Provider = BackgroundProvider.Get();
	return Provider ? Cast<UTexture>(Provider->GetMinimapRenderTarget()) : nullptr;
}

void UMinimapSubsystem::RequestBackgroundRefresh()
{
	if (UMinimapCaptureComponent* Provider = BackgroundProvider.Get())
	{
		Provider->RequestBackgroundRefresh();
		return;
	}

	// No provider yet: fall back to the authoritative volume, which will create one.
	FString Reason;
	if (AMinimapBoundsVolume* Volume = ResolveAuthoritativeBounds(Reason))
	{
		Volume->RefreshMinimapBackground();
	}
	else
	{
		UE_LOG(LogMinimap, Warning, TEXT("RequestBackgroundRefresh: %s"), *Reason);
	}
}

void UMinimapSubsystem::RefitBoundsAndRefresh()
{
	FString Reason;
	if (AMinimapBoundsVolume* Volume = ResolveAuthoritativeBounds(Reason))
	{
		Volume->FitBoundsAndRefresh();
	}
	else
	{
		UE_LOG(LogMinimap, Warning, TEXT("RefitBoundsAndRefresh: %s"), *Reason);
	}
}

void UMinimapSubsystem::ApplyCaptureSettingsAndRefresh()
{
	FString Reason;
	if (AMinimapBoundsVolume* Volume = ResolveAuthoritativeBounds(Reason))
	{
		Volume->ApplyCaptureSettingsAndRefresh();
	}
	else
	{
		UE_LOG(LogMinimap, Warning, TEXT("ApplyCaptureSettingsAndRefresh: %s"), *Reason);
	}
}

void UMinimapSubsystem::NotifyMinimapContentReady()
{
	FString Reason;
	if (AMinimapBoundsVolume* Volume = ResolveAuthoritativeBounds(Reason))
	{
		Volume->NotifyMinimapContentReady();
	}
	else
	{
		UE_LOG(LogMinimap, Warning, TEXT("NotifyMinimapContentReady: %s"), *Reason);
	}
}
