#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "MinimapCaptureTypes.generated.h"

/** Where the minimap background image comes from. */
UENUM(BlueprintType)
enum class EMinimapBackgroundSource : uint8
{
	/** Existing behaviour: the map image is whatever the widget's material already samples. */
	StaticTexture	UMETA(DisplayName = "Static Texture (Legacy)"),

	/** A SceneCapture2D renders the level top-down into a render target at runtime. */
	SceneCapture	UMETA(DisplayName = "Automatic Scene Capture")
};

/** How the capture camera's world Z is chosen. */
UENUM(BlueprintType)
enum class EMinimapCaptureHeightMode : uint8
{
	/**
	 * Bounds top + AutoHeightMargin. The margin exists so the camera sits ABOVE furniture
	 * rather than inside it. This is placement relative to the BOUNDS, not to a per-actor
	 * geometry scan - see AutoHeightMode for the scanned variant.
	 */
	AutoAboveBounds			UMETA(DisplayName = "Auto - Above Bounds Top"),

	/**
	 * Scans the tallest eligible actor inside the bounds once per refresh and places the
	 * camera above it. Costs one actor iteration per refresh, never per frame.
	 */
	AutoAboveGeometry		UMETA(DisplayName = "Auto - Above Scanned Geometry"),

	/** Explicit world-space Z. */
	ManualWorldHeight		UMETA(DisplayName = "Manual World Height"),

	/** Lerp(MinZ, MaxZ, RelativeHeightAlpha). Advanced; useful for picking a floor. */
	RelativeWithinBounds	UMETA(DisplayName = "Relative Within Bounds (Advanced)")
};

/** When the background is re-rendered. */
UENUM(BlueprintType)
enum class EMinimapRefreshPolicy : uint8
{
	/** Only when something explicitly calls RequestBackgroundRefresh(). */
	Manual				UMETA(DisplayName = "Manual Only"),

	/** One capture once the level reports ready, then manual. This is the default. */
	CaptureOnceWhenReady UMETA(DisplayName = "Capture Once When Ready"),

	/** Capture once when ready, then on a throttled timer. Off by default; costs GPU. */
	ThrottledPeriodic	UMETA(DisplayName = "Throttled Periodic (Advanced)")
};

/**
 * How the capture's exposure is decided.
 *
 * This is the single most common cause of a black capture. Forcing manual exposure means
 * the image no longer reacts to the scene's lighting, and an interior lit well below the
 * manual EV renders as black even though the game view looks correct.
 */
UENUM(BlueprintType)
enum class EMinimapCaptureExposureMode : uint8
{
	/**
	 * Do not touch exposure at all - the capture inherits the scene's post-process, so it
	 * looks like the game does. This is the default and the right answer for almost every
	 * project, interiors especially.
	 */
	InheritScene		UMETA(DisplayName = "Inherit Scene (Recommended)"),

	/**
	 * Auto exposure with min == max, which pins it to one fixed brightness. Stable frame
	 * to frame without being disconnected from the scene's lighting.
	 */
	FixedAutoExposure	UMETA(DisplayName = "Fixed Auto Exposure"),

	/**
	 * AEM_Manual driven by the physical camera settings. Fully deterministic, but you must
	 * dial in the bias for your lighting or the result can be black or blown out.
	 */
	Manual				UMETA(DisplayName = "Manual (Advanced)")
};

/** How the captured texture reaches the widget's Background image. */
UENUM(BlueprintType)
enum class EMinimapBackgroundApplyMode : uint8
{
	/**
	 * Set a texture parameter on the cached MID. Keeps M_Minimap's pan/rotation working.
	 * Requires the material to expose a texture parameter with the configured name.
	 */
	MaterialParameter	UMETA(DisplayName = "Material Texture Parameter"),

	/**
	 * Replace the Image brush outright. Always displays something, but bypasses the
	 * material, so PlayerX / PlayerY / MapRotation stop affecting the background.
	 */
	ImageBrush			UMETA(DisplayName = "Image Brush (No Material)"),

	/** Try the material parameter; fall back to the brush if the material lacks it. */
	Automatic			UMETA(DisplayName = "Automatic (Parameter, then Brush)")
};

/**
 * Portable capture configuration. Contains NO level-specific actor references, so it can
 * live in a UMinimapPresetAsset and be reused across levels and projects. Per-level actor
 * references (excluded/included actors) live on AMinimapBoundsVolume instead.
 */
USTRUCT(BlueprintType)
struct MINIMAP_API FMinimapCaptureSettings
{
	GENERATED_BODY()

	// --- Source -----------------------------------------------------------

	/** Defaults to StaticTexture so existing projects behave exactly as before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture")
	EMinimapBackgroundSource BackgroundSource = EMinimapBackgroundSource::StaticTexture;

	/**
	 * Longest edge of the render target, in pixels. The other edge is derived from the
	 * bounds aspect ratio so the image is never stretched.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture",
		meta = (ClampMin = "64", ClampMax = "4096", UIMin = "256", UIMax = "2048"))
	int32 CaptureResolution = 1024;

	// --- Height -----------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Height")
	EMinimapCaptureHeightMode HeightMode = EMinimapCaptureHeightMode::AutoAboveBounds;

	/** Clearance added above the reference height so the camera is not inside furniture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Height",
		meta = (ClampMin = "0.0", Units = "cm"))
	float AutoHeightMargin = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Height",
		meta = (EditCondition = "HeightMode == EMinimapCaptureHeightMode::ManualWorldHeight", Units = "cm"))
	float ManualCaptureHeight = 0.0f;

	/** 0 = bounds floor, 1 = bounds ceiling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Height",
		meta = (EditCondition = "HeightMode == EMinimapCaptureHeightMode::RelativeWithinBounds",
			ClampMin = "0.0", ClampMax = "1.0"))
	float RelativeHeightAlpha = 1.0f;

	/**
	 * How far down the camera renders. 0 = automatic (down to the bounds floor plus a
	 * margin). Reduce it to slice off lower floors in a multi-storey building.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Height",
		meta = (ClampMin = "0.0", Units = "cm"))
	float CaptureDepth = 0.0f;

	// --- Visibility -------------------------------------------------------

	/** Hide the local player's pawn from the capture only. It stays visible in game. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility")
	bool bHideLocalPlayerPawn = true;

	/**
	 * Actors carrying any of these tags are hidden from the capture only.
	 * Resolved by one actor iteration per refresh - never per frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility")
	TArray<FName> ExclusionTags;

	/**
	 * Switch to a show-only workflow: ONLY tagged actors (plus the bounds volume's
	 * explicit inclusion list) are rendered. Everything else disappears from the capture.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility")
	bool bUseShowOnlyList = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visibility",
		meta = (EditCondition = "bUseShowOnlyList"))
	TArray<FName> InclusionTags;

	// --- Refresh ----------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Refresh")
	EMinimapRefreshPolicy RefreshPolicy = EMinimapRefreshPolicy::CaptureOnceWhenReady;

	/**
	 * Refresh requests inside this window collapse into a single capture. A batch of
	 * furniture edits therefore costs one render, not one per item.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Refresh",
		meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "s"))
	float RefreshCoalesceSeconds = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Refresh",
		meta = (EditCondition = "RefreshPolicy == EMinimapRefreshPolicy::ThrottledPeriodic",
			ClampMin = "0.5", Units = "s"))
	float PeriodicRefreshInterval = 10.0f;

	/**
	 * Small settle delay before the first capture, so streamed geometry has a chance to
	 * appear. This is a convenience, NOT the readiness mechanism - call
	 * NotifyMinimapContentReady() or RequestBackgroundRefresh() for streamed or
	 * procedurally generated content.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Refresh",
		meta = (ClampMin = "0.0", ClampMax = "10.0", Units = "s"))
	float InitialCaptureDelay = 0.25f;

	// --- Visual -----------------------------------------------------------

	/**
	 * Disable bloom, vignette, motion blur, DOF and chromatic fringe for a cleaner map.
	 * Does NOT touch exposure - that is ExposureMode's job.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visual")
	bool bUseFlatCaptureLook = true;

	/** Leave on Inherit Scene unless the capture is genuinely too bright or too dark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visual")
	EMinimapCaptureExposureMode ExposureMode = EMinimapCaptureExposureMode::InheritScene;

	/** EV offset. Applied for Fixed Auto Exposure and Manual; ignored for Inherit Scene. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visual",
		meta = (EditCondition = "ExposureMode != EMinimapCaptureExposureMode::InheritScene",
			UIMin = "-8.0", UIMax = "8.0"))
	float ExposureBias = 0.0f;

	/** Brightness that auto exposure is pinned to in Fixed Auto Exposure mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visual",
		meta = (EditCondition = "ExposureMode == EMinimapCaptureExposureMode::FixedAutoExposure",
			ClampMin = "0.001"))
	float FixedExposureBrightness = 1.0f;

	/**
	 * Extra CaptureScene() passes per refresh. A one-shot capture has no temporal history,
	 * so effects that accumulate over frames (TAA, Lumen GI) can come back black or noisy
	 * on the first pass. With bAlwaysPersistRenderingState these extra passes let that
	 * history build. 1 is usually enough; raise it if the map is still dark under Lumen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Refresh",
		meta = (ClampMin = "0", ClampMax = "8"))
	int32 WarmUpPasses = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visual")
	FLinearColor ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	/** Capture alpha so uncovered areas are transparent rather than black. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Capture|Visual")
	bool bCaptureAlpha = false;

	/** Clamp everything to a safe range. Called before the settings are used. */
	void SanitizeInPlace()
	{
		CaptureResolution       = FMath::Clamp(CaptureResolution, 64, 4096);
		AutoHeightMargin        = FMath::Max(AutoHeightMargin, 0.0f);
		RelativeHeightAlpha     = FMath::Clamp(RelativeHeightAlpha, 0.0f, 1.0f);
		CaptureDepth            = FMath::Max(CaptureDepth, 0.0f);
		RefreshCoalesceSeconds  = FMath::Clamp(RefreshCoalesceSeconds, 0.0f, 5.0f);
		PeriodicRefreshInterval = FMath::Max(PeriodicRefreshInterval, 0.5f);
		InitialCaptureDelay     = FMath::Clamp(InitialCaptureDelay, 0.0f, 10.0f);
		ExposureBias            = FMath::Clamp(ExposureBias, -8.0f, 8.0f);
		FixedExposureBrightness = FMath::Max(FixedExposureBrightness, 0.001f);
		WarmUpPasses            = FMath::Clamp(WarmUpPasses, 0, 8);
	}
};

/** One problem found by Validate Minimap Setup. */
USTRUCT(BlueprintType)
struct MINIMAP_API FMinimapValidationIssue
{
	GENERATED_BODY()

	/** True for a blocking error, false for a warning. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	bool bIsError = false;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	FString Message;
};

/** Result of Validate Minimap Setup. */
USTRUCT(BlueprintType)
struct MINIMAP_API FMinimapValidationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	TArray<FMinimapValidationIssue> Issues;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	int32 ErrorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	int32 WarningCount = 0;

	void Add(bool bIsError, const FString& Message)
	{
		// Explicit construction: a USTRUCT with GENERATED_BODY is not an aggregate, so
		// brace-init would not compile here.
		FMinimapValidationIssue Issue;
		Issue.bIsError = bIsError;
		Issue.Message = Message;
		Issues.Add(MoveTemp(Issue));

		if (bIsError) { ++ErrorCount; } else { ++WarningCount; }
	}

	bool IsValid() const { return ErrorCount == 0; }

	FString ToDisplayString() const
	{
		if (Issues.Num() == 0)
		{
			return TEXT("Minimap setup OK - no issues found.");
		}
		FString Result = FString::Printf(TEXT("Minimap setup: %d error(s), %d warning(s)\n"),
			ErrorCount, WarningCount);
		for (const FMinimapValidationIssue& Issue : Issues)
		{
			Result += FString::Printf(TEXT("  [%s] %s\n"),
				Issue.bIsError ? TEXT("ERROR") : TEXT("WARN "), *Issue.Message);
		}
		return Result;
	}
};
