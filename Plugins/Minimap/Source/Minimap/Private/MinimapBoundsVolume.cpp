#include "MinimapBoundsVolume.h"

#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
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
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "TextureResource.h"
#include "UObject/Package.h"
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

	// Static mode: publish the authored texture through the SAME channel the capture uses,
	// so both sources are previewable and the widget has one code path. Leaving
	// StaticMapTexture empty preserves the original behaviour exactly - the widget simply
	// keeps whatever image M_Minimap already samples.
	if (GetEffectiveCaptureSettings().BackgroundSource == EMinimapBackgroundSource::StaticTexture)
	{
		if (StaticMapTexture)
		{
			Subsystem->SetStaticBackgroundTexture(StaticMapTexture);
		}
		return true;
	}

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
	// legacy static-texture path exactly as cheap as it was. bAllowInStaticMode is set
	// only by the editor preview, which must be able to render a comparison image without
	// changing the configured Background Source.
	if (EffectiveSettings.BackgroundSource != EMinimapBackgroundSource::SceneCapture && !bAllowCaptureInStaticMode)
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

bool AMinimapBoundsVolume::PassesActorFitFilters(const AActor* Actor) const
{
	if (!IsValid(Actor))
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
		for (const FName& Tag : FitRequireTags)
		{
			if (!Tag.IsNone() && Actor->ActorHasTag(Tag))
			{
				return true;
			}
		}
		return false;
	}

	return true;
}

void AMinimapBoundsVolume::FitToGeometryBounds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// One entry per qualifying primitive component: its world box and how much geometry
	// mass it represents. Volume is a good enough proxy for "how much of the building is
	// this" without touching render data.
	struct FGeometryEntry
	{
		FBox Box;
		double Weight;
	};

	TArray<FGeometryEntry> Entries;
	int32 RejectedComponents = 0;

	const double MinVolume = static_cast<double>(MinComponentVolumeCubicMeters) * 1000000.0; // m^3 -> cm^3

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor) || Actor == this || Actor->IsA<AMinimapBoundsVolume>())
		{
			continue;
		}

		// Actor-level filters still apply, so tags and classes exclude exactly as they do
		// for the actor-bounds fit. Expressed as a single predicate rather than jumps.
		if (!PassesActorFitFilters(Actor))
		{
			continue;
		}

		// --- Component level ---------------------------------------------
		Actor->ForEachComponent<UPrimitiveComponent>(/*bIncludeFromChildActors=*/true,
			[&](UPrimitiveComponent* Primitive)
			{
				if (!IsValid(Primitive) || !Primitive->IsRegistered())
				{
					return;
				}

				// Only things that actually carry architectural geometry. This is the core
				// of "mass-bearing": a mesh has volume, a light or an audio emitter does not.
				const bool bIsMesh = Primitive->IsA<UStaticMeshComponent>() || Primitive->IsA<USkeletalMeshComponent>();
				if (!bIsMesh)
				{
					++RejectedComponents;
					return;
				}

				if (bGeometryFitRequiresCollision &&
				    Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				{
					++RejectedComponents;
					return;
				}

				const FBoxSphereBounds ComponentBounds = Primitive->Bounds;
				const FVector Extent = ComponentBounds.BoxExtent;
				if (Extent.IsNearlyZero())
				{
					++RejectedComponents;
					return;
				}

				// Guard against a single component with absurd bounds (sky spheres,
				// unbounded effects) before it can dominate the weighting.
				if (FitMaxActorExtent > 0.0f &&
				    (Extent.X > FitMaxActorExtent || Extent.Y > FitMaxActorExtent))
				{
					++RejectedComponents;
					return;
				}

				const double Volume = 8.0 * Extent.X * Extent.Y * Extent.Z;
				if (Volume < MinVolume)
				{
					++RejectedComponents;
					return;
				}

				FGeometryEntry Entry;
				Entry.Box = FBox(ComponentBounds.Origin - Extent, ComponentBounds.Origin + Extent);
				Entry.Weight = Volume;
				Entries.Add(Entry);
			});
	}

	if (Entries.Num() == 0)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': Fit To Geometry found no qualifying mesh components (%d rejected). "
			     "Lower Min Component Volume, or turn off Requires Collision."),
			*GetName(), RejectedComponents);
		return;
	}

	// --- Weighted outlier trim -------------------------------------------
	// Sort by centre along an axis, walk the cumulative weight, and take the value where
	// the trim fraction is crossed at each end. Weighting by volume means the result
	// follows where the building actually is, not where the most objects happen to be.
	double TotalWeight = 0.0;
	for (const FGeometryEntry& Entry : Entries)
	{
		TotalWeight += Entry.Weight;
	}

	const double Trim = FMath::Clamp(static_cast<double>(GeometryOutlierTrim), 0.0, 0.25);

	auto WeightedRange = [&Entries, TotalWeight, Trim](int32 Axis, double& OutMin, double& OutMax)
	{
		TArray<FGeometryEntry> Sorted = Entries;
		Sorted.Sort([Axis](const FGeometryEntry& A, const FGeometryEntry& B)
		{
			return A.Box.GetCenter()[Axis] < B.Box.GetCenter()[Axis];
		});

		const double LowTarget  = TotalWeight * Trim;
		const double HighTarget = TotalWeight * (1.0 - Trim);

		double Accumulated = 0.0;
		OutMin = Sorted[0].Box.Min[Axis];
		OutMax = Sorted.Last().Box.Max[Axis];

		bool bFoundLow = (Trim <= 0.0);
		for (const FGeometryEntry& Entry : Sorted)
		{
			const double Before = Accumulated;
			Accumulated += Entry.Weight;

			if (!bFoundLow && Accumulated >= LowTarget)
			{
				// Keep this component whole: trimming should exclude outliers, never
				// slice through a wall that survived the cut.
				OutMin = Entry.Box.Min[Axis];
				bFoundLow = true;
			}
			if (Before < HighTarget && Accumulated >= HighTarget)
			{
				OutMax = Entry.Box.Max[Axis];
			}
		}
	};

	double MinX, MaxX, MinY, MaxY;
	WeightedRange(0, MinX, MaxX);
	WeightedRange(1, MinY, MaxY);

	// Z is not trimmed: a roof or a basement is legitimately part of the building, and the
	// Z range only feeds height filtering and the capture camera.
	FBox FullZ(ForceInit);
	for (const FGeometryEntry& Entry : Entries)
	{
		FullZ += Entry.Box;
	}

	FBox Fitted(FVector(MinX, MinY, FullZ.Min.Z), FVector(MaxX, MaxY, FullZ.Max.Z));
	if (!Fitted.IsValid || Fitted.GetExtent().X < 1.0 || Fitted.GetExtent().Y < 1.0)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': Fit To Geometry produced a degenerate box; volume unchanged."), *GetName());
		return;
	}

	Modify();
	if (IsValid(BoundsBox))
	{
		BoundsBox->Modify();
	}

	SetActorRotation(FRotator::ZeroRotator);
	SetActorScale3D(FVector::OneVector);
	SetActorLocation(Fitted.GetCenter());

	FVector NewExtent = Fitted.GetExtent();
	NewExtent.X += FitPadding;
	NewExtent.Y += FitPadding;
	BoundsBox->SetBoxExtent(NewExtent, /*bUpdateOverlaps=*/false);

	const FBox Untrimmed = [&Entries]()
	{
		FBox Box(ForceInit);
		for (const FGeometryEntry& Entry : Entries) { Box += Entry.Box; }
		return Box;
	}();

	UE_LOG(LogMinimap, Log,
		TEXT("'%s': fitted to geometry. %d components kept, %d rejected. "
		     "Trimmed extent %s vs untrimmed %s (%.0f%% smaller on X, %.0f%% on Y)."),
		*GetName(), Entries.Num(), RejectedComponents,
		*NewExtent.ToCompactString(), *Untrimmed.GetExtent().ToCompactString(),
		100.0 * (1.0 - NewExtent.X / FMath::Max(Untrimmed.GetExtent().X, 1.0)),
		100.0 * (1.0 - NewExtent.Y / FMath::Max(Untrimmed.GetExtent().Y, 1.0)));
}

void AMinimapBoundsVolume::SaveCaptureAsStaticTexture()
{
	UMinimapCaptureComponent* Capture = IsValid(CaptureComponent) ? CaptureComponent.Get() : nullptr;
	if (!Capture || !Capture->GetMinimapRenderTarget())
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': nothing to save - press Capture Preview Now first so a render target exists."),
			*GetName());
		return;
	}

	if (Capture->GetCaptureCallCount() == 0)
	{
		UE_LOG(LogMinimap, Warning,
			TEXT("'%s': the render target has never been captured into; saving it would bake a "
			     "blank image. Press Capture Preview Now first."), *GetName());
		return;
	}

	FString AssetName = StaticTextureAssetName;
	if (AssetName.IsEmpty())
	{
		// Per-level default, so two levels cannot silently overwrite each other's map.
		const FString LevelName = GetWorld() ? GetWorld()->GetMapName() : TEXT("Level");
		AssetName = FString::Printf(TEXT("T_Minimap_%s"), *LevelName);
	}

	FString PackagePath = StaticTextureSavePath;
	PackagePath.RemoveFromEnd(TEXT("/"));
	const FString FullName = FString::Printf(TEXT("%s/%s"), *PackagePath, *AssetName);

	// --- Read the pixels back -------------------------------------------
	// Building the texture from CPU pixels rather than calling
	// RenderTargetCreateStaticTexture2DEditorOnly is deliberate. That helper derives the
	// source gamma space and the per-mip gamma space independently, and when they diverge
	// the texture build fires:
	//
	//   Assertion failed: MipView.GammaSpace == LayerData.SourceGammaSpace
	//   [TextureDerivedDataTask.cpp]
	//
	// Here there is exactly ONE mip and ONE layer, and the sRGB flag is declared once to
	// match how the bytes are actually encoded, so those two values come from the same
	// declaration and cannot disagree.
	UTextureRenderTarget2D* RenderTarget = Capture->GetMinimapRenderTarget();
	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		UE_LOG(LogMinimap, Error,
			TEXT("'%s': the render target has no RHI resource yet; capture again before baking."),
			*GetName());
		return;
	}

	TArray<FColor> Pixels;

	// The capture target stores sRGB-encoded bytes (RTF_RGBA8_SRGB), so ask for them
	// verbatim. Leaving the default linear-to-gamma conversion on would encode a second
	// time and wash the map out.
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);

	if (!Resource->ReadPixels(Pixels, ReadFlags) || Pixels.Num() == 0)
	{
		UE_LOG(LogMinimap, Error, TEXT("'%s': could not read the render target back."), *GetName());
		return;
	}

	const int32 Width  = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;
	if (Pixels.Num() != Width * Height)
	{
		UE_LOG(LogMinimap, Error,
			TEXT("'%s': read %d pixels but expected %dx%d = %d. Aborting rather than writing a "
			     "malformed asset."), *GetName(), Pixels.Num(), Width, Height, Width * Height);
		return;
	}

	// --- Package and asset ------------------------------------------------
	UPackage* Package = CreatePackage(*FullName);
	if (!Package)
	{
		UE_LOG(LogMinimap, Error, TEXT("'%s': could not create package '%s'."), *GetName(), *FullName);
		return;
	}
	Package->FullyLoad();

	// Reuse an existing asset of the same name so re-baking updates in place instead of
	// leaving a trail of T_Minimap_X_1, _2, _3.
	UTexture2D* Baked = FindObject<UTexture2D>(Package, *AssetName);
	const bool bCreatedNew = (Baked == nullptr);
	if (bCreatedNew)
	{
		Baked = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	}

	if (!Baked)
	{
		UE_LOG(LogMinimap, Error, TEXT("'%s': could not create texture '%s'."), *GetName(), *FullName);
		return;
	}

	Baked->Modify();

	// FColor is B,G,R,A in memory, so TSF_BGRA8 matches the readback with no shuffling.
	Baked->Source.Init(Width, Height, /*NumSlices=*/1, /*NumMips=*/1, TSF_BGRA8,
		reinterpret_cast<const uint8*>(Pixels.GetData()));

	// The single gamma declaration the whole build derives from.
	Baked->SRGB = bStaticTextureSRGB;
	Baked->CompressionSettings = StaticTextureCompression;
	Baked->MipGenSettings = StaticTextureMipGen;
	Baked->AddressX = TA_Clamp;
	Baked->AddressY = TA_Clamp;
	Baked->NeverStream = true;

	Baked->UpdateResource();
	Baked->PostEditChange();

	Package->MarkPackageDirty();
	if (bCreatedNew)
	{
		FAssetRegistryModule::AssetCreated(Baked);
	}

	Modify();
	StaticMapTexture = Baked;

	if (bSwitchToStaticAfterSave)
	{
		// Switching source is the point of the workflow, but only ever on the per-instance
		// override, never on a shared preset that other levels also use.
		bOverridePresetCaptureSettings = true;
		CaptureSettingsOverride.BackgroundSource = EMinimapBackgroundSource::StaticTexture;

		if (UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this))
		{
			Subsystem->SetStaticBackgroundTexture(Baked);
		}
	}

	UE_LOG(LogMinimap, Log,
		TEXT("'%s': baked %dx%d capture into '%s' (%s, sRGB=%s)%s The package is DIRTY - "
		     "save it (Ctrl+S or File > Save All) to keep it."),
		*GetName(), Width, Height, *Baked->GetPathName(),
		bCreatedNew ? TEXT("new asset") : TEXT("updated in place"),
		bStaticTextureSRGB ? TEXT("true") : TEXT("false"),
		bSwitchToStaticAfterSave ? TEXT(" Background Source switched to Static Texture.") : TEXT(""));
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


// ---------------------------------------------------------------------------
// Editor / Blueprint preview
// ---------------------------------------------------------------------------

bool AMinimapBoundsVolume::CapturePreviewNow()
{
	LastPreviewError.Reset();

    // Allow the component to exist even in Static Texture mode, purely so the two sources
    // can be compared side by side. This flag never touches Background Source itself.
	TGuardValue<bool> AllowGuard(bAllowCaptureInStaticMode, true);

	UMinimapCaptureComponent* Capture = EnsureCaptureComponent();
	if (!Capture)
	{
		LastPreviewError = TEXT("Could not create the capture component (dedicated server, or no world).");
		UE_LOG(LogMinimap, Warning, TEXT("'%s': %s"), *GetName(), *LastPreviewError);
		return false;
	}

	const FMinimapCalibration Calibration = BuildCalibration();

	FString Error;
	if (!Capture->CaptureForPreview(Calibration, Error))
	{
		LastPreviewError = Error;
		UE_LOG(LogMinimap, Warning, TEXT("'%s': preview capture failed - %s"), *GetName(), *Error);
		UE_LOG(LogMinimap, Log, TEXT("%s"), *Capture->GetCaptureDiagnostics());
		return false;
	}

	// Prove what actually landed in the render target rather than assuming.
	float MeanLuminance = 0.0f;
	float MaxLuminance = 0.0f;
	int32 NonBlackPixels = 0;
	FString ProbeSummary;
	if (Capture->ProbeRenderTarget(MeanLuminance, MaxLuminance, NonBlackPixels, ProbeSummary))
	{
		UE_LOG(LogMinimap, Log, TEXT("'%s': %s"), *GetName(), *ProbeSummary);
		if (NonBlackPixels == 0)
		{
			LastPreviewError = TEXT("Capture ran but the render target is black. See LogMinimap for the "
			                        "pipeline report.");
			UE_LOG(LogMinimap, Log, TEXT("%s"), *Capture->GetCaptureDiagnostics());
		}
	}

	return true;
}

UTextureRenderTarget2D* AMinimapBoundsVolume::GetPreviewRenderTarget() const
{
	return IsValid(CaptureComponent) ? CaptureComponent->GetMinimapRenderTarget() : nullptr;
}

EMinimapBackgroundSource AMinimapBoundsVolume::GetActiveBackgroundSource() const
{
	return GetEffectiveCaptureSettings().BackgroundSource;
}

FString AMinimapBoundsVolume::GetPreviewStatusText() const
{
	if (!IsValid(CaptureComponent))
	{
		return TEXT("No capture component yet - press Capture Preview Now.");
	}
	if (!CaptureComponent->GetMinimapRenderTarget())
	{
		return TEXT("Capture component exists but no render target has been created.");
	}
	if (CaptureComponent->GetCaptureCallCount() == 0)
	{
		return TEXT("Render target allocated but never captured into - the image is the clear colour.");
	}
	if (!LastPreviewError.IsEmpty())
	{
		return FString::Printf(TEXT("Last attempt reported: %s"), *LastPreviewError);
	}

	const UTextureRenderTarget2D* RT = CaptureComponent->GetMinimapRenderTarget();
	return FString::Printf(TEXT("Captured %d time(s) - %dx%d."),
		CaptureComponent->GetCaptureCallCount(), RT->SizeX, RT->SizeY);
}
