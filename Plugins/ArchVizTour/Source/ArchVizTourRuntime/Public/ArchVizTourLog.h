// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Stats/Stats.h"

/**
 * Dedicated log category for every ArchViz Tour module.
 *
 * Recoverable authoring mistakes (a step that names a missing ATourPath, a zero-duration
 * step, an empty preset) are reported here as Warnings and the offending step is skipped;
 * they must never assert or crash a client build.
 */
ARCHVIZTOURRUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogArchVizTour, Log, All);

/** Stat group for the per-frame tour evaluation, visible via `stat ArchVizTour`. */
DECLARE_STATS_GROUP(TEXT("ArchVizTour"), STATGROUP_ArchVizTour, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Tour Subsystem Tick"), STAT_ArchVizTour_SubsystemTick, STATGROUP_ArchVizTour, ARCHVIZTOURRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Tour Path Evaluate"), STAT_ArchVizTour_PathEvaluate, STATGROUP_ArchVizTour, ARCHVIZTOURRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Tour Rig Apply State"), STAT_ArchVizTour_RigApplyState, STATGROUP_ArchVizTour, ARCHVIZTOURRUNTIME_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Tour Rail Rebuild"), STAT_ArchVizTour_RailRebuild, STATGROUP_ArchVizTour, ARCHVIZTOURRUNTIME_API);
