// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ArchSkySettings.h"

#include "Util/ArchSkyLog.h"

UArchSkySettings::UArchSkySettings()
{
	// Tehran, matching DefaultLocationId, so the fallback is never a surprise jump.
	FallbackLocation = FArchGeoLocation(35.6892, 51.3890, 3.5f, 1200.f);

	// ARCH NOTE: the default scan path is the plugin's own Data folder rather than the
	// project's content root. Scanning /Game wholesale on a large archviz project costs
	// seconds of asset-registry work at startup for no benefit; a studio that keeps its
	// presets elsewhere adds their path here explicitly.
	FDirectoryPath PluginDataPath;
	PluginDataPath.Path = TEXT("/ArchSkySystem/Data");
	PresetScanPaths.Add(PluginDataPath);
}

const UArchSkySettings* UArchSkySettings::Get()
{
	const UArchSkySettings* Settings = GetDefault<UArchSkySettings>();

	// GetDefault never returns null for a UCLASS that is linked in, but the plugin's whole
	// contract is "never crash on a missing reference", so we hold ourselves to it too.
	if (!Settings)
	{
		ARCHSKY_LOG_ONCE(Error, TEXT("UArchSkySettings CDO is unavailable; falling back to hard-coded defaults."));
	}

	return Settings;
}

FName UArchSkySettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
