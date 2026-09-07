#include "MinimapCaptureComponent.h"

#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MinimapFunctionLibrary.h"
#include "MinimapModule.h"
#include "TimerManager.h"

UMinimapCaptureComponent::UMinimapCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Explicit capture only. These two flags are the difference between a minimap that
	// costs one render per refresh and one that costs a full scene render every frame.
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;

	ProjectionType = ECameraProjectionMode::Orthographic;

	// FinalColorLDR gives the post-processed image, which reads far better than scene
	// colour for a map. Alpha variant is selected in ApplyVisualDefaults when requested.
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	// Never let the capture contribute to gameplay visibility or bounds.
	bAutoActivate = true;
	SetHiddenInGame(true);
}

void UMinimapCaptureComponent::OnRegister()
{
	Super::OnRegister();

	// Enforce the invariants even if a Blueprint or preset flipped them.
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;
	ProjectionType = ECameraProjectionMode::Orthographic;
}

void UMinimapCaptureComponent::BeginPlay()
{
	Super::BeginPlay();
	Settings.SanitizeInPlace();
}

void UMinimapCaptureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseCaptureResources();
	Super::EndPlay(EndPlayReason);
}

bool UMinimapCaptureComponent::IsDedicatedServerWorld() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
}

// ---------------------------------------------------------------------------
// Calibration -> camera
// ---------------------------------------------------------------------------

bool UMinimapCaptureComponent::ApplyCalibration(const FMinimapCalibration& Calibration, FString& OutError)
{
	OutError.Reset();
	Settings.SanitizeInPlace();

	if (IsDedicatedServerWorld())
	{
		// Not an error - there is simply nothing to render for, and allocating a render
		// target on a dedicated server would waste memory for no observer.
		OutError = TEXT("Dedicated server: minimap capture resources are intentionally not allocated.");
		LastCaptureError = OutError;
		return false;
	}

	// The single alignment gate. A mirrored axis convention is reported rather than
	// silently producing a flipped map.
	float CaptureYaw = 0.0f;
	if (!UMinimapFunctionLibrary::ComputeCaptureYaw(Calibration, CaptureYaw, OutError))
	{
		LastCaptureError = OutError;
		UE_LOG(LogMinimap, Warning, TEXT("MinimapCapture on '%s': %s"), *GetNameSafe(GetOwner()), *OutError);
		return false;
	}

	const FIntPoint DesiredSize = UMinimapFunctionLibrary::ComputeCaptureResolution(Calibration, Settings.CaptureResolution);
	if (!EnsureRenderTarget(DesiredSize))
	{
		OutError = FString::Printf(TEXT("Could not create a %dx%d render target."), DesiredSize.X, DesiredSize.Y);
		LastCaptureError = OutError;
		return false;
	}

	AppliedCalibration = Calibration;

	// --- Placement --------------------------------------------------------
	const float CaptureZ = ResolveCaptureHeight(Calibration);
	const FVector CaptureLocation(Calibration.WorldCenter.X, Calibration.WorldCenter.Y, CaptureZ);

	// Pitch -90 looks straight down. With Roll 0 the camera's up vector in world is
	// (cos Yaw, sin Yaw, 0), which is exactly what ComputeCaptureYaw solved for.
	SetWorldLocationAndRotation(CaptureLocation, FRotator(-90.0f, CaptureYaw, 0.0f));

	// --- Coverage ---------------------------------------------------------
	// OrthoWidth spans the image's horizontal axis. The vertical extent follows from the
	// render target aspect, which ComputeCaptureResolution matched to the world aspect -
	// so non-square bounds are covered exactly, with no stretch and no crop.
	OrthoWidth = UMinimapFunctionLibrary::GetCaptureOrthoWidth(Calibration);

	// Render from the camera down to the bounds floor (plus a margin), or the explicit
	// depth when one is set. This is what lets a user slice off lower floors.
	const float AutoDepth = FMath::Max(CaptureZ - Calibration.MinZ, 1.0f) + 100.0f;
	MaxViewDistanceOverride = (Settings.CaptureDepth > 0.0f) ? Settings.CaptureDepth : AutoDepth;

	ApplyVisualDefaults();

	bCalibrationApplied = true;
	LastCaptureError.Reset();

	UE_LOG(LogMinimap, Log,
		TEXT("MinimapCapture on '%s' aligned: Center=(%.1f, %.1f) Z=%.1f Yaw=%.2f OrthoWidth=%.1f RT=%dx%d Depth=%.1f"),
		*GetNameSafe(GetOwner()), Calibration.WorldCenter.X, Calibration.WorldCenter.Y,
		CaptureZ, CaptureYaw, OrthoWidth, DesiredSize.X, DesiredSize.Y, MaxViewDistanceOverride);

	return true;
}

float UMinimapCaptureComponent::ResolveCaptureHeight(const FMinimapCalibration& Calibration) const
{
	switch (Settings.HeightMode)
	{
	case EMinimapCaptureHeightMode::ManualWorldHeight:
		return Settings.ManualCaptureHeight;

	case EMinimapCaptureHeightMode::RelativeWithinBounds:
		return FMath::Lerp(Calibration.MinZ, Calibration.MaxZ, Settings.RelativeHeightAlpha);

	case EMinimapCaptureHeightMode::AutoAboveGeometry:
		{
			// Above the tallest thing actually in the bounds, so furniture is seen from
			// above rather than the camera sitting inside it.
			const float GeometryTop = ScanGeometryTop(Calibration);
			return FMath::Max(GeometryTop, Calibration.MaxZ) + Settings.AutoHeightMargin;
		}

	case EMinimapCaptureHeightMode::AutoAboveBounds:
	default:
		return Calibration.MaxZ + Settings.AutoHeightMargin;
	}
}

float UMinimapCaptureComponent::ScanGeometryTop(const FMinimapCalibration& Calibration) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return Calibration.MaxZ;
	}

	// One iteration per refresh - never per frame. Documented as such.
	const FVector2D Extent = Calibration.GetEffectiveExtent();
	const double RadiusSq = FMath::Square(FMath::Max(Extent.X, Extent.Y));

	float Highest = Calibration.MinZ;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == GetOwner())
		{
			continue;
		}

		FVector Origin;
		FVector ActorExtent;
		Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, ActorExtent);
		if (ActorExtent.IsNearlyZero())
		{
			continue;
		}

		// Cheap planar radius test; exact containment is not needed for a height estimate.
		const double PlanarDistSq = FVector2D::DistSquared(FVector2D(Origin.X, Origin.Y), Calibration.WorldCenter);
		if (PlanarDistSq > RadiusSq)
		{
			continue;
		}

		Highest = FMath::Max(Highest, static_cast<float>(Origin.Z + ActorExtent.Z));
	}

	return Highest;
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

bool UMinimapCaptureComponent::EnsureRenderTarget(const FIntPoint& DesiredSize)
{
	if (DesiredSize.X < 16 || DesiredSize.Y < 16)
	{
		return false;
	}

	// Reuse when the size is unchanged. Recreating the target every refresh would churn
	// GPU memory and drop a frame each time.
	if (IsValid(MinimapRenderTarget) &&
	    MinimapRenderTarget->SizeX == DesiredSize.X &&
	    MinimapRenderTarget->SizeY == DesiredSize.Y)
	{
		TextureTarget = MinimapRenderTarget;
		return true;
	}

	// Outered to this component, so it is GC-reachable exactly as long as the component
	// is, and released with it. The UPROPERTY below is what actually keeps it rooted.
	UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(this);
	if (!NewTarget)
	{
		return false;
	}

	NewTarget->RenderTargetFormat = Settings.bCaptureAlpha ? RTF_RGBA8 : RTF_RGBA8_SRGB;
	NewTarget->ClearColor = Settings.ClearColor;
	NewTarget->bAutoGenerateMips = false;
	NewTarget->bGPUSharedFlag = false;
	NewTarget->InitAutoFormat(DesiredSize.X, DesiredSize.Y);
	NewTarget->UpdateResourceImmediate(true);

	MinimapRenderTarget = NewTarget;
	TextureTarget = NewTarget;

	// A resized target invalidates whatever was drawn before.
	bHasCaptured = false;

	return true;
}

void UMinimapCaptureComponent::ReleaseCaptureResources()
{
	if (const UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		Timers.ClearTimer(CoalesceTimerHandle);
		Timers.ClearTimer(PeriodicTimerHandle);
	}

	CoalesceTimerHandle.Invalidate();
	PeriodicTimerHandle.Invalidate();

	TextureTarget = nullptr;
	MinimapRenderTarget = nullptr; // GC reclaims it once nothing else references it.

	bRefreshQueued = false;
	bHasCaptured = false;
	bCalibrationApplied = false;
}

void UMinimapCaptureComponent::ApplyVisualDefaults()
{
	// FinalColorLDR is the post-processed image, which reads far better as a map than raw
	// scene colour. NOTE: it does not carry meaningful alpha - bCaptureAlpha only selects
	// an alpha-capable render target format, so uncovered areas still come back as the
	// clear colour rather than transparent. Documented as a limitation.
	CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	if (!Settings.bUseFlatCaptureLook)
	{
		return;
	}

	// A readable map wants a stable, flat image. Auto exposure would make the map's
	// brightness drift with whatever the capture happens to frame, and the lens effects
	// below only blur furniture silhouettes.
	FPostProcessSettings& PP = PostProcessSettings;

	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;

	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = Settings.ExposureBias;

	PP.bOverride_MotionBlurAmount = true;
	PP.MotionBlurAmount = 0.0f;

	// 0 focal distance disables UE5's diaphragm DOF.
	PP.bOverride_DepthOfFieldFocalDistance = true;
	PP.DepthOfFieldFocalDistance = 0.0f;

	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.0f;

	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.0f;

	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = 0.0f;

	PostProcessBlendWeight = 1.0f;
}

// ---------------------------------------------------------------------------
// Visibility filtering
// ---------------------------------------------------------------------------

void UMinimapCaptureComponent::ApplyVisibilityFilters()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	HiddenActors.Reset();
	ShowOnlyActors.Reset();

	// Scene-capture visibility lists affect ONLY this capture. Actor visibility in the
	// main game view is never touched, which is the point: a roof can vanish from the
	// minimap while still rendering normally for the player.
	const bool bShowOnly = Settings.bUseShowOnlyList;
	PrimitiveRenderMode = bShowOnly
		? ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList
		: ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

	// Explicit level references first - cheap and unambiguous.
	for (const TSoftObjectPtr<AActor>& SoftActor : ExcludedActors)
	{
		if (AActor* Actor = SoftActor.Get())
		{
			HiddenActors.Add(Actor);
		}
	}

	if (bShowOnly)
	{
		for (const TSoftObjectPtr<AActor>& SoftActor : IncludedActors)
		{
			if (AActor* Actor = SoftActor.Get())
			{
				ShowOnlyActors.Add(Actor);
			}
		}
	}

	// Tag resolution needs an actor iteration. Done here, on refresh only.
	const bool bNeedsTagScan = Settings.ExclusionTags.Num() > 0 || (bShowOnly && Settings.InclusionTags.Num() > 0);
	if (bNeedsTagScan)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			for (const FName& Tag : Settings.ExclusionTags)
			{
				if (!Tag.IsNone() && Actor->ActorHasTag(Tag))
				{
					HiddenActors.AddUnique(Actor);
					break;
				}
			}

			if (bShowOnly)
			{
				for (const FName& Tag : Settings.InclusionTags)
				{
					if (!Tag.IsNone() && Actor->ActorHasTag(Tag))
					{
						ShowOnlyActors.AddUnique(Actor);
						break;
					}
				}
			}
		}
	}

	// The local pawn would otherwise appear as a blob under its own marker.
	if (Settings.bHideLocalPlayerPawn)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					HiddenActors.AddUnique(Pawn);
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void UMinimapCaptureComponent::RequestBackgroundRefresh()
{
	if (!IsCaptureEnabled() || IsDedicatedServerWorld())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Coalescing: the first request arms a one-shot timer, later requests inside the
	// window are absorbed. A hundred furniture edits therefore cost one capture.
	if (bRefreshQueued)
	{
		return;
	}

	bRefreshQueued = true;

	const float Delay = FMath::Max(Settings.RefreshCoalesceSeconds, 0.0f);
	if (Delay <= UE_KINDA_SMALL_NUMBER)
	{
		HandleCoalescedRefresh();
		return;
	}

	World->GetTimerManager().SetTimer(
		CoalesceTimerHandle, this, &UMinimapCaptureComponent::HandleCoalescedRefresh, Delay, /*bLoop=*/false);
}

void UMinimapCaptureComponent::HandleCoalescedRefresh()
{
	bRefreshQueued = false;
	RefreshBackgroundImmediate();
}

bool UMinimapCaptureComponent::RefreshBackgroundImmediate()
{
	if (!IsCaptureEnabled())
	{
		return false;
	}

	if (IsDedicatedServerWorld())
	{
		return false;
	}

	if (!bCalibrationApplied)
	{
		// A refresh before calibration is a real ordering problem worth reporting, but it
		// resolves itself as soon as the bounds volume applies its calibration.
		LastCaptureError = TEXT("Refresh requested before a calibration was applied.");
		UE_LOG(LogMinimap, Verbose, TEXT("MinimapCapture on '%s': %s"),
			*GetNameSafe(GetOwner()), *LastCaptureError);
		return false;
	}

	if (!IsValid(MinimapRenderTarget))
	{
		LastCaptureError = TEXT("Refresh requested with no render target.");
		return false;
	}

	ApplyVisibilityFilters();

	TextureTarget = MinimapRenderTarget;
	CaptureScene();

	bHasCaptured = true;
	LastCaptureError.Reset();

	OnBackgroundCaptured.Broadcast(this, MinimapRenderTarget);
	return true;
}

void UMinimapCaptureComponent::ApplyCaptureSettingsAndRefresh()
{
	Settings.SanitizeInPlace();

	if (bCalibrationApplied)
	{
		// Re-applying may resize the render target (resolution changed) or move the camera
		// (height policy changed), so it must happen before the capture.
		FString Error;
		ApplyCalibration(AppliedCalibration, Error);
	}

	UpdatePeriodicTimer();
	RequestBackgroundRefresh();
}

void UMinimapCaptureComponent::UpdatePeriodicTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	Timers.ClearTimer(PeriodicTimerHandle);

	if (Settings.RefreshPolicy != EMinimapRefreshPolicy::ThrottledPeriodic || !IsCaptureEnabled())
	{
		return;
	}

	// Deliberately opt-in: a periodic full-scene render is the most expensive thing this
	// plugin can do, so it is never on by default.
	Timers.SetTimer(PeriodicTimerHandle, this, &UMinimapCaptureComponent::RequestBackgroundRefresh,
		FMath::Max(Settings.PeriodicRefreshInterval, 0.5f), /*bLoop=*/true);
}
