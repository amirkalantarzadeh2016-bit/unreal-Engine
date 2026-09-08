// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ArchOpeningTypes.h"

#include "ArchOpeningPreset.generated.h"

/**
 * Reusable behaviour preset.
 *
 * Deliberately holds NO level-specific data: no part references, no hinge location, no slide
 * direction, no proximity box placement. Applying a preset therefore cannot destroy an artist's
 * assigned meshes or calibrated pivots. Handle behaviour is applied to the settings of existing
 * handle groups only; it never creates, removes or reassigns groups.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Architectural Opening Preset"))
class ARCHITECTURALOPENINGS_API UArchOpeningPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preset")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening Type")
	bool bApplyMotionType = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening Type",
		meta = (EditCondition = "bApplyMotionType"))
	EArchOpeningMotionType MotionType = EArchOpeningMotionType::Hinged;

	/**
	 * Angle and travel distance are motion "size" rather than placement, so they are carried by the
	 * preset. Hinge location, hinge side, slide direction and swing direction are not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening Type")
	bool bApplyOpenAngleAndTravel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening Type",
		meta = (Units = "deg", ClampMin = "0.0", ClampMax = "179.0", EditCondition = "bApplyOpenAngleAndTravel"))
	float OpenAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Opening Type",
		meta = (Units = "cm", ClampMin = "0.0", EditCondition = "bApplyOpenAngleAndTravel"))
	float TravelDistance = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing")
	bool bApplyTiming = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (EditCondition = "bApplyTiming"))
	FArchOpeningTimingSettings Timing;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles")
	bool bApplyHandleBehaviour = true;

	/** Applied to every existing handle group's behaviour fields. Parts and pivots are untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (EditCondition = "bApplyHandleBehaviour"))
	float HandleRotationAngle = -40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (Units = "s", ClampMin = "0.0", EditCondition = "bApplyHandleBehaviour"))
	float HandleActuationDuration = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (Units = "s", ClampMin = "0.0", EditCondition = "bApplyHandleBehaviour"))
	float HandleReturnDuration = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (Units = "s", ClampMin = "0.0", EditCondition = "bApplyHandleBehaviour"))
	float HandleDelayBeforeLeafMovement = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handles",
		meta = (EditCondition = "bApplyHandleBehaviour"))
	EArchOpeningHandleReturn HandleReturnBehavior = EArchOpeningHandleReturn::ReturnAfterActuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bApplyInteraction = true;

	/** InteractionProxies are intentionally not carried: they are level-specific references. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (EditCondition = "bApplyInteraction"))
	FArchOpeningInteractionSettings Interaction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity")
	bool bApplyProximity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity",
		meta = (EditCondition = "bApplyProximity"))
	FArchOpeningProximitySettings Proximity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction")
	bool bApplyObstruction = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Obstruction",
		meta = (EditCondition = "bApplyObstruction"))
	FArchOpeningObstructionSettings Obstruction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bApplyAudio = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio",
		meta = (EditCondition = "bApplyAudio"))
	FArchOpeningAudioSettings Audio;

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif
};
