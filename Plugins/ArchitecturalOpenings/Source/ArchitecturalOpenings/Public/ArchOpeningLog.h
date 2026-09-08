// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Dedicated log category for the Architectural Openings plugin.
 *
 * Nothing in the plugin logs from an animation tick. Runtime diagnostics that could repeat are
 * routed through UArchOpeningComponent::LogOnce(), which de-duplicates by message key.
 */
ARCHITECTURALOPENINGS_API DECLARE_LOG_CATEGORY_EXTERN(LogArchOpenings, Log, All);
