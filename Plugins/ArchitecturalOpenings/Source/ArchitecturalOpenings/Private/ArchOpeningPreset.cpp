// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningPreset.h"

#include "ArchOpeningLog.h"

#if WITH_EDITOR

void UArchOpeningPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// A preset is a reusable asset; a reference to a component in one particular level has no
	// meaning here and would create a cross-package reference from an asset into a map. The
	// interaction settings struct carries a proxy list because openings need one, so the preset
	// clears it rather than letting it be authored by accident. ApplyPreset also ignores it and
	// preserves whatever the target opening already had.
	if (Interaction.InteractionProxies.Num() > 0)
	{
		Interaction.InteractionProxies.Reset();

		UE_LOG(LogArchOpenings, Warning,
			TEXT("'%s': interaction proxies were cleared. Presets deliberately hold no level-specific object references; assign proxies on the opening in the level instead."),
			*GetPathName());
	}
}

#endif // WITH_EDITOR
