// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourEditorSettings.h"

UTourEditorSettings::UTourEditorSettings()
{
	SectionName = TEXT("ArchViz Tour");
	DefaultSequenceBakeDirectory.Path = TEXT("/Game/Cinematics/TourSequences");
}

FName UTourEditorSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

const UTourEditorSettings& UTourEditorSettings::Get()
{
	const UTourEditorSettings* Settings = GetDefault<UTourEditorSettings>();
	check(Settings != nullptr);
	return *Settings;
}
