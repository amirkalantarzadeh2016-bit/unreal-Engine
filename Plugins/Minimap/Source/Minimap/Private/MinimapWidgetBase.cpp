#include "MinimapWidgetBase.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MinimapFunctionLibrary.h"
#include "MinimapMarkerWidget.h"
#include "MinimapModule.h"
#include "MinimapSubsystem.h"
#include "MinimapTrackedComponent.h"
#include "MinimapViewComponent.h"

void UMinimapWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Cache the MID exactly once. The legacy graph called GetDynamicMaterial() on every
	// Tick; the value never changes, so once is enough.
	if (IsValid(Background))
	{
		CachedMapMID = Background->GetDynamicMaterial();
		if (!CachedMapMID)
		{
			UE_LOG(LogMinimap, Warning,
				TEXT("'%s': Background image has no material, so PlayerX/PlayerY/MapRotation cannot be driven. "
				     "Assign M_Minimap to the Background image's brush."), *GetName());
		}
	}

	if (bManageMarkerWidgets && IsValid(MarkerCanvas) && DefaultMarkerWidgetClass)
	{
		// Pre-warm so the first few updates do not allocate widgets mid-frame.
		const int32 PreWarm = (MaxMarkerWidgets > 0)
			? FMath::Min(InitialMarkerPoolSize, MaxMarkerWidgets)
			: InitialMarkerPoolSize;

		for (int32 Index = 0; Index < PreWarm; ++Index)
		{
			FMinimapMarkerSnapshot Empty;
			AcquireMarkerWidget(Index, Empty);
		}

		for (TObjectPtr<UMinimapMarkerWidget>& Widget : MarkerPool)
		{
			if (IsValid(Widget))
			{
				Widget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	if (!BoundView.IsValid())
	{
		InitializeMinimap(ResolveViewComponent());
	}
}

void UMinimapWidgetBase::NativeDestruct()
{
	ShutdownMinimap();
	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

UMinimapViewComponent* UMinimapWidgetBase::ResolveViewComponent()
{
	APlayerController* PC = GetOwningPlayer();
	if (!IsValid(PC))
	{
		return nullptr;
	}

	// Preferred: a view component authored on the PlayerController, which survives pawn
	// death and repossession.
	if (UMinimapViewComponent* ControllerView = PC->FindComponentByClass<UMinimapViewComponent>())
	{
		return ControllerView;
	}

	if (APawn* Pawn = PC->GetPawn())
	{
		if (UMinimapViewComponent* PawnView = Pawn->FindComponentByClass<UMinimapViewComponent>())
		{
			return PawnView;
		}
	}

	// Last resort: create one on the controller so the widget works with zero setup.
	// Registering it explicitly is required because BeginPlay has already run for the owner.
	UMinimapViewComponent* NewView = NewObject<UMinimapViewComponent>(PC, UMinimapViewComponent::StaticClass(),
		TEXT("MinimapViewComponent_Auto"));
	if (NewView)
	{
		NewView->RegisterComponent();
		NewView->RegisterView();

		UE_LOG(LogMinimap, Log,
			TEXT("'%s': no UMinimapViewComponent found; created one automatically on '%s'."),
			*GetName(), *PC->GetName());
	}
	return NewView;
}

void UMinimapWidgetBase::InitializeMinimap(UMinimapViewComponent* InView)
{
	if (BoundView.Get() == InView)
	{
		return;
	}

	// Always detach cleanly before attaching elsewhere, or the old view keeps pushing
	// updates into this widget.
	if (UMinimapViewComponent* OldView = BoundView.Get())
	{
		OldView->OnMinimapViewUpdated.RemoveDynamic(this, &UMinimapWidgetBase::HandleViewUpdated);
		OldView->SetViewRenderingEnabled(false);
	}

	BoundView = InView;

	if (!IsValid(InView))
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': InitializeMinimap called with no view; the minimap will not update."), *GetName());
		return;
	}

	InView->OnMinimapViewUpdated.AddUniqueDynamic(this, &UMinimapWidgetBase::HandleViewUpdated);

	// Background updates arrive on capture, never per frame, and are entirely separate
	// from the marker path.
	if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
	{
		Subsystem->OnBackgroundTextureChanged.AddUniqueDynamic(this, &UMinimapWidgetBase::HandleBackgroundTextureChanged);

		// A widget created after the capture already happened still needs the texture.
		if (UTexture* Existing = Subsystem->GetBackgroundTexture())
		{
			ApplyBackgroundTexture(Existing);
		}
	}

	// Tell the subsystem this view is on screen, and force one immediate pass so the
	// minimap is correct on the frame it appears rather than up to TickInterval later.
	InView->SetViewRenderingEnabled(true);

	if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
	{
		Subsystem->ForceUpdate();
	}
}

void UMinimapWidgetBase::ShutdownMinimap()
{
	if (UMinimapViewComponent* View = BoundView.Get())
	{
		View->OnMinimapViewUpdated.RemoveDynamic(this, &UMinimapWidgetBase::HandleViewUpdated);

		// Stops the subsystem projecting for a view nobody is displaying.
		View->SetViewRenderingEnabled(false);
	}

	// Every delegate this widget bound must come off, or a recreated widget would leave
	// the old one receiving broadcasts.
	if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
	{
		Subsystem->OnBackgroundTextureChanged.RemoveDynamic(this, &UMinimapWidgetBase::HandleBackgroundTextureChanged);
	}

	BoundView.Reset();

	for (TObjectPtr<UMinimapMarkerWidget>& Widget : MarkerPool)
	{
		if (IsValid(Widget))
		{
			Widget->SetBoundMarker(nullptr);
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// ---------------------------------------------------------------------------
// Per-update work
// ---------------------------------------------------------------------------

void UMinimapWidgetBase::HandleViewUpdated(UMinimapViewComponent* View, const TArray<FMinimapMarkerSnapshot>& Snapshots)
{
	if (!IsValid(View) || View != BoundView.Get())
	{
		return;
	}

	if (bDriveMaterialParameters)
	{
		UpdateMaterialParameters(*View);
	}

	if (bDriveNorthIndicator && IsValid(North_Container))
	{
		// Counter-rotate so the compass keeps pointing at world north as the map turns.
		const float CompassAngle = View->GetCompassAngle();
		if (!bCompassInitialized || !FMath::IsNearlyEqual(CachedCompassAngle, CompassAngle, 0.01f))
		{
			North_Container->SetRenderTransformAngle(CompassAngle);
			CachedCompassAngle = CompassAngle;
			bCompassInitialized = true;
		}
	}

	if (bManageMarkerWidgets)
	{
		UpdateMarkerWidgets(Snapshots);
	}

	OnMinimapUpdated(Snapshots);
}

void UMinimapWidgetBase::SetScalarIfChanged(FName ParameterName, float NewValue, float& CachedValue, bool& bCacheInitialized)
{
	if (bCacheInitialized && FMath::Abs(CachedValue - NewValue) <= MaterialUpdateTolerance)
	{
		return;
	}

	CachedMapMID->SetScalarParameterValue(ParameterName, NewValue);
	CachedValue = NewValue;
	bCacheInitialized = true;
}

void UMinimapWidgetBase::UpdateMaterialParameters(const UMinimapViewComponent& View)
{
	if (!IsValid(CachedMapMID))
	{
		return;
	}

	// PlayerX / PlayerY are the viewer's position on the WHOLE map, scaled to the
	// [-0.5, 0.5] range M_Minimap already expects. Identical semantics to the legacy
	// MapRangeClamped pair, but derived from calibration instead of hardcoded ranges.
	const FVector2D PlayerParams = View.GetMaterialPlayerParams();

	SetScalarIfChanged(PlayerXParameterName, static_cast<float>(PlayerParams.X), CachedPlayerX, bPlayerXInitialized);
	SetScalarIfChanged(PlayerYParameterName, static_cast<float>(PlayerParams.Y), CachedPlayerY, bPlayerYInitialized);

	// Turns, [0, 1). Frac'd so crossing due north cannot make the Rotator node jump.
	const float MapRotationTurns = View.GetMapRotationTurns();

	// Rotation wraps, so a naive tolerance test would suppress the 0.999 -> 0.001 step.
	// Compare on the circle instead.
	const float Delta = FMath::Abs(FMath::Fmod(FMath::Abs(MapRotationTurns - CachedMapRotation), 1.0f));
	const float WrappedDelta = FMath::Min(Delta, 1.0f - Delta);

	if (!bMapRotationInitialized || WrappedDelta > MaterialUpdateTolerance)
	{
		CachedMapMID->SetScalarParameterValue(MapRotationParameterName, MapRotationTurns);
		CachedMapRotation = MapRotationTurns;
		bMapRotationInitialized = true;
	}
}

// ---------------------------------------------------------------------------
// Marker pooling
// ---------------------------------------------------------------------------

FVector2D UMinimapWidgetBase::GetMapWidgetSize() const
{
	if (IsValid(MarkerCanvas))
	{
		const FVector2D CachedSize = MarkerCanvas->GetCachedGeometry().GetLocalSize();
		if (CachedSize.X > 1.0 && CachedSize.Y > 1.0)
		{
			return CachedSize;
		}
	}
	// Before the first paint the geometry is zero-sized; using it would stack every marker
	// in the top-left corner for one frame.
	return FallbackMapSize;
}

UMinimapMarkerWidget* UMinimapWidgetBase::AcquireMarkerWidget(int32 Index, const FMinimapMarkerSnapshot& Snapshot)
{
	if (MarkerPool.IsValidIndex(Index))
	{
		return MarkerPool[Index];
	}

	if (!IsValid(MarkerCanvas))
	{
		return nullptr;
	}

	// Per-marker class override, falling back to the widget's default.
	TSubclassOf<UUserWidget> DesiredClass = nullptr;
	if (const UMinimapTrackedComponent* Marker = Snapshot.Tracked)
	{
		DesiredClass = Marker->Style.MarkerWidgetClass;
	}

	TSubclassOf<UMinimapMarkerWidget> ResolvedClass = DefaultMarkerWidgetClass;
	if (DesiredClass && DesiredClass->IsChildOf(UMinimapMarkerWidget::StaticClass()))
	{
		ResolvedClass = TSubclassOf<UMinimapMarkerWidget>(DesiredClass);
	}

	if (!ResolvedClass)
	{
		return nullptr;
	}

	UMinimapMarkerWidget* NewWidget = CreateWidget<UMinimapMarkerWidget>(GetOwningPlayer(), ResolvedClass);
	if (!NewWidget)
	{
		return nullptr;
	}

	if (UCanvasPanelSlot* CanvasSlot = MarkerCanvas->AddChildToCanvas(NewWidget))
	{
		// Anchor top-left and align to the widget's own centre, so the pixel position we
		// compute lands on the marker's middle rather than its corner.
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		CanvasSlot->SetAlignment(FVector2D(0.5, 0.5));
		CanvasSlot->SetAutoSize(true);
	}

	MarkerPool.Add(NewWidget);
	return NewWidget;
}

void UMinimapWidgetBase::UpdateMarkerWidgets(const TArray<FMinimapMarkerSnapshot>& Snapshots)
{
	if (!IsValid(MarkerCanvas))
	{
		return;
	}

	const FVector2D MapSize = GetMapWidgetSize();
	int32 UsedWidgets = 0;

	for (const FMinimapMarkerSnapshot& Snapshot : Snapshots)
	{
		if (!Snapshot.bVisible || !IsValid(Snapshot.Tracked))
		{
			continue;
		}

		if (MaxMarkerWidgets > 0 && UsedWidgets >= MaxMarkerWidgets)
		{
			// Snapshots arrive priority-ascending, so anything dropped here is the least
			// important. Break rather than continue: everything after is lower priority.
			break;
		}

		UMinimapMarkerWidget* Widget = AcquireMarkerWidget(UsedWidgets, Snapshot);
		if (!IsValid(Widget))
		{
			break;
		}

		Widget->SetBoundMarker(Snapshot.Tracked);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			CanvasSlot->SetPosition(UMinimapFunctionLibrary::NormalizedToWidgetPixels(Snapshot.Clamped, MapSize));

			// Higher priority markers are later in the array, so a rising ZOrder puts
			// them on top without a second sort.
			CanvasSlot->SetZOrder(UsedWidgets);
		}

		Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
		Widget->OnMarkerUpdated(Snapshot);

		++UsedWidgets;
	}

	// Collapse the tail rather than destroying it: that is the whole point of a pool.
	for (int32 Index = UsedWidgets; Index < MarkerPool.Num(); ++Index)
	{
		if (UMinimapMarkerWidget* Widget = MarkerPool[Index])
		{
			if (Widget->GetVisibility() != ESlateVisibility::Collapsed)
			{
				Widget->SetBoundMarker(nullptr);
				Widget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Pop effect passthrough
// ---------------------------------------------------------------------------

void UMinimapWidgetBase::SetMinimapExpanded(bool bExpanded)
{
	if (bMinimapExpanded == bExpanded)
	{
		// Guarding here is what stops the animation restarting from frame 0 every time
		// the input is held.
		return;
	}

	bMinimapExpanded = bExpanded;
	HandlePopEffect(bExpanded);
}


// ---------------------------------------------------------------------------
// Captured background
// ---------------------------------------------------------------------------

void UMinimapWidgetBase::HandleBackgroundTextureChanged(UTexture* BackgroundTexture)
{
	ApplyBackgroundTexture(BackgroundTexture);
}

void UMinimapWidgetBase::ApplyBackgroundTexture(UTexture* BackgroundTexture)
{
	if (AppliedBackgroundTexture == BackgroundTexture)
	{
		// Applying a texture invalidates widget layout, so never do it redundantly.
		return;
	}

	AppliedBackgroundTexture = BackgroundTexture;

	if (!BackgroundTexture)
	{
		// Null means "capture is gone" - leave whatever static background is configured
		// in place rather than blanking the map.
		return;
	}

	const bool bTryMaterial =
		BackgroundApplyMode == EMinimapBackgroundApplyMode::MaterialParameter ||
		BackgroundApplyMode == EMinimapBackgroundApplyMode::Automatic;

	bool bAppliedToMaterial = false;

	if (bTryMaterial && IsValid(CachedMapMID) && !MapTextureParameterName.IsNone())
	{
		// Set, then read back through the Blueprint-stable accessor. If the material has
		// no such parameter the write is a no-op and the read returns null, which is how
		// we detect the miss without depending on the overload set of
		// GetTextureParameterValue (its signature differs across 5.x point releases).
		CachedMapMID->SetTextureParameterValue(MapTextureParameterName, BackgroundTexture);

		const UTexture* ReadBack = CachedMapMID->K2_GetTextureParameterValue(MapTextureParameterName);
		bAppliedToMaterial = (ReadBack == BackgroundTexture);

		if (!bAppliedToMaterial && !bWarnedMissingTextureParameter)
		{
			bWarnedMissingTextureParameter = true;
			UE_LOG(LogMinimap, Warning,
				TEXT("'%s': the map material has no texture parameter named '%s'. %s"),
				*GetName(), *MapTextureParameterName.ToString(),
				BackgroundApplyMode == EMinimapBackgroundApplyMode::Automatic
					? TEXT("Falling back to setting the Image brush, which bypasses the material "
					       "(PlayerX/PlayerY/MapRotation will no longer affect the background). Add a "
					       "Texture Sample Parameter with that name to M_Minimap to keep them working.")
					: TEXT("Nothing was applied. Add the parameter, or switch Background Apply Mode "
					       "to Automatic or Image Brush."));
		}
	}

	const bool bUseBrush =
		BackgroundApplyMode == EMinimapBackgroundApplyMode::ImageBrush ||
		(BackgroundApplyMode == EMinimapBackgroundApplyMode::Automatic && !bAppliedToMaterial);

	if (bUseBrush && IsValid(Background))
	{
		// SetBrushFromTexture takes a UTexture2D and a render target is NOT one - it
		// derives from UTexture via UTextureRenderTarget. SetBrushResourceObject accepts
		// any resource object, and has the additional advantage of leaving the brush's
		// existing DrawAs / ImageSize / tint alone, so the widget's mask, clipping and
		// layout are preserved exactly as authored.
		Background->SetBrushResourceObject(BackgroundTexture);
	}
}
