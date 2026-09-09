// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "TourInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Enhanced Input bindings for tour playback.
 *
 * Every binding is optional and the whole context is off by default (see
 * UTourRuntimeSettings::bEnableDefaultInput): a plugin that silently claims Space and the
 * arrow keys will collide with whatever the host project already binds them to.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tour Input Config"))
class ARCHVIZTOURRUNTIME_API UTourInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Context pushed onto the local player's Enhanced Input subsystem while a tour is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	/** Priority the context is pushed with. Higher wins over lower-priority contexts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (ClampMin = "0"))
	int32 MappingPriority = 0;

	/** Suggested default: Space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> TogglePauseAction;

	/** Suggested default: Right Arrow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> NextStepAction;

	/** Suggested default: Left Arrow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> PreviousStepAction;

	/** Suggested default: Escape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> StopTourAction;

	/** Suggested default: R. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> RestartTourAction;
};
