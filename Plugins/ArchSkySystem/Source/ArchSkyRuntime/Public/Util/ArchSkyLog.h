// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/**
 * Dedicated log category for the whole plugin.
 *
 * ARCH NOTE: every diagnostic in ArchSky goes through LogArchSky. UE_LOG(LogTemp, ...)
 * is banned in this plugin because an architect running a client presentation needs to
 * be able to filter our warnings out of (or into) their output log with a single
 * `Log LogArchSky Verbose` command.
 */
ARCHSKYRUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogArchSky, Log, All);

/** Cycle counters surfaced by `stat ArchSky`. */
DECLARE_STATS_GROUP(TEXT("ArchSky"), STATGROUP_ArchSky, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("ArchSky Director Tick"), STAT_ArchSky_DirectorTick, STATGROUP_ArchSky, ARCHSKYRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ArchSky Solar Math"), STAT_ArchSky_SolarMath, STATGROUP_ArchSky, ARCHSKYRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ArchSky Lunar Math"), STAT_ArchSky_LunarMath, STATGROUP_ArchSky, ARCHSKYRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ArchSky Sky Recapture"), STAT_ArchSky_SkyRecapture, STATGROUP_ArchSky, ARCHSKYRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ArchSky MPC Write"), STAT_ArchSky_MpcWrite, STATGROUP_ArchSky, ARCHSKYRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ArchSky Weather Apply"), STAT_ArchSky_WeatherApply, STATGROUP_ArchSky, ARCHSKYRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("ArchSky Subsystem Tick"), STAT_ArchSky_SubsystemTick, STATGROUP_ArchSky, ARCHSKYRUNTIME_API);

/**
 * Logs `Format` exactly once per call site for the lifetime of the process.
 * Used for "your scene is missing X" style complaints that would otherwise spam
 * a per-frame log at 120 Hz.
 */
#define ARCHSKY_LOG_ONCE(Verbosity, ...) \
	do \
	{ \
		/* Block-scoped, so each call site gets its own flag without token pasting. */ \
		static bool bArchSkyLoggedOnce = false; \
		if (!bArchSkyLoggedOnce) \
		{ \
			bArchSkyLoggedOnce = true; \
			UE_LOG(LogArchSky, Verbosity, __VA_ARGS__); \
		} \
	} while (0)
