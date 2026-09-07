#include "MinimapBoundsVolume.h"

#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "MinimapModule.h"
#include "MinimapCaptureComponent.h"
#include "MinimapFunctionLibrary.h"
#include "MinimapPresetAsset.h"
#include "MinimapSubsystem.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"
#include "MinimapWidgetBase.h"

#if WITH_EDITOR
#include "EngineUtils.h"
#endif

AMinimapBoundsVolume::AMinimapBoundsVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// Never replicated: calibration is authored data, identical on every machine.
	bReplicates = false;
	SetCanBeDamaged(false);

	BoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));
	RootComponent = BoundsBox;

	// A sensible starting size; authors scale this, and nothing in the runtime path
	// ever assumes these numbers.
	BoundsBox->SetBoxExtent(FVector(5000.0f, 5000.0f, 2000.0f), /*bUpdateOverlaps=*/false);
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetGenerateOverlapEvents(false);
	BoundsBox->SetHiddenInGame(true);
	BoundsBox->ShapeColor = FColor(80, 200, 255);
	BoundsBox->bDrawOnlyIfSelected = false;

#if WITH_EDITORONLY_DATA
	EditorSprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("EditorSprite"));
	if (EditorSprite)
	{
		EditorSprite->SetupAttachment(BoundsBox);
		EditorSprite->bIsScreenSizeScaled = true;
		EditorSprite->SetHiddenInGame(true);
	}
#endif
}

void AMinimapBoundsVolume::BeginPlay()
{
	Super::BeginPlay();

	// Register before applying, so the subsystem can detect an ambiguous setup (several
	// eligible volumes) rather than silently letting the last one to run BeginPlay win.
	if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
	{
		Subsystem->RegisterBoundsVolume(this);
	}

	if (bApplyOnBeginPlay)
	{
		ApplyCalibration();
	}
}

void AMinimapBoundsVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Explicit teardown: leaving a stale volume registered would make a level transition
	// or a PIE restart look like an ambiguous-bounds setup.
	if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
	{
		Subsystem->UnregisterBoundsVolume(this);
	}

	if (IsValid(CaptureComponent))
	{
		CaptureComponent->ReleaseCaptureResources();
	}
	CaptureComponent = nullptr;
	bCalibrationAppliedThisPlay = false;

	Super::EndPlay(EndPlayReason);
}

FMinimapCalibration AMinimapBoundsVolume::BuildCalibration() const
{
	if (bManualOverride)
	{
		FString Reason;
		if (!ManualCalibration.IsValidCalibration(&Reason))
		{
			UE_LOG(LogMinimap, Warning,
				TEXT("'%s': manual calibration override is invalid (%s). It will be rejected by the subsystem."),
				*GetName(), *Reason);
		}
		return ManualCalibration;
	}

	FMinimapCalibration Calibration;

	if (!IsValid(BoundsBox))
	{
		// Cannot happen via the constructor, but a Blueprint child could null the root.
		UE_LOG(LogMinimap, Error,
			TEXT("'%s' has no BoundsBox; returning a default calibration that will be rejected."), *GetName());
		Calibration.WorldExtent = FVector2D::ZeroVector;
		return Calibration;
	}

	// GetScaledBoxExtent() already folds in the component AND actor scale, so a designer
	// can size the volume with either and get the same answer.
	const FVector ScaledExtent = BoundsBox->GetScaledBoxExtent();
	const FVector WorldLocation = BoundsBox->GetComponentLocation();

	Calibration.WorldCenter = FVector2D(WorldLocation.X, WorldLocation.Y);

	// WorldExtent is expressed along the MAP IMAGE axes (U, then V) - NOT the box's local
	// axes. The projection divides an East-derived value by WorldExtent.X, so under the
	// standard convention (image U follows local +Y, image V follows local -X) the
	// components must be swapped relative to the box. With bSwapUV the image axes coincide
	// with the box axes and no swap is needed.
	//
	// This only changes behaviour for a NON-SQUARE volume with bPreserveAspectRatio OFF;
	// with the default (on) both components are squared to max(X, Y) and the order is
	// irrelevant. Previously a non-square volume divided the East offset by the North
	// half-extent, which misplaced markers along one axis.
	// Resolve the axis convention first: the extent ordering below depends on it, so a
	// preset that overrides bSwapUV must be honoured before the extent is computed.
	const bool bUsePresetCalibration = IsValid(Preset) && Preset->bApplyCalibrationDefaults;
	const bool bEffectiveSwapUV  = bUsePresetCalibration ? Preset->bSwapUV  : bSwapUV;
	const bool bEffectiveInvertU = bUsePresetCalibration ? Preset->bInvertU : bInvertU;
	const bool bEffectiveInvertV = bUsePresetCalibration ? Preset->bInvertV : bInvertV;
	const bool bEffectivePreserveAspect = bUsePresetCalibration ? Preset->bPreserveAspectRatio : bPreserveAspectRatio;
	const bool bEffectiveCircular = bUsePresetCalibration ? Preset->bCircularMap : bCircularMap;

	const double LocalHalfX = FMath::Abs(ScaledExtent.X);
	const double LocalHalfY = FMath::Abs(ScaledExtent.Y);
	Calibration.WorldExtent = bEffectiveSwapUV
		? FVector2D(LocalHalfX, LocalHalfY)
		: FVector2D(LocalHalfY, LocalHalfX);

	// The box rotates with the actor, so the map must rotate with it too or the projection
	// and the volume would disagree the moment the volume is not axis-aligned.
	Calibration.MapYaw = bUseActorYawAsMapYaw
		? static_cast<float>(GetActorRotation().Yaw) + AdditionalMapYaw
		: AdditionalMapYaw;

	Calibration.MinZ = static_cast<float>(WorldLocation.Z - FMath::Abs(ScaledExtent.Z));
	Calibration.MaxZ = static_cast<float>(WorldLocation.Z + FMath::Abs(ScaledExtent.Z));

	Calibration.Zoom                 = FMath::Max(Zoom, 0.01f);
	Calibration.bPreserveAspectRatio = bEffectivePreserveAspect;
	Calibration.bCircularMap         = bEffectiveCircular;
	Calibration.bSwapUV              = bEffectiveSwapUV;
	Calibration.bInvertU             = bEffectiveInvertU;
	Calibration.bInvertV             = bEffectiveInvertV;

	// Validate here as well as in the subsystem so the author gets a message naming the
	// offending actor rather than an anonymous rejection.
	FString Reason;
	if (!Calibration.IsValidCalibration(&Reason))
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s' produced an invalid calibration: %s (scaled box extent was %s). "
			     "Scale the BoundsBox so its X and Y extents are greater than zero."),
			*GetName(), *Reason, *ScaledExtent.ToCompactString());
	}

	return Calibration;
}

bool AMinimapBoundsVolume::ApplyCalibration()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogMinimap, Warning, TEXT("'%s': ApplyCalibration called with no world."), *GetName());
		return false;
	}

	UMinimapSubsystem* Subsystem = World->GetSubsystem<UMinimapSubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': no UMinimapSubsystem in world '%s'; calibration not applied."),
			*GetName(), *World->GetName());
		return false;
	}

	const FMinimapCalibration Calibration = BuildCalibration();

	// SetCalibration re-validates and refuses bad input, keeping the last good values.
	const bool bApplied = Subsystem->SetCalibration(Calibration);
	if (!bApplied)
	{
		return false;
	}

	UE_LOG(LogMinimap, Log, TEXT("'%s': calibration applied to the minimap subsystem."), *GetName());
	bCalibrationAppliedThisPlay = true;

	// Drive the capture from the SAME calibration the markers use. Nothing else computes
	// a second coordinate transform, which is what guarantees image/marker agreement.
	if (UMinimapCaptureComponent* Capture = EnsureCaptureComponent())
	{
		FString CaptureError;
		if (Capture->ApplyCalibration(Calibration, CaptureError))
		{
			Subsystem->RegisterBackgroundProvider(Capture);

			// One settle delay so streamed geometry has a chance to appear. This is a
			// convenience, not the readiness contract - see NotifyMinimapContentReady.
			const FMinimapCaptureSettings EffectiveSettings = GetEffectiveCaptureSettings();
			if (EffectiveSettings.RefreshPolicy != EMinimapRefreshPolicy::Manual)
			{
				if (EffectiveSettings.InitialCaptureDelay > 0.0f && GetWorld())
				{
					FTimerHandle InitialHandle;
					GetWorld()->GetTimerManager().SetTimer(
						InitialHandle,
						FTimerDelegate::CreateWeakLambda(this, [this]()
						{
							if (IsValid(CaptureComponent))
							{
								CaptureComponent->ApplyCaptureSettingsAndRefresh();
							}
						}),
						EffectiveSettings.InitialCaptureDelay, /*bLoop=*/false);
				}
				else
				{
					Capture->ApplyCaptureSettingsAndRefresh();
				}
			}
		}
		else if (!CaptureError.IsEmpty())
		{
			// Explicit fallback: the widget keeps whatever static background it already
			// has, rather than showing a blank or misaligned map.
			UE_LOG(LogMinimap, Warning,
				TEXT("'%s': automatic capture could not initialize (%s). Falling back to the "
				     "configured static background."), *GetName(), *CaptureError);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Fit filtering (shared by the editor fit; compiled in all configurations)
// ---------------------------------------------------------------------------

bool AMinimapBoundsVolume::PassesFitFilter(const AActor* Actor, const FVector& ActorExtent) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	// Zero-extent actors carry no geometry to map.
	if (ActorExtent.IsNearlyZero())
	{
		return false;
	}

	// Other bounds volumes would make the fit self-referential.
	if (Actor->IsA<AMinimapBoundsVolume>())
	{
		return false;
	}

	// The single most useful guard: one sky sphere or infinite-extent fog volume would
	// otherwise inflate the map to kilometres and make the whole level a few pixels.
	if (FitMaxActorExtent > 0.0f &&
	    (ActorExtent.X > FitMaxActorExtent || ActorExtent.Y > FitMaxActorExtent))
	{
		return false;
	}

	if (bFitIgnoreHiddenActors && Actor->IsHidden())
	{
		return false;
	}

	for (const TSubclassOf<AActor>& ExcludedClass : FitExcludeClasses)
	{
		if (*ExcludedClass && Actor->IsA(ExcludedClass))
		{
			return false;
		}
	}

	for (const FName& Tag : FitExcludeTags)
	{
		if (!Tag.IsNone() && Actor->ActorHasTag(Tag))
		{
			return false;
		}
	}

	// An allow-list, when supplied, overrides everything permissive above.
	if (FitRequireTags.Num() > 0)
	{
		bool bHasRequired = false;
		for (const FName& Tag : FitRequireTags)
		{
			if (!Tag.IsNone() && Actor->ActorHasTag(Tag))
			{
				bHasRequired = true;
				break;
			}
		}
		if (!bHasRequired)
		{
			return false;
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Capture ownership
// ---------------------------------------------------------------------------

FMinimapCaptureSettings AMinimapBoundsVolume::GetEffectiveCaptureSettings() const
{
	FMinimapCaptureSettings Result =
		(IsValid(Preset) && !bOverridePresetCaptureSettings) ? Preset->CaptureSettings : CaptureSettingsOverride;

	Result.SanitizeInPlace();
	return Result;
}

UMinimapCaptureComponent* AMinimapBoundsVolume::EnsureCaptureComponent()
{
	const FMinimapCaptureSettings EffectiveSettings = GetEffectiveCaptureSettings();

	// Nothing is allocated unless capture mode is actually selected, which keeps the
	// legacy static-texture path exactly as cheap as it was.
	if (EffectiveSettings.BackgroundSource != EMinimapBackgroundSource::SceneCapture)
	{
		return nullptr;
	}

	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		// No viewer, so no capture resources.
		return nullptr;
	}

	if (IsValid(CaptureComponent))
	{
		// Idempotent: repeated calls (PIE restarts, repeated Apply presses) must not
		// create a second capture for the same map view.
		CaptureComponent->Settings = EffectiveSettings;
		CaptureComponent->ExcludedActors = CaptureExcludedActors;
		CaptureComponent->IncludedActors = CaptureIncludedActors;
		return CaptureComponent;
	}

	CaptureComponent = NewObject<UMinimapCaptureComponent>(this, UMinimapCaptureComponent::StaticClass(),
		TEXT("MinimapCaptureComponent"));
	if (!CaptureComponent)
	{
		UE_LOG(LogMinimap, Error, TEXT("'%s': failed to create the minimap capture component."), *GetName());
		return nullptr;
	}

	CaptureComponent->Settings = EffectiveSettings;
	CaptureComponent->ExcludedActors = CaptureExcludedActors;
	CaptureComponent->IncludedActors = CaptureIncludedActors;

	CaptureComponent->SetupAttachment(RootComponent);
	CaptureComponent->RegisterComponent();

	return CaptureComponent;
}

// ---------------------------------------------------------------------------
// Refresh entry points
// ---------------------------------------------------------------------------

void AMinimapBoundsVolume::RefreshMinimapBackground()
{
	// Deliberately does NOT touch the bounds. Ordinary furniture edits must never move
	// or resize the map calibration.
	if (UMinimapCaptureComponent* Capture = EnsureCaptureComponent())
	{
		if (!bCalibrationAppliedThisPlay)
		{
			// Covers the editor button and any call before BeginPlay ordering settled.
			ApplyCalibration();
			return;
		}
		Capture->RequestBackgroundRefresh();
	}
}

void AMinimapBoundsVolume::FitBoundsAndRefresh()
{
#if WITH_EDITOR
	FitToLevelBounds();
#else
	UE_LOG(LogMinimap, Warning,
		TEXT("'%s': FitBoundsAndRefresh is editor-only; refreshing without re-fitting."), *GetName());
#endif

	// Re-applying pushes the new calibration to BOTH the markers and the capture.
	ApplyCalibration();
	RefreshMinimapBackground();
}

void AMinimapBoundsVolume::ApplyCaptureSettingsAndRefresh()
{
	if (UMinimapCaptureComponent* Capture = EnsureCaptureComponent())
	{
		if (!bCalibrationAppliedThisPlay)
		{
			ApplyCalibration();
			return;
		}
		Capture->ApplyCaptureSettingsAndRefresh();
	}
}

void AMinimapBoundsVolume::NotifyMinimapContentReady()
{
	// The explicit readiness contract for streamed / procedurally generated levels.
	if (!bCalibrationAppliedThisPlay)
	{
		ApplyCalibration();
		return;
	}
	RefreshMinimapBackground();
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

FMinimapValidationReport AMinimapBoundsVolume::ValidateMinimapSetup()
{
	FMinimapValidationReport Report;

	// --- Bounds ----------------------------------------------------------
	if (!IsValid(BoundsBox))
	{
		Report.Add(true, TEXT("BoundsBox component is missing."));
	}
	else
	{
		const FVector ScaledExtent = BoundsBox->GetScaledBoxExtent();
		if (FMath::Abs(ScaledExtent.X) < 1.0 || FMath::Abs(ScaledExtent.Y) < 1.0)
		{
			Report.Add(true, FString::Printf(
				TEXT("Degenerate bounds: scaled box extent is %s. Scale the BoundsBox so X and Y are > 0."),
				*ScaledExtent.ToCompactString()));
		}
		if (FMath::Abs(ScaledExtent.Z) < 1.0)
		{
			Report.Add(false, TEXT("Bounds Z extent is near zero; height filtering and auto capture "
			                       "height will not behave usefully."));
		}
	}

	const FMinimapCalibration Calibration = BuildCalibration();
	FString CalibrationReason;
	if (!Calibration.IsValidCalibration(&CalibrationReason))
	{
		Report.Add(true, FString::Printf(TEXT("Calibration invalid: %s"), *CalibrationReason));
	}

	// --- Ambiguous bounds selection ---------------------------------------
	if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
	{
		FString SelectionReason;
		const AMinimapBoundsVolume* Authoritative = Subsystem->ResolveAuthoritativeBounds(SelectionReason);

		if (!Authoritative)
		{
			Report.Add(true, FString::Printf(TEXT("No authoritative bounds volume: %s"), *SelectionReason));
		}
		else if (Authoritative != this)
		{
			Report.Add(false, FString::Printf(
				TEXT("'%s' is the authoritative bounds volume, not this one. %s"),
				*Authoritative->GetName(), *SelectionReason));
		}

		if (!Subsystem->HasValidCalibration())
		{
			Report.Add(false, TEXT("Subsystem has no calibration yet. Expected before BeginPlay; "
			                       "an error at runtime."));
		}
	}
	else
	{
		Report.Add(false, TEXT("No UMinimapSubsystem in this world (normal for some preview worlds)."));
	}

	// --- Transform support ------------------------------------------------
	const FRotator ActorRotation = GetActorRotation();
	if (!FMath::IsNearlyZero(ActorRotation.Pitch, 0.1f) || !FMath::IsNearlyZero(ActorRotation.Roll, 0.1f))
	{
		Report.Add(true, FString::Printf(
			TEXT("Bounds volume is pitched/rolled (Pitch=%.1f Roll=%.1f). Only YAW is supported; "
			     "the projection is planar and a tilted volume would produce a misaligned map."),
			ActorRotation.Pitch, ActorRotation.Roll));
	}

	const FVector Scale = GetActorScale3D();
	if (Scale.X < 0.0 || Scale.Y < 0.0 || Scale.Z < 0.0)
	{
		Report.Add(true, TEXT("Negative actor scale mirrors the bounds; this is not supported and "
		                      "will misalign markers relative to the image."));
	}

	// --- Capture ----------------------------------------------------------
	const FMinimapCaptureSettings EffectiveSettings = GetEffectiveCaptureSettings();
	if (EffectiveSettings.BackgroundSource == EMinimapBackgroundSource::SceneCapture)
	{
		FString AlignmentReason;
		if (!UMinimapFunctionLibrary::IsCaptureAlignmentSupported(Calibration, AlignmentReason))
		{
			Report.Add(true, FString::Printf(TEXT("Capture cannot be aligned: %s"), *AlignmentReason));
		}

		const FIntPoint Resolution =
			UMinimapFunctionLibrary::ComputeCaptureResolution(Calibration, EffectiveSettings.CaptureResolution);
		if (Resolution.X < 16 || Resolution.Y < 16)
		{
			Report.Add(true, FString::Printf(TEXT("Computed capture resolution %dx%d is unusable."),
				Resolution.X, Resolution.Y));
		}

		if (IsValid(CaptureComponent))
		{
			// The diagnostics dump is where a black-capture problem actually becomes
			// visible, so always emit it alongside the pass/fail list.
			UE_LOG(LogMinimap, Log, TEXT("%s"), *CaptureComponent->GetCaptureDiagnostics());

			const FString& CaptureError = CaptureComponent->GetLastCaptureError();
			if (!CaptureError.IsEmpty())
			{
				Report.Add(true, FString::Printf(TEXT("Last capture error: %s"), *CaptureError));
			}
			if (!CaptureComponent->GetMinimapRenderTarget())
			{
				Report.Add(false, TEXT("Capture component exists but has no render target yet."));
			}
		}
		else
		{
			Report.Add(false, TEXT("Capture mode is selected but no capture component exists yet. "
			                       "It is created when the calibration is applied (BeginPlay)."));
		}

		// The two settings that most often produce a black capture.
		if (EffectiveSettings.CaptureDepth > 0.0f)
		{
			const float CameraZ = IsValid(CaptureComponent)
				? CaptureComponent->ResolveCaptureHeight(Calibration)
				: Calibration.MaxZ + EffectiveSettings.AutoHeightMargin;
			const float RequiredDepth = FMath::Max(CameraZ - Calibration.MinZ, 1.0f);

			if (EffectiveSettings.CaptureDepth < RequiredDepth)
			{
				Report.Add(true, FString::Printf(
					TEXT("Capture Depth (%.0f cm) is less than the camera's height above the bounds "
					     "floor (%.0f cm). The level is culled and the map renders black. Set Capture "
					     "Depth to 0 for automatic."),
					EffectiveSettings.CaptureDepth, RequiredDepth));
			}
		}

		if (EffectiveSettings.ExposureMode == EMinimapCaptureExposureMode::Manual)
		{
			Report.Add(false, TEXT("Exposure Mode is Manual, which ignores scene lighting. A dimly lit "
			                       "interior can capture as black even though the game view looks "
			                       "correct. Use Inherit Scene unless you have tuned the bias."));
		}

		if (EffectiveSettings.bUseShowOnlyList &&
		    EffectiveSettings.InclusionTags.Num() == 0 && CaptureIncludedActors.Num() == 0)
		{
			Report.Add(true, TEXT("Show-only mode is on but nothing is included, so the capture will "
			                      "render an empty scene."));
		}
	}

	// --- Widget bindings --------------------------------------------------
	// Checked against the live widget when one exists; a widget is not required for the
	// bounds volume itself to be correct.
	if (const UWorld* World = GetWorld())
	{
		bool bFoundWidget = false;
		for (TObjectIterator<UMinimapWidgetBase> It; It; ++It)
		{
			UMinimapWidgetBase* Widget = *It;
			if (!IsValid(Widget) || Widget->GetWorld() != World || Widget->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			bFoundWidget = true;

			if (!Widget->Background)
			{
				Report.Add(false, FString::Printf(
					TEXT("Widget '%s' has no 'Background' image bound; the map image cannot be driven."),
					*Widget->GetName()));
			}
			if (!Widget->MarkerCanvas)
			{
				Report.Add(false, FString::Printf(
					TEXT("Widget '%s' has no 'MarkerCanvas' canvas bound; markers will not be placed."),
					*Widget->GetName()));
			}
			if (!Widget->GetCachedMapMaterial() &&
			    EffectiveSettings.BackgroundSource == EMinimapBackgroundSource::SceneCapture)
			{
				Report.Add(false, FString::Printf(
					TEXT("Widget '%s' has no dynamic material; capture will fall back to the image brush."),
					*Widget->GetName()));
			}
		}

		if (!bFoundWidget && World->IsGameWorld())
		{
			Report.Add(false, TEXT("No UMinimapWidgetBase instance found in this world."));
		}
	}

	UE_LOG(LogMinimap, Log, TEXT("'%s' validation:\n%s"), *GetName(), *Report.ToDisplayString());
	return Report;
}

#if WITH_EDITOR

void AMinimapBoundsVolume::FitToLevelBounds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FBox LevelBounds(ForceInit);
	int32 ConsideredActors = 0;
	int32 RejectedActors = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == this)
		{
			continue;
		}

		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);

		if (!PassesFitFilter(Actor, Extent))
		{
			++RejectedActors;
			continue;
		}

		LevelBounds += FBox(Origin - Extent, Origin + Extent);
		++ConsideredActors;
	}

	if (ConsideredActors == 0 || !LevelBounds.IsValid)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': FitToLevelBounds found no actors passing the filters (%d rejected); "
			     "volume unchanged. Relax Minimap|Fit settings if this is unexpected."),
			*GetName(), RejectedActors);
		return;
	}

	Modify();
	if (IsValid(BoundsBox))
	{
		BoundsBox->Modify();
	}

	// Reset rotation and scale so the captured extent means exactly what it says; a
	// residual scale here would silently double the mapped area.
	SetActorRotation(FRotator::ZeroRotator);
	SetActorScale3D(FVector::OneVector);
	SetActorLocation(LevelBounds.GetCenter());

	FVector NewExtent = LevelBounds.GetExtent();
	NewExtent.X += FitPadding;
	NewExtent.Y += FitPadding;
	BoundsBox->SetBoxExtent(NewExtent, /*bUpdateOverlaps=*/false);

	UE_LOG(LogMinimap, Log,
		TEXT("'%s': fitted to %d actors (%d rejected by filters). Center=%s Extent=%s"),
		*GetName(), ConsideredActors, RejectedActors,
		*LevelBounds.GetCenter().ToCompactString(), *NewExtent.ToCompactString());
}

void AMinimapBoundsVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Live-update the calibration while editing in PIE so tweaks are visible immediately.
	// Guarded to play worlds: touching the editor world's subsystem from here would fight
	// with the level's own BeginPlay-time calibration.
	const UWorld* World = GetWorld();
	if (World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
	{
		ApplyCalibration();
	}
}

#endif // WITH_EDITOR
