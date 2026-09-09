// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourRuntimeSettings.h"

#define LOCTEXT_NAMESPACE "ArchVizTour"

UTourRuntimeSettings::UTourRuntimeSettings()
{
	// Groups the settings under "ArchViz Tour" in the Plugins section rather than scattering
	// them under the module name.
	SectionName = TEXT("ArchViz Tour");
}

FName UTourRuntimeSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

const UTourRuntimeSettings& UTourRuntimeSettings::Get()
{
	const UTourRuntimeSettings* Settings = GetDefault<UTourRuntimeSettings>();
	check(Settings != nullptr);
	return *Settings;
}

#undef LOCTEXT_NAMESPACE
