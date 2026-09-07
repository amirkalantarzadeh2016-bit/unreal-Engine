#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MinimapCaptureTypes.h"
#include "MinimapPresetAsset.generated.h"

/**
 * Portable minimap configuration, shareable across levels and projects.
 *
 * Deliberately contains NO level-specific actor references. Excluded / included actors are
 * per-level data and live on the AMinimapBoundsVolume instance instead, so dropping this
 * asset into another project can never drag in a dangling actor reference.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Minimap Preset"))
class MINIMAP_API UMinimapPresetAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Background source, resolution, height policy, refresh policy, exclusion tags, look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset")
	FMinimapCaptureSettings CaptureSettings;

	// --- Calibration defaults applied to a bounds volume that opts in ------

	/** Square the effective extent so world distances are undistorted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset|Calibration")
	bool bPreserveAspectRatio = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset|Calibration")
	bool bCircularMap = false;

	/** Axis convention. Leave all false unless matching a pre-authored map texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset|Calibration")
	bool bSwapUV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset|Calibration")
	bool bInvertU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset|Calibration")
	bool bInvertV = false;

	/** Apply the calibration block above, not just the capture settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap|Preset|Calibration")
	bool bApplyCalibrationDefaults = false;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("MinimapPreset"), GetFName());
	}
};
