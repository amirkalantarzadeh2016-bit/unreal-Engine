// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchSkyPlaybackTypes.generated.h"

/**
 * The four named playback speeds, plus Custom for a hand-entered multiplier.
 *
 * SpeedMultiplier is a RATIO of simulated time to real time, which is what makes every
 * label below self-consistent: x1 really is real time, and x3600 really is one simulated
 * hour per real second. See the unit note in ArchSkyPlaybackSubsystem.cpp.
 *
 * ARCH NOTE: this enum lives in its own header rather than in ArchSkyPlaybackSubsystem.h
 * so that UArchSkySettings can name it without pulling a UWorldSubsystem and Tickable.h
 * into every translation unit that includes the settings - which, via ArchSkyDirector.h,
 * is most of them.
 */
UENUM(BlueprintType)
enum class EArchPlaybackSpeedPreset : uint8
{
	/** x1 - one simulated second per real second. */
	RealTime			UMETA(DisplayName = "Real-time (x1)"),
	/** x60 - one simulated minute per real second. The default. */
	Fast				UMETA(DisplayName = "Fast (x60 - 1 min/sec)"),
	/** x3600 - one simulated hour per real second. */
	HourPerSecond		UMETA(DisplayName = "Hour per second (x3600)"),
	/** x8640 - a full 24 hour day in ten real seconds. */
	FullDayTenSeconds	UMETA(DisplayName = "Full day in 10 s (x8640)"),
	/** Any other multiplier, entered by hand. Reported, never selected. */
	Custom				UMETA(DisplayName = "Custom")
};
