// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "TourRuntimeSettings.generated.h"

class UTourInputConfig;

/**
 * Project Settings > Plugins > ArchViz Tour (Runtime).
 *
 * Kept in the runtime module rather than the editor one because a packaged client reads
 * these: the editor settings that follow are authoring defaults only.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ArchViz Tour (Runtime)"))
class ARCHVIZTOURRUNTIME_API UTourRuntimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTourRuntimeSettings();

	virtual FName GetCategoryName() const override;

	/**
	 * Push the plugin's Enhanced Input context when a tour starts.
	 *
	 * Off by default: Space, Escape and the arrow keys almost always already mean something in
	 * the host project, and silently stealing them is worse than making the user opt in.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Input")
	bool bEnableDefaultInput = false;

	/** Bindings used when bEnableDefaultInput is set. */
	UPROPERTY(EditAnywhere, Config, Category = "Input", meta = (EditCondition = "bEnableDefaultInput"))
	TSoftObjectPtr<UTourInputConfig> InputConfig;

	/** Default traversal speed for a newly created path, in centimetres per second. */
	UPROPERTY(EditAnywhere, Config, Category = "Playback", meta = (ClampMin = "0.01", Units = "cm/s"))
	float DefaultPathSpeed = 200.0f;

	/** Default view-target blend length for a newly created step, in seconds. */
	UPROPERTY(EditAnywhere, Config, Category = "Playback", meta = (ClampMin = "0.0", Units = "s"))
	float DefaultBlendTime = 1.0f;

	/** Default value of UTourSubsystem::bUseUnpausedDeltaTime. */
	UPROPERTY(EditAnywhere, Config, Category = "Playback")
	bool bUseUnpausedDeltaTime = false;

	/** Convenience accessor; never null, since UDeveloperSettings has a CDO for the life of the process. */
	static const UTourRuntimeSettings& Get();
};
